#include "chunkpool.h"
#include "../common/skiplist.h"
#include <time.h>

/*
 * A simple malloc implementation.
 * The twist is that this malloc doesn't allocate more than 4092 bytes.
 * If the item is bigger than 4092 bytes, multiple buffers can be used.
 * Apart from malloc, this implementation also supports what I call
 * relaxed malloc. In case we don't have buffer of a given size, we
 * return a smaller buffer. It is expected that the caller can use
 * multiple smaller buffers to the same effect at a bit higher cost
 * of buffer management.
 *
 * We have 256 buckets starting from 16 bytes to 4096 bytes.
 * Initially all the memory is given to the 4096 size buckets.
 * If the required sized bucket is empty, we pick up a buffer
 * from next available bucket and split the buffer into two.
 *
 * 4 bytes of every allocation are used for management. Thus we only
 * know the size of the current and next allocation and not the
 * previous one. Hence the use of GC function which is periodically
 * called to merge smaller buffers to create larger buffers. This is
 * also done at free time, but as discussed above doesn't work in
 * all the cases.
 */

typedef struct slabEntry_t {
    u_int16_t  slabID;           /* actually we only need 2 bytes here, but that will */
    u_int16_t  inUse;            /* mis-align the pointer. keeping it 4 bytes aligned */
    char       data[0];
}slabEntry_t;


typedef struct slabFreeEntry_t {     /* this is 12 bytes on 32bit and on 64bit */
    u_int16_t       slabID;
    u_int16_t       inUse;
    u_int32_t       nextOffset;
    u_int32_t       prevOffset;
}slabFreeEntry_t;

typedef struct slab_t {
    u_int32_t        freeCount;
    u_int32_t        slabSize;
    u_int32_t        nextFreeOffset;
}slab_t;

typedef struct chunkpoolImpl_t {
    void*              startAddress;
    u_int32_t          pageCount;
    u_int32_t          slabsCount;
    u_int32_t          gcIndex;
    u_int64_t          freeMemory;
    u_int64_t          freeChunks;
    skipList_t         slabList;
    slab_t             slabs[0];
}chunkpoolImpl_t;

#define AS_CHUNKPOOL(x) ((chunkpoolImpl_t*)(x))

#define PAGE_SIZE       (4*1024)           /*  4 KB */
#define MAX_SLAB_SIZE   (PAGE_SIZE - (sizeof(slabFreeEntry_t)))
#define MIN_SLAB_SIZE   (16 - (sizeof(slabFreeEntry_t)))
#define SLAB_MAX        (MAX_SLAB_SIZE/16)
#define SLAB_USE_HIGH   (64 * 1024)
#define SLAB_USE_LOW    (16 * 1024)
#define SLAB_GC_INLINE  (16)
#define GC_PAGE_COUNT   ((8 * 1024 * 1024)/PAGE_SIZE)          //GC 8MB worth of memory at a time

static slabFreeEntry_t* OFFSET2POINTER(chunkpoolImpl_t* pPool, u_int32_t offset) {
	u_int64_t newOffset = offset;
	u_int64_t value = (u_int64_t)pPool->startAddress;
			  value += newOffset << 4;

	return (slabFreeEntry_t*)value;
}

static u_int32_t POINTER2OFFSET(chunkpoolImpl_t* pPool, slabFreeEntry_t* pointer) {
	u_int64_t value  = (u_int64_t)pPool->startAddress;
	u_int64_t pvalue = (u_int64_t)pointer;
	u_int64_t offset = ((pvalue - value) >> 4) ;
	return (u_int32_t)(offset);
}

chunkpool_t chunkpoolCreate(u_int32_t maxSizeInPages) {
    chunkpoolImpl_t* pPool    = 0;
    int              err      = 0;
    int              index    = 0;
    int              slabSize = 0;
    pPool = (chunkpoolImpl_t*)malloc(sizeof(chunkpoolImpl_t)+(SLAB_MAX+1)*sizeof(slab_t));
    IfTrue(pPool, ERR, "Error allocating memory");
    memset(pPool, 0, sizeof(chunkpoolImpl_t)+SLAB_MAX*sizeof(slab_t));
    pPool->pageCount  = maxSizeInPages -1;
    pPool->slabsCount = SLAB_MAX;
    pPool->gcIndex    = 1;
    pPool->freeMemory = PAGE_SIZE * pPool->pageCount;
    pPool->freeChunks = pPool->pageCount;
    pPool->slabList   = skipListCreate();
    IfTrue(pPool->slabList, ERR, "Error creating skip list");

    err = posix_memalign(&(pPool->startAddress), PAGE_SIZE, PAGE_SIZE * maxSizeInPages);
    IfTrue(err == 0, ERR, "posix_memalign failed err %d for memory size %d", err, PAGE_SIZE * maxSizeInPages);
    /* Cool - we got all the memory we asked for*/
    for (index = 0; index <= SLAB_MAX; index++) {
        slabSize+= 16;
        pPool->slabs[index].slabSize  = slabSize - sizeof(slabEntry_t);
    }
    /* we will now assign all the allocated memory to the last slab
     * We can postponse this step to add buffers to slab if we want
     * to reduce the upfront memory usage. linux commits memory only
     * on access*/
    pPool->slabs[SLAB_MAX].freeCount = pPool->pageCount;
    slabFreeEntry_t* pFree = 0;
    /* We don't use the first page */
    for (index = 1; index < pPool->pageCount; index++) {
        pFree = (slabFreeEntry_t*)((char*)pPool->startAddress+index*PAGE_SIZE);
        pFree->slabID = SLAB_MAX;
        pFree->inUse  = 0;
        pFree->nextOffset = ((index+1) * PAGE_SIZE) >> 4;
        pFree->prevOffset = ((index-1) * PAGE_SIZE) >> 4;
    }
    pFree->nextOffset = 0;
    OFFSET2POINTER(pPool, PAGE_SIZE)->prevOffset = 0;
    pPool->slabs[SLAB_MAX].nextFreeOffset = PAGE_SIZE >> 4;
    skipListInsertSlab(pPool->slabList, SLAB_MAX);
    goto OnSuccess;
OnError:
    if (pPool) {
        chunkpoolDelete(pPool);
        pPool = 0;
    }
OnSuccess:
    return pPool;
}

static void crashNow() {
    char* a = 0;
    *a      = 1;
}

static void validateChunk(chunkpoolImpl_t* pPool, void* chunk) {
	//we don't use the first page
    unsigned long start = (unsigned long)pPool->startAddress + PAGE_SIZE;
    unsigned long last  = start + pPool->pageCount * PAGE_SIZE;
    unsigned long value = (unsigned long)chunk;
    if (!(value <= last && value >= start)) {
        crashNow();
    }
    slabEntry_t* pChunk = (slabEntry_t*)((char*)chunk - sizeof(slabEntry_t));
    if (!(pChunk->slabID <= pPool->slabsCount)) {
          crashNow();
    }
}


void chunkpoolDelete(chunkpool_t chunkpool) {
    chunkpoolImpl_t* pPool = AS_CHUNKPOOL(chunkpool);
    if (pPool) {
        //chunkpoolPrint(pPool);
        if (pPool->startAddress) {
            FREE(pPool->startAddress);
        }
        if (pPool->slabList) {
        	skipListDelete(pPool->slabList);
        }
        FREE(pPool);
    }
}

u_int32_t chunkpoolMaxMallocSize(chunkpool_t chunkpool) {
    chunkpoolImpl_t* pPool         = AS_CHUNKPOOL(chunkpool);
    u_int32_t        maxMallocSize = 0;
    if (pPool) {
        maxMallocSize =  pPool->slabs[SLAB_MAX].slabSize;
    }
    return maxMallocSize;
}

static void  unlinkFromFreeList(chunkpoolImpl_t* pPool, slabFreeEntry_t* pEntry) {
    if (pPool->slabs[pEntry->slabID].freeCount == 0) {
        crashNow();
    }
    if (pEntry->nextOffset) {
        if (pEntry->prevOffset) {
        	OFFSET2POINTER(pPool, pEntry->prevOffset)->nextOffset = pEntry->nextOffset;
            OFFSET2POINTER(pPool, pEntry->nextOffset)->prevOffset = pEntry->prevOffset;
        }else {
            pPool->slabs[pEntry->slabID].nextFreeOffset = pEntry->nextOffset;
            OFFSET2POINTER(pPool, pPool->slabs[pEntry->slabID].nextFreeOffset)->prevOffset = 0;
        }
    }else {
        if (pEntry->prevOffset) {
            OFFSET2POINTER(pPool, pEntry->prevOffset)->nextOffset = 0;
        }else {
            pPool->slabs[pEntry->slabID].nextFreeOffset = 0;
        }
    }
    pEntry->nextOffset = 0;
    pEntry->prevOffset = 0;
    pPool->slabs[pEntry->slabID].freeCount--;
    if (pPool->slabs[pEntry->slabID].freeCount == 0) {
    	skipListDeleteSlab(pPool->slabList, pEntry->slabID);
    }
	pPool->freeMemory -= pPool->slabs[pEntry->slabID].slabSize + sizeof(slabEntry_t);
	pPool->freeChunks--;
}


static void* allocFromFreeList(chunkpoolImpl_t* pPool, u_int16_t slabIndex) {
	if (pPool->slabs[slabIndex].nextFreeOffset) {
		slabFreeEntry_t* pFree = OFFSET2POINTER(pPool, pPool->slabs[slabIndex].nextFreeOffset);
		unlinkFromFreeList(pPool, pFree);
		pFree->inUse  = 1;
		pFree->slabID = slabIndex;
		return ((char*)pFree+sizeof(slabEntry_t));
	}
	return 0;
}

static void putInFreeList(chunkpoolImpl_t* pPool, void* pointer) {
    slabFreeEntry_t* pUsed = 0;
    if (!pointer) return;

    pUsed = (slabFreeEntry_t*)((char*)pointer - sizeof(slabEntry_t));
    pUsed->inUse = 0;
    /* Now add the chunk in its slab's free list */
    pUsed->nextOffset = pPool->slabs[pUsed->slabID].nextFreeOffset;
    pUsed->prevOffset = 0;

    if (pPool->slabs[pUsed->slabID].nextFreeOffset) {
    	OFFSET2POINTER(pPool, pPool->slabs[pUsed->slabID].nextFreeOffset)->prevOffset = POINTER2OFFSET(pPool, pUsed);
    }
    pPool->slabs[pUsed->slabID].nextFreeOffset = POINTER2OFFSET(pPool, pUsed);
    pPool->slabs[pUsed->slabID].freeCount++;
    if (pPool->slabs[pUsed->slabID].freeCount == 1) {
    	skipListInsertSlab(pPool->slabList, pUsed->slabID);
    }
    pPool->freeMemory += pPool->slabs[pUsed->slabID].slabSize + sizeof(slabEntry_t);
    pPool->freeChunks++;
}



/* No free entry in the slab - find a free entry in the bigger slab and split
 * it into two parts. Return the first part to the caller and the next part
 * to the corresponding slab */
static void* allocFromBigSlab(chunkpoolImpl_t* pPool, u_int16_t slabIndex) {
    slabFreeEntry_t* originalPointer = 0;
    slabFreeEntry_t* leftOverPointer = 0;
    u_int16_t        bigSlabIndex    = 0;
    void*            pointer         = 0;

    if (slabIndex >= SLAB_MAX) {
    	return pointer;
    }

    bigSlabIndex = skipListFindNextSlab(pPool->slabList, slabIndex);
	if (pPool->slabs[bigSlabIndex].freeCount) {
		pointer  = allocFromFreeList(pPool, bigSlabIndex);
		if (!pointer) return 0;
		/* since we got a bigger buffer, will put the "rest" of the memory in
		 * corresponding slab's free list */
		originalPointer = (slabFreeEntry_t*)((char*)pointer - sizeof(slabEntry_t));
		leftOverPointer = (slabFreeEntry_t*)((char*)pointer +  pPool->slabs[slabIndex].slabSize);
		originalPointer->slabID = slabIndex;
		leftOverPointer->slabID = bigSlabIndex - (slabIndex+1);
		leftOverPointer->inUse  = 0;
		putInFreeList(pPool, (char*)leftOverPointer+sizeof(slabEntry_t));
		return pointer;
	}
    return pointer;
}

/* No free entry in the slab - find a free entry in the smaller slab ..
 * this is called for chunkpoolRelaxedMalloc*/
static void* allocFromSmallerSlab(chunkpoolImpl_t* pPool, u_int16_t slabIndex) {
    u_int16_t        smallSlabIndex = 0;
    void*            pointer        = 0;

    if (slabIndex == 0) {
    	return pointer;
    }
    smallSlabIndex = skipListFindPrevSlab(pPool->slabList, slabIndex);

	if (pPool->slabs[smallSlabIndex].freeCount) {
		pointer  = allocFromFreeList(pPool, smallSlabIndex);
	}
    return pointer;
}

/* - should be part of the same PAGE
 * - adjacent and free */
static int isBuddyMergable(slabEntry_t* buddy) {
    unsigned long buddyAsLong = (unsigned long)buddy;
    if (buddyAsLong % PAGE_SIZE == 0 || buddy->inUse) {
        return 0;
    }
    return 1;
}

/*
 * pFirst is in some free list
 */
static int mergeBuddyChunks(chunkpoolImpl_t* pPool, slabEntry_t* pFirst) {
    slabEntry_t* pBuddy = (slabEntry_t*)((char*)pFirst+(pPool->slabs[pFirst->slabID].slabSize+sizeof(slabEntry_t)));
    validateChunk(pPool, (char*)pFirst+sizeof(slabEntry_t));
    if (isBuddyMergable(pBuddy)) {
        /* remove buddy from the free list*/
        slabFreeEntry_t* pFreeBuddy = (slabFreeEntry_t*)pBuddy;
        unlinkFromFreeList(pPool, pFreeBuddy);
        unlinkFromFreeList(pPool, (slabFreeEntry_t*)pFirst);
        /* after merge the slabID of the combined entity is changed */
        pFirst->slabID += pBuddy->slabID+1;
        putInFreeList(pPool, (char*)pFirst+sizeof(slabEntry_t));
        return pFirst->slabID;
    }
    return -1;
}

/* Goes over each chunk in the PAGE, merging adjacent chunks if both
 * are free.
 */
static void mergePage(chunkpoolImpl_t* pPool, u_int32_t pageID) {
    slabFreeEntry_t* pCurrent = (slabFreeEntry_t*)((char*)(pPool->startAddress)+(pageID*PAGE_SIZE));
    slabFreeEntry_t* pStart   = pCurrent;
    u_int32_t offset = 0;
    while (offset < (PAGE_SIZE-16)) {
        if (!pCurrent->inUse) {
            int result = mergeBuddyChunks(pPool, (slabEntry_t*)pCurrent);
            if (result == -1) {
                offset  +=  pPool->slabs[pCurrent->slabID].slabSize+sizeof(slabEntry_t);
                pCurrent = (slabFreeEntry_t*)((char*)pStart+offset);
            }
        }else {
            offset  +=  pPool->slabs[pCurrent->slabID].slabSize+sizeof(slabEntry_t);
            pCurrent = (slabFreeEntry_t*)((char*)pStart+offset);
        }
    }
}

/* calls mergePage for all pages
 */

void  chunkpoolGC(chunkpool_t  chunkpool){
	chunkpoolImpl_t* pPool = AS_CHUNKPOOL(chunkpool);
	/*
	 * GC only makes sense when we have enough freeMemory
	 * and when that memory is fragmented.
	 */

	if (pPool->freeMemory > ((pPool->pageCount * PAGE_SIZE) >> 3 )) {
		//at least 1/8th of memory is free
		if ((pPool->freeMemory/pPool->freeChunks) < (PAGE_SIZE >> 4)) {
			//average size of free chunks is less than 256 bytes
			u_int64_t  chunks = pPool->freeChunks;
			int start   = pPool->gcIndex;
			int end     = pPool->gcIndex + GC_PAGE_COUNT;
			int current = 0;

			if (end >= pPool->pageCount) {
				end = pPool->pageCount;
			}

			struct timespec ts, te;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			for (current = start; current < end; current++) {
				mergePage(pPool, current);
			}
			if (end >= pPool->pageCount) {
				pPool->gcIndex = 1;
			}else {
				pPool->gcIndex = end;
			}

			clock_gettime(CLOCK_MONOTONIC, &te);
			u_int64_t ms = ts.tv_sec*1000+ts.tv_nsec/1000000;
			u_int64_t me = te.tv_sec*1000+te.tv_nsec/1000000;
			LOG(INFO, "Time for GC millis %lu merged chunks %lu \n", (me - ms), (chunks - pPool->freeChunks));
		}
	}
}


void* chunkpoolMalloc(chunkpool_t chunkpool, u_int32_t size) {
    chunkpoolImpl_t* pPool     = AS_CHUNKPOOL(chunkpool);
    void*            pointer   = 0;
    u_int16_t        slabIndex = 0;
    u_int32_t        retrying  = 1;

    IfTrue(pPool, ERR, "Null pool");
    IfTrue(size > 0 && size <= chunkpoolMaxMallocSize(pPool),
        INFO, "Invalid size %d max allowed %d", size, chunkpoolMaxMallocSize(pPool));
Retry:

    slabIndex = (size+3) >> 4;
    if (pPool->slabs[slabIndex].nextFreeOffset) {
        /* found entry in the free list for the slab */
        pointer = allocFromFreeList(pPool, slabIndex);
        validateChunk(pPool, pointer);
        goto OnSuccess;
    }
    /* Entry not found in the free list for the slab */
    pointer = allocFromBigSlab(pPool, slabIndex);
    if (pointer) {
        validateChunk(pPool, pointer);
        goto OnSuccess;
    }
    /* Bad things do happen..couldn't find any appropriate buffer */
    if (!retrying) {
        retrying = 1;
        chunkpoolGC(pPool);
        goto Retry;
    }
    goto OnSuccess;
OnError:
    pointer = 0;
OnSuccess:
	if (pointer) {
		memset(pointer, 0, size);
	}
    return pointer;
}


static void slabGC(chunkpool_t chunkpool, void* chunk) {
	chunkpoolImpl_t* pPool    = AS_CHUNKPOOL(chunkpool);
    slabFreeEntry_t* pFree    = (slabFreeEntry_t*)((char*)chunk - sizeof(slabEntry_t));
    u_int32_t        slabID   = pFree->slabID;
    u_int32_t        maxTries = SLAB_GC_INLINE;

    if (slabID == SLAB_MAX) {
    	return;
    }

    if ((pPool->slabs[slabID].freeCount * pPool->slabs[slabID].slabSize) < SLAB_USE_HIGH) {
    	return;
    }

    pFree = OFFSET2POINTER(pPool, pPool->slabs[slabID].nextFreeOffset);
    while (maxTries-- && pFree && (pPool->slabs[slabID].freeCount * pPool->slabs[slabID].slabSize) > SLAB_USE_LOW) {
    	if (-1 == mergeBuddyChunks(pPool, (slabEntry_t*)pFree)) {
    		//unable to merge buddy, move to next one
    		if (pFree->nextOffset) {
    			pFree = OFFSET2POINTER(pPool, pFree->nextOffset);
    		}else {
    			pFree = 0;
    		}
    	}
    }
}

void chunkpoolFree(chunkpool_t chunkpool, void* chunk) {
    chunkpoolImpl_t* pPool  = AS_CHUNKPOOL(chunkpool);
    if (pPool && chunk) {
        validateChunk(pPool, chunk);
        putInFreeList(pPool, chunk);
        slabGC(pPool, chunk);
    }
}

void chunkpoolPrint(chunkpool_t chunkpool) {
    chunkpoolImpl_t* pPool = AS_CHUNKPOOL(chunkpool);
    int i = 0;

    printf("Page Count    %d\n", pPool->pageCount);
    printf("Slab Count    %d\n", pPool->slabsCount);
    printf("Start Address %p\n", pPool->startAddress);
    printf("\n");
    for (i = 0; i <= pPool->slabsCount; i++) {
    	if (pPool->slabs[i].freeCount) {
    		printf("[slab %d] [freecount %d] [memusage %d]\n", pPool->slabs[i].slabSize,
            pPool->slabs[i].freeCount, pPool->slabs[i].slabSize * pPool->slabs[i].freeCount);
    	}
    }
}

/* Not used */
void* chunkpoolRelaxedMalloc(chunkpool_t chunkpool, u_int32_t prefferedSize, u_int32_t *pActualSize) {
    chunkpoolImpl_t* pPool     = AS_CHUNKPOOL(chunkpool);
    void*            pointer   = 0;
    u_int16_t        slabIndex = 0;

    IfTrue(pPool, ERR, "Null pool");
    IfTrue(prefferedSize > 0 , INFO, "Invalid prefferedSize %d ", prefferedSize);
    if (prefferedSize > chunkpoolMaxMallocSize(chunkpool)) {
    	prefferedSize = chunkpoolMaxMallocSize(chunkpool);
    }
    slabIndex = (prefferedSize+3) >> 4;
    if (pPool->slabs[slabIndex].nextFreeOffset) {
        /* lucky.. found entry in the free list for the slab */
        pointer = allocFromFreeList(pPool, slabIndex);
        validateChunk(pPool, pointer);
        *pActualSize = prefferedSize;
        goto OnSuccess;
    }
    /* no entry of preffered size found */
    pointer = allocFromSmallerSlab(pPool, slabIndex);
    if (pointer) {
        validateChunk(pPool, pointer);
        slabFreeEntry_t* pUsed = (slabFreeEntry_t*)((char*)pointer - sizeof(slabEntry_t));
        *pActualSize = pPool->slabs[pUsed->slabID].slabSize;
        goto OnSuccess;
    }
    goto OnSuccess;
OnError:
    pointer = 0;
OnSuccess:
	if (pointer) {
		memset(pointer, 0, *pActualSize);
	}
    return pointer;
}

u_int32_t  chunkpoolMemoryUsed(chunkpool_t chunkpool) {
	chunkpoolImpl_t* pPool = AS_CHUNKPOOL(chunkpool);
	u_int32_t  freeSize = 0;
	for (int i = 0; i <= SLAB_MAX; i++) {
		freeSize += (pPool->slabs[i].slabSize + sizeof(slabEntry_t)) * pPool->slabs[i].freeCount;
	}
	return (pPool->pageCount* PAGE_SIZE) - freeSize;
}

void* chunkpoolRealloc(chunkpool_t chunkpool, void* pointer, u_int32_t newSize) {
	void* newPointer = chunkpoolMalloc(chunkpool, newSize);
	if (newPointer) {
		chunkpoolImpl_t* pPool = AS_CHUNKPOOL(chunkpool);
		if (pointer) {
			slabEntry_t* pEntry = (slabEntry_t*)((char*)pointer - sizeof(slabEntry_t));
			memcpy(newPointer, pointer, pPool->slabs[pEntry->slabID].slabSize);
			chunkpoolFree(chunkpool, pointer);
		}
	}
	return newPointer;
}





