#include "map.h"
#include "../hashmap/hash.h"

typedef struct hashEntry_t{
	struct hashEntry_t* pMapNext;
	char*               key;
	void*               value;
	u_int32_t           keyLength;
	u_int32_t           hashCode;
} hashEntry_t;

typedef struct hashMapImpl_t {
	u_int32_t        count;
    u_int32_t        size;
    u_int32_t        maskedBits;
	u_int32_t        splitAt;
	u_int32_t        maxSplit;
 	hashEntry_t**    pBuckets;
}hashMapImpl_t;


#define HASHMAPIMPL(x) ((hashMapImpl_t*)(x))
#define INITIAL_HASHMAP_SIZE_BITS   5
#define INITIAL_HASHMAP_SIZE        (hashsize(INITIAL_HASHMAP_SIZE_BITS))
#define INITIAL_MAXSPLIT_BITS       3
#define INITIAL_MAXSPLIT_SIZE       (hashsize(INITIAL_MAXSPLIT_BITS))

#define hashsize(n) ((u_int32_t)1<<(n))
#define hashmask(n) (hashsize(n)-1)


static u_int32_t hashcode( hashMapImpl_t* pHashMap, const char* key, u_int32_t keyLength) {
	return hash((u_int32_t*)key, (size_t)(keyLength), 0xFEEDDEED);
}

map_t mapCreate(void) {
	hashMapImpl_t* pHashMap = ALLOCATE_1(hashMapImpl_t);
    IfTrue(pHashMap, ERR, "Error allocating memory");

    pHashMap->count      = 0;
    pHashMap->size       = INITIAL_HASHMAP_SIZE;
    pHashMap->maxSplit   = INITIAL_MAXSPLIT_SIZE/2;
    pHashMap->maskedBits = INITIAL_MAXSPLIT_BITS-1;
    pHashMap->pBuckets   = (hashEntry_t**)ALLOCATE_N(INITIAL_HASHMAP_SIZE, hashEntry_t*);
    IfTrue(pHashMap->pBuckets, ERR, "Error allocating memory");

    goto OnSuccess;
OnError:
    if (pHashMap) {
        mapDelete(pHashMap);
        pHashMap = 0;
    }
OnSuccess:
    return pHashMap;
}

u_int32_t mapSize(map_t hashMap) {
	hashMapImpl_t* pHashMap = HASHMAPIMPL(hashMap);
	if (pHashMap) {
		return pHashMap->count;
	}
	return 0;
}


void mapDelete(map_t hashMap) {
    hashMapImpl_t* pHashMap = HASHMAPIMPL(hashMap);
    if (pHashMap) {
        if (pHashMap->pBuckets) {
            FREE(pHashMap->pBuckets);
            pHashMap->pBuckets = 0;
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

    pHashMap->pBuckets[toOffset] = 0;

    while (pCurrent) {
    	hashEntry_t* pNext = pCurrent->pMapNext;
    	if ((pCurrent->hashCode & hashmask(pHashMap->maskedBits+1)) == toOffset) {
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

int mapPutElement(map_t hashMap, char* key, void* value) {
    int              returnValue = 0;
    hashMapImpl_t*   pHashMap    = HASHMAPIMPL(hashMap);
    hashEntry_t*     pElement    = 0;
    u_int32_t        hashValue   = 0;
    u_int32_t        bucket      = 0;
    u_int32_t        keyLength   = 0;

    IfTrue(pHashMap,  ERR, "Null argument");
    pElement = ALLOCATE_1(hashEntry_t);
    IfTrue(pElement, WARN, "Error allocating memory");

    keyLength = strlen(key);
    hashValue = hashcode(pHashMap, key, keyLength);
    bucket    = bucketOffset(pHashMap, hashValue);

    pElement->value     = value;
    pElement->hashCode  = hashValue;
    pElement->keyLength = keyLength;
    pElement->key       = strdup(key);
    //TODO : check retturn value of strdup

    pElement->pMapNext = pHashMap->pBuckets[bucket];
    pHashMap->pBuckets[bucket] = pElement;
    pHashMap->count++;

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
    goto OnSuccess;
OnError:
    if (!returnValue) {
        returnValue = -1;
    }
OnSuccess:
    return returnValue;
}


void* mapGetElement(map_t hashMap, char* key) {
    hashMapImpl_t* pHashMap  = HASHMAPIMPL(hashMap);
    hashEntry_t*   pElement  = 0;
    hashEntry_t*   pPrev     = 0;
    u_int32_t      hashValue = 0;
    u_int32_t      bucket    = 0;
    u_int32_t      keyLength = 0;

    IfTrue(pHashMap, ERR, "Null argument");
    IfTrue(key, INFO, "Invalid argument");

    keyLength = strlen(key);
    hashValue = hashcode(pHashMap, key, keyLength);
    bucket    = bucketOffset(pHashMap, hashValue);
    pElement  = pHashMap->pBuckets[bucket];

    while (pElement) {
        if ((pElement->hashCode == hashValue) &&
        	(pElement->keyLength == keyLength) &&
            (0 == memcmp(pElement->key, key, keyLength))) {
                /* match found */
             break;
        }
        pPrev    = pElement;
        pElement = pElement->pMapNext;
    }
    goto OnSuccess;
OnError:
    LOG(INFO, "Error looking for key %p", key);
OnSuccess:
    return pElement ? pElement->value : 0;
}

int mapDeleteElement(map_t hashMap, char* key) {
    hashMapImpl_t* pHashMap  = HASHMAPIMPL(hashMap);
    hashEntry_t*   pElement  = 0;
    hashEntry_t*   pPrev     = 0;
    u_int32_t      hashValue = 0;
    u_int32_t      bucket    = 0;
    u_int32_t      keyLength = 0;

    IfTrue(pHashMap, ERR,  "Null argument");
    IfTrue(key, INFO, "Invalid argument");

    keyLength = strlen(key);
    hashValue = hashcode(pHashMap, key, keyLength);
    bucket    = bucketOffset(pHashMap, hashValue);
    pElement  = pHashMap->pBuckets[bucket];

    while (pElement) {
        if ((pElement->hashCode == hashValue) &&
        	(pElement->keyLength == keyLength) &&
            (0 == memcmp(pElement->key, key, keyLength))) {
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
        pHashMap->count--;
        FREE(pElement->key);
        FREE(pElement);
    }
    goto OnSuccess;
OnError:
    LOG(INFO, "Error looking for key %p", key);
OnSuccess:
    return 0;
}
