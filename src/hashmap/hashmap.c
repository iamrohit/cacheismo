#include "hashmap.h"
#include "hash.h"
#include "../datastream/datastream.h"
#include <time.h>

/* This is the cache/hashtable implementation that we will use. Most people use
 * chaining based hashtables. I am using Linear Hashing algorithm by Litwin.
 * Usually it is used in  file systems. I choose this over the usual hashtable
 * implementation because it can easily grow without modifying the complete
 * hashtable. For our use case it would be suicidal to restructure the complete
 * hashtable when it contains millions of entries.
 *
 * For the first cut I am using realloc to double the size of array but I don't
 * need to rehash all the existing entries. Other alternatives are dynamic arrays.
 * This will be a simple change if required. Moreover realloc shouldn't take much
 * time on linux because all we are asking for is address space..pages can come later.
 *
 * The relationship between cache/RAM is similar to relationship between RAM
 * and disk. Hence one of the main goal for this implementation is to be as much
 * cache friendly as possible without compromising the simplicity of the code.
 * This is just a reminder for myself to think about page-faults.
 *
 * Ideally we should store the object in the bucket itself because only then we would
 * be able to get to the object with a single page-fault. But given that objects are
 * of variable sizes, we will need at least one indirection.
 *
 * It is Ok if we don't merge when the elements are deleted, but will help in reducing
 * memory usage. For first cut no merge.
 */


typedef struct hashEntry_t{
	u_int32_t           magic;
	struct hashEntry_t* pMapNext;
	void*               value;
	struct hashEntry_t* pLRUNext;
	struct hashEntry_t* pLRUPrev;
	u_int32_t           hashCode;
	u_int32_t           position; //position in min heap for expiry
} hashEntry_t;

/**
 *	Heap starts at index 1 instead of 0
 */

typedef struct {
	u_int32_t        size;
	u_int32_t        capacity;
	hashEntryAPI_t*  API;
	hashEntry_t**    queue;
}minHeapImpl_t;

typedef struct hashMapImpl_t {
	u_int32_t        count;
    u_int32_t        size;
    u_int32_t        maskedBits;
	u_int32_t        splitAt;
	u_int32_t        maxSplit;
    hashEntryAPI_t*  API;
	hashEntry_t**    pBuckets;
	minHeapImpl_t*   pMinHeap;
	hashEntry_t*     pLRUListHead;
	hashEntry_t*     pLRUListTail;
}hashMapImpl_t;


#define HASHMAPIMPL(x) ((hashMapImpl_t*)(x))
#define INITIAL_HASHMAP_SIZE_BITS   16
#define INITIAL_HASHMAP_SIZE        (hashsize(INITIAL_HASHMAP_SIZE_BITS))
#define INITIAL_MAXSPLIT_BITS       3
#define INITIAL_MAXSPLIT_SIZE       (hashsize(INITIAL_MAXSPLIT_BITS))

#define hashsize(n) ((u_int32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)

static void checkMagic(hashEntry_t* pElement)  {
	if (pElement) {
		if (pElement->magic != 123456) {
			 char* a = 0;
			  *a      = 1;
		}
	}
}

static u_int32_t currentTimeInSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return  (u_int32_t) (ts.tv_sec);
}


static void removeFromLRUList(hashMapImpl_t* pHashMap, hashEntry_t* pEntry) {
	if (pEntry->pLRUPrev) {
		if (pEntry->pLRUNext) {
			pEntry->pLRUPrev->pLRUNext = pEntry->pLRUNext;
			pEntry->pLRUNext->pLRUPrev = pEntry->pLRUPrev;
		}else {
			pEntry->pLRUPrev->pLRUNext = 0;
			pHashMap->pLRUListTail     = pEntry->pLRUPrev;
		}
	}else {
		if (pEntry->pLRUNext) {
			pHashMap->pLRUListHead = pEntry->pLRUNext;
			pEntry->pLRUNext->pLRUPrev = 0;
		}else {
			pHashMap->pLRUListHead = 0;
			pHashMap->pLRUListTail = 0;
		}
	}
	pEntry->pLRUNext = 0;
	pEntry->pLRUPrev = 0;
	checkMagic(pEntry);
}

static void makeHeadOfLRUList(hashMapImpl_t* pHashMap, hashEntry_t* pEntry) {
	pEntry->pLRUNext = pHashMap->pLRUListHead;
	pEntry->pLRUPrev = 0;

	if (pHashMap->pLRUListHead) {
		pHashMap->pLRUListHead->pLRUPrev = pEntry;
	}else {
		pHashMap->pLRUListTail = pEntry;
	}
	pHashMap->pLRUListHead = pEntry;
	checkMagic(pEntry);
}


static minHeapImpl_t*  minHeapCreate(hashEntryAPI_t* API, int initialSize) {
	minHeapImpl_t* pMinHeap = ALLOCATE_1(minHeapImpl_t);
	if (pMinHeap) {
		pMinHeap->capacity = initialSize;
		pMinHeap->size     = 0;
		pMinHeap->queue    = ALLOCATE_N(initialSize, hashEntry_t*);
		pMinHeap->API      = API;
	}
	return pMinHeap;
}

#define GET_EXPIRY(heap, atIndex)  ((heap)->API->getExpiry((heap)->queue[(atIndex)]->value))

static void fixUp(minHeapImpl_t* pMinHeap, int k) {
	while (k > 1) {
		int j = k >> 1;
		if (GET_EXPIRY(pMinHeap,j) <= GET_EXPIRY(pMinHeap,k))
			break;
		void* tmp = pMinHeap->queue[j];  pMinHeap->queue[j] = pMinHeap->queue[k]; pMinHeap->queue[k] = tmp;
		pMinHeap->queue[j]->position = j;
		pMinHeap->queue[k]->position =  k;
		k = j;
	}
}

static void fixDown(minHeapImpl_t* pMinHeap, int k) {
	int j = 0;
	while ((j = k << 1) <= pMinHeap->size && j > 0) {
		if (j < pMinHeap->size &&
				GET_EXPIRY(pMinHeap, j) > GET_EXPIRY(pMinHeap, j+1))
			j++; // j indexes smallest kid
		if (GET_EXPIRY(pMinHeap, k) <= GET_EXPIRY(pMinHeap, j))
			break;
		void* tmp = pMinHeap->queue[j];  pMinHeap->queue[j] = pMinHeap->queue[k]; pMinHeap->queue[k] = tmp;
		pMinHeap->queue[j]->position =  j;
		pMinHeap->queue[k]->position =  k;
		k = j;
	}
}

static int minHeapInsert(minHeapImpl_t* pMinHeap, hashEntry_t* object) {
	int            returnValue = 0;

	IfTrue(pMinHeap, ERR, "Null heap");
	IfTrue(object, ERR, "Null object");

	if (++pMinHeap->size >= pMinHeap->capacity) {
		void* newQueue = realloc(pMinHeap->queue, sizeof(hashEntry_t*) * pMinHeap->capacity * 2);
		if (!newQueue) {
			//decrease the size (we increased it above)
			pMinHeap->size--;
			LOG(WARN, "Error increasing min heap size");
			goto OnError;
		}else {
			memset(((hashEntry_t**)newQueue)+pMinHeap->capacity, 0, sizeof(hashEntry_t*) * pMinHeap->capacity);
			pMinHeap->queue = newQueue;
			pMinHeap->capacity = 2 * pMinHeap->capacity;
		}
	}
	pMinHeap->queue[pMinHeap->size] = object;
	object->position = pMinHeap->size;
	fixUp(pMinHeap, pMinHeap->size);
	checkMagic(object);
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}

static int minHeapDelete(minHeapImpl_t* pMinHeap, hashEntry_t* object) {
	int            returnValue = 0;
	int            objectIndex = 0;

	IfTrue(pMinHeap, ERR,  "Null heap");
	IfTrue(object, ERR, "Null object");

	objectIndex = object->position;
	object->position = 0;
	pMinHeap->queue[objectIndex] = pMinHeap->queue[pMinHeap->size];
    pMinHeap->queue[pMinHeap->size] = 0;
    pMinHeap->size--;

    if (pMinHeap->queue[objectIndex]) {
    	pMinHeap->queue[objectIndex]->position = objectIndex;
    }
    fixDown(pMinHeap, objectIndex);
    checkMagic(object);
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}


/* return min if min is less than lessThan */
static hashEntry_t* minHeapGetMin(minHeapImpl_t* pMinHeap, u_int32_t lessThan) {
	hashEntry_t*    object = 0;

	IfTrue(pMinHeap, ERR, "Null min heap");
	if (pMinHeap->size <= 0) {
		goto OnSuccess;
	}
	object = pMinHeap->queue[1];

	if (pMinHeap->API->getExpiry(object->value) <= lessThan) {
		pMinHeap->queue[1] = pMinHeap->queue[pMinHeap->size];
		pMinHeap->queue[pMinHeap->size] = 0;
		pMinHeap->size--;
		if (pMinHeap->queue[1]) {
			pMinHeap->queue[1]->position =  1;
		}
		fixDown(pMinHeap, 1);
		checkMagic(object);
	}else {
		object = 0;
	}
	goto OnSuccess;
OnError:
	object = 0;
OnSuccess:
	return object;
}

static u_int32_t hashcode( hashMapImpl_t* pHashMap, const char* key, u_int32_t keyLength) {
	return hash((u_int32_t*)key, (size_t)(keyLength), 0xFEEDDEED);
}

hashMap_t hashMapCreate(hashEntryAPI_t* API ) {
	hashMapImpl_t* pHashMap = ALLOCATE_1(hashMapImpl_t);
    IfTrue(pHashMap, ERR, "Error allocating memory");

    pHashMap->count      = 0;
    pHashMap->size       = INITIAL_HASHMAP_SIZE;
    pHashMap->maxSplit   = INITIAL_MAXSPLIT_SIZE/2;
    pHashMap->maskedBits = INITIAL_MAXSPLIT_BITS-1;
    pHashMap->pBuckets   = (hashEntry_t**)ALLOCATE_N(INITIAL_HASHMAP_SIZE, hashEntry_t*);
    IfTrue(pHashMap->pBuckets, ERR, "Error allocating memory");
    pHashMap->API        = API;
    pHashMap->pMinHeap   = minHeapCreate(API, INITIAL_HASHMAP_SIZE);

    goto OnSuccess;
OnError:
    if (pHashMap) {
        hashMapDelete(pHashMap);
        pHashMap = 0;
    }
OnSuccess:
    return pHashMap;
}

u_int32_t hashMapSize(hashMap_t hashMap) {
	hashMapImpl_t* pHashMap = HASHMAPIMPL(hashMap);
	if (pHashMap) {
		return pHashMap->count;
	}
	return 0;
}

void hashMapDelete(hashMap_t hashMap) {
    hashMapImpl_t* pHashMap = HASHMAPIMPL(hashMap);
    if (pHashMap) {
        if (pHashMap->pBuckets) {
            FREE(pHashMap->pBuckets);
            pHashMap->pBuckets = 0;
        }
        if (pHashMap->pMinHeap) {
        	//TODO : no delete function for min heap
        }
        FREE(pHashMap);
    }
}



static u_int32_t bucketOffset(hashMapImpl_t* pHashMap, u_int32_t hashValue) {
    u_int32_t offset = hashmask(pHashMap->maskedBits) & hashValue;
    if (pHashMap->splitAt != 0) {
         if (offset < pHashMap->splitAt) {
            offset = hashmask(pHashMap->maskedBits+1) & hashValue;
         }
    }
    return offset;
}

static void splitBucket(hashMapImpl_t* pHashMap) {
    u_int32_t    fromOffset = pHashMap->splitAt;
    u_int32_t    toOffset   = pHashMap->splitAt+pHashMap->maxSplit;
    hashEntry_t* pCurrent   = pHashMap->pBuckets[fromOffset];
    hashEntry_t* pPrev      = 0;
    //printf("SPLIT - splitAt %d maxSplit %d size %d count %d\n",
    //		pHashMap->splitAt, pHashMap->maxSplit, pHashMap->size, pHashMap->count);
    pHashMap->pBuckets[toOffset] = 0;

    while (pCurrent) {
    	hashEntry_t* pNext = pCurrent->pMapNext;
    	checkMagic(pCurrent);
        if ((pCurrent->hashCode & hashmask(pHashMap->maskedBits+1)) == toOffset) {
            if (pHashMap->pBuckets[toOffset]) {
            	//enusre that bucket doesn't have bad pointer
            	checkMagic(pHashMap->pBuckets[toOffset]);
            //	printf("%d\n", pHashMap->pBuckets[toOffset]->position);
            }
            pCurrent->pMapNext = pHashMap->pBuckets[toOffset];
            pHashMap->pBuckets[toOffset] = pCurrent;
            //remove from old list
			if (pPrev) {
				pPrev->pMapNext = pNext;
			}else {
				pHashMap->pBuckets[fromOffset] = pNext;
			}
        }else {
        	pPrev = pCurrent;
        }
        pCurrent = pNext;
    }
    pHashMap->splitAt++;
}

// Delete everything that has expired..
// many people get confused looking at stats..
// because expired items continue to be present
// we will just delete everything that has expired
// makes us slow, but keep stats sane

u_int32_t hashMapDeleteExpired(hashMap_t hashMap) {
    hashMapImpl_t*   pHashMap    = HASHMAPIMPL(hashMap);
    u_int32_t        freeSpace   = 0;
    u_int32_t        currentTime = currentTimeInSeconds();
    hashEntry_t*     pEntry      = 0;

    while ((pEntry = minHeapGetMin(pHashMap->pMinHeap, currentTime)) != 0) {
    	freeSpace += pHashMap->API->getTotalSize(pEntry->value);
    	//delete this element
    	checkMagic(pEntry);
    	hashMapDeleteElement(pHashMap, pHashMap->API->getKey(pEntry->value),
    			pHashMap->API->getKeyLength(pEntry->value));
    }
    //printf("\nhashMapDeleteExpired freed %d bytes\n", freeSpace);
    return freeSpace;
}

//       if not enough space created ..
//       remove stuff from LRU list
u_int64_t  hashMapDeleteLRU(hashMap_t hashMap, u_int64_t requiredSpace) {
	hashMapImpl_t*   pHashMap  = HASHMAPIMPL(hashMap);
	u_int64_t        freeSpace = 0;
	hashEntry_t*     pEntry    = pHashMap->pLRUListTail;

	while ((freeSpace < requiredSpace) && pEntry) {
		freeSpace += pHashMap->API->getTotalSize(pEntry->value);
		checkMagic(pEntry);
		hashMapDeleteElement(pHashMap, pHashMap->API->getKey(pEntry->value),
				pHashMap->API->getKeyLength(pEntry->value));
		pEntry = pHashMap->pLRUListTail;
	}
	return freeSpace;
}

int hashMapPutElement(hashMap_t hashMap, void* value) {
    int              returnValue = 0;
    hashMapImpl_t*   pHashMap    = HASHMAPIMPL(hashMap);
    hashEntry_t*     pElement    = 0;
    u_int32_t        hashValue   = 0;
    u_int32_t        bucket      = 0;

    IfTrue(pHashMap,  ERR, "Null argument");
    pElement = ALLOCATE_1(hashEntry_t);
    IfTrue(pElement, WARN, "Error allocating memory");

    hashValue = hashcode(pHashMap, pHashMap->API->getKey(value),pHashMap->API->getKeyLength(value));

    bucket    = bucketOffset(pHashMap, hashValue);

    pElement->value    = value;
    pElement->hashCode = hashValue;
    pElement->magic    = 123456;

    pElement->pMapNext = pHashMap->pBuckets[bucket];
    pHashMap->pBuckets[bucket] = pElement;
    pHashMap->count++;

    minHeapInsert(pHashMap->pMinHeap, pElement);
    makeHeadOfLRUList(pHashMap, pElement);

    if (pHashMap->count > pHashMap->maxSplit) {
        splitBucket(pHashMap);
    }
    if (pHashMap->splitAt == pHashMap->maxSplit) {
    	 if ((pHashMap->maxSplit*2) >= pHashMap->size) {
			 hashEntry_t** newBuckets  = realloc(pHashMap->pBuckets, 2 * pHashMap->size * sizeof(hashEntry_t*));
			 IfTrue(newBuckets, WARN, "Error reallocating memory");
			 memset(newBuckets+pHashMap->size, 0, pHashMap->size * sizeof(hashEntry_t*));
			 pHashMap->pBuckets = newBuckets;
			 pHashMap->size     = 2 * pHashMap->size;
    	 }
         pHashMap->splitAt  = 0;
         pHashMap->maskedBits++;
         pHashMap->maxSplit = pHashMap->maxSplit * 2;
    }
    checkMagic(pElement);
    goto OnSuccess;
OnError:
    if (!returnValue) {
        returnValue = -1;
    }
OnSuccess:
    return returnValue;
}


void* hashMapGetElement(hashMap_t hashMap, char* key, u_int32_t keyLength) {
    hashMapImpl_t* pHashMap  = HASHMAPIMPL(hashMap);
    hashEntry_t*   pElement  = 0;
    hashEntry_t*   pPrev     = 0;
    u_int32_t      hashValue = 0;
    u_int32_t      bucket    = 0;

    IfTrue(pHashMap, ERR, "Null argument");
    IfTrue(key && (keyLength > 0), INFO, "Invalid argument");

    hashValue = hashcode(pHashMap, key, keyLength);
    bucket    = bucketOffset(pHashMap, hashValue);
    pElement  = pHashMap->pBuckets[bucket];

    while (pElement) {
    	checkMagic(pElement);
        if ((pElement->hashCode == hashValue) &&
        	(pHashMap->API->getKeyLength(pElement->value) == keyLength) &&
            (0 == memcmp(pHashMap->API->getKey(pElement->value), key, keyLength))) {
                /* match found */
             break;
        }
        pPrev    = pElement;
        pElement = pElement->pMapNext;
    }

    if (pElement) {
    	//found the element in the map
    	//check if it has expired
    	if (currentTimeInSeconds() < pHashMap->API->getExpiry(pElement->value)) {
    	  	pHashMap->API->addReference(pElement->value);
    	  	removeFromLRUList(pHashMap, pElement);
    	    makeHeadOfLRUList(pHashMap, pElement);
    	    checkMagic(pElement);
    	}else {
    		//expired - delete this element */
    		if (pPrev) {
				pPrev->pMapNext = pElement->pMapNext;
			}else {
				pHashMap->pBuckets[bucket] = pElement->pMapNext;
			}
			minHeapDelete(pHashMap->pMinHeap, pElement);
	    	removeFromLRUList(pHashMap, pElement);
	    	checkMagic(pElement);
			pHashMap->API->onObjectDeleted(pHashMap->API->context, pElement->value);
			//delete the memory used by the element
			FREE(pElement);
			pHashMap->count--;
			pElement = 0;
    	}
    }
    goto OnSuccess;
OnError:
    LOG(INFO, "Error looking for key %p", key);
OnSuccess:
    return pElement ? pElement->value : 0;
}

int hashMapDeleteElement(hashMap_t hashMap, char* key, u_int32_t keyLength) {
    hashMapImpl_t* pHashMap  = HASHMAPIMPL(hashMap);
    hashEntry_t*   pElement  = 0;
    hashEntry_t*   pPrev     = 0;
    u_int32_t      hashValue = 0;
    u_int32_t      bucket    = 0;

    IfTrue(pHashMap, ERR,  "Null argument");
    IfTrue(key && (keyLength > 0), INFO, "Invalid argument");

    hashValue = hashcode(pHashMap, key, keyLength);
    bucket    = bucketOffset(pHashMap, hashValue);
    pElement  = pHashMap->pBuckets[bucket];

    while (pElement) {
    	checkMagic(pElement);
        if ((pElement->hashCode == hashValue) &&
        	(pHashMap->API->getKeyLength(pElement->value) == keyLength) &&
            (0 == memcmp(pHashMap->API->getKey(pElement->value), key, keyLength))) {
                /* match found */
             break;
        }
        pPrev    = pElement;
        pElement = pElement->pMapNext;
    }

    if (pElement) {
        /* delete this element */
    	if (pPrev) {
    		pPrev->pMapNext = pElement->pMapNext;
    	}else {
    		pHashMap->pBuckets[bucket] = pElement->pMapNext;
    	}
    	pElement->pMapNext = 0;
    	minHeapDelete(pHashMap->pMinHeap, pElement);
    	removeFromLRUList(pHashMap, pElement);
        pHashMap->API->onObjectDeleted(pHashMap->API->context, pElement->value);
        //delete the memory used by the element
        pHashMap->count--;
        FREE(pElement);
    }
    goto OnSuccess;
OnError:
    LOG(INFO, "Error looking for key %p", key);
OnSuccess:
    return 0;
}

#define DEFAULT_RESULT_SIZE 4096

u_int32_t hashMapGetPrefixMatchingKeys(hashMap_t hashMap, char* prefix, char** keys) {
    hashMapImpl_t* pHashMap     = HASHMAPIMPL(hashMap);
    hashEntry_t*   pElement     = 0;
    u_int32_t      count        = 0;
    char*          result       = 0;
    int            resultSize   = DEFAULT_RESULT_SIZE;
    int            resultUsed   = 0;
    int            prefixLength = 0;

    IfTrue(pHashMap, ERR,  "Null argument");
    IfTrue(keys && prefix, INFO, "Invalid argument");
    prefixLength = strlen(prefix);
    result       = ALLOCATE_N(resultSize, char);

    IfTrue(result, ERR, "Error allocating memory");

    for (int i = 0; i < pHashMap->size; i++) {
    	pElement  = pHashMap->pBuckets[i];
        while (pElement) {
        	int   length = pHashMap->API->getKeyLength(pElement->value);
            char* key    = pHashMap->API->getKey(pElement->value);
            if (length >= prefixLength) {
            	if (0 == memcmp(key, prefix, prefixLength)) {
            		LOG(DEBUG, "Key %s matches prefix %s", key, prefix);
            		if ((resultSize - resultUsed) < (length+1)) {
                        char* newResult = realloc(result, resultSize * 2);
                        IfTrue(newResult, ERR, "Error allocating memory");
                        result = newResult;
                        memset(result+resultSize, 0, resultSize);
                        resultSize = resultSize * 2;
            		}
            		LOG(DEBUG, "writing key at offset %d total %d", resultUsed, resultSize)
					memcpy(result+resultUsed, key, length);
					result[resultUsed+length] = 0;
					resultUsed+= length+1;
					count++;
            	}else {
            		LOG(DEBUG, "Key %s doesn't matches prefix %s", key, prefix);
            	}
            }
            pElement = pElement->pMapNext;
        }
    }
    goto OnSuccess;
OnError:
    count = 0;
    if (result) {
    	FREE(result);
    	result = 0;
    }
OnSuccess:
	if (count == 0) {
		if (result) {
			FREE(result);
			result = 0;
		}
	}
	if (keys) {
		*keys = result;
	}
    return count;
}

