#include "fallocator.h"

/*
 * Fallocator is again a simple allocator which is used to manage temporary memory.
 * It does a 4KB alloc using malloc and returns memory by simply incrementing the
 * used pointer by whatever size is required. For memory bigger than 4KB, it
 * depends on malloc. If the new allocation cannot be fulfilled from the
 * current buffer, new buffer is allocated.
 *
 * None of the memory allocated by fallocator is reused. Calling free simply
 * decrements refcount on the parent buffer and when refcount becomes 0, the
 * buffer is freed in one shot.
 *
 * All buffers used by fallocator are tracked. So even if caller forgets to
 * free memory, it is finally freed when the fallocator is destroyed.
 *
 * It is not good to use fallocator for allocating memory which will be retained
 * for long time as this will just increase memory pressure on the system.
 *
 * We use it at two places.
 * 1) A single fallocator for lua scripts as they only use memory temporarily.
 *    Once the script is executed, all used memory will be freed by lua GC.
 * 2) For the connection buffer and subsequent parsing of the request,
 *    generating response, etc. Again this memory is usually released very fast,
 *    and any pending stuff is cleared when connection is closed.
 *
 * Instead of bothering malloc again and again, we also cache the fallocator
 * buffer (only 4KB buffers). The default size of cache is 16MB but can be
 * increased/decreased as required via command line parameter -i.
 */


/* 8 bytes are for the malloc header so that the whole thing is in 1 page */


#define DEFAULT_BUFFER_SIZE  (4096 -(8+sizeof(memoryBuffer_t)))  

typedef struct memoryBuffer_t {
	struct memoryBuffer_t* pNext; 
	struct memoryBuffer_t* pPrev;			
	u_int32_t              size;
	u_int32_t              used;
    u_int32_t              refCount;     
	char                   data[0];	
} memoryBuffer_t;


//TODO : not good...globals are bad
static int             MAX_BUFFER_COUNT = 4096;
static int             freeCount        = 0;
static memoryBuffer_t* pGlobalFreeList  = 0;

/*  Every pointer that we give to the user has a 8/4 bytes overhead to keep track of the 
 *  memoryBuffer_t it belongs to. We can optimize on this is many ways (make this zero)
 *  but this simple approach makes sure that we don't need to make any assumptions about 
 *  the address we are working with.
 */
typedef struct memoryPointer_t {
	memoryBuffer_t*        pBuffer;
	char                   data[0];	
} memoryPointer_t;

typedef struct fallocatorImpl_t {
	memoryBuffer_t*        pDefaultBuffers;
	memoryBuffer_t*        pLargeBuffers;    
    u_int32_t              allocCount;
    u_int32_t              freeCount;    
    u_int32_t              allocSize;        
} fallocatorImpl_t;


#define FALLOCATOR(x) ((fallocatorImpl_t*)(x))


static memoryBuffer_t* allocateMemoryBuffer(u_int32_t size) {
	memoryBuffer_t* pBuffer  = 0;
	if ((size == DEFAULT_BUFFER_SIZE) && freeCount) {
		pBuffer = pGlobalFreeList;
		pGlobalFreeList = pGlobalFreeList->pNext;
		if (pGlobalFreeList) {
			pGlobalFreeList->pPrev = 0;
		}
		freeCount--;
		memset(pBuffer, 0, size + sizeof(memoryBuffer_t));
		pBuffer->size = size;
	}else {
		pBuffer = (memoryBuffer_t*)malloc(size + sizeof(memoryBuffer_t));
		if (pBuffer) {
			memset(pBuffer, 0, size + sizeof(memoryBuffer_t));
			pBuffer->size = size;
		}
	}
	return pBuffer;
}

static void freeMemoryBuffer(memoryBuffer_t* pBuffer) {
	if (pBuffer) {
		if ((pBuffer->size == DEFAULT_BUFFER_SIZE) && (freeCount < MAX_BUFFER_COUNT)) {
			memset(pBuffer, 0, DEFAULT_BUFFER_SIZE + sizeof(memoryBuffer_t));
			pBuffer->pNext = pGlobalFreeList;
			pBuffer->pPrev = 0;
			if (pGlobalFreeList) {
				pGlobalFreeList->pPrev = pBuffer;
			}
			pGlobalFreeList = pBuffer;
			freeCount++;
			pBuffer = 0;
		}else {
			free(pBuffer);
		}
	}		
}

/* calls free on all the pointers in the memoryBuffer list */
static int freeBufferList(memoryBuffer_t* pBufferList) {
	memoryBuffer_t* pNext = 0;
	int             count = 0;
	
	while(pBufferList) {		
		pNext = pBufferList->pNext;
		freeMemoryBuffer(pBufferList);
		pBufferList = pNext;
		count++;
	}
	return count;
}


static void linkBuffer(memoryBuffer_t* pBuffer, memoryBuffer_t** ppHead) {
	if (!(*ppHead)) {
		*ppHead = pBuffer;
		pBuffer->pNext = 0;
		pBuffer->pPrev = 0;
	}else {
		pBuffer->pNext = *ppHead;
		(*ppHead)->pPrev = pBuffer;			
		*ppHead = pBuffer;
	}	
}

static void delinkBuffer(memoryBuffer_t* pBuffer, memoryBuffer_t** ppHead) {
	if (pBuffer->pNext) {
		if (pBuffer->pPrev) {
			pBuffer->pPrev->pNext = pBuffer->pNext;
			pBuffer->pNext->pPrev = pBuffer->pPrev;
		}else {
			pBuffer->pPrev->pNext = 0;
		}
	}else {
		if (pBuffer->pPrev) {
			pBuffer->pPrev->pNext = 0;
		}else {
			*ppHead = 0;
		}
	}	
	pBuffer->pNext = 0;
	pBuffer->pPrev = 0;
}

static void* getPointerFromBuffer(memoryBuffer_t* pBuffer, u_int32_t alignedSize) {
	memoryPointer_t* pMP = (memoryPointer_t*)(pBuffer->data + pBuffer->used);
	pMP->pBuffer         = pBuffer;
	pBuffer->refCount++;
	pBuffer->used+= alignedSize;
	return 	pMP->data;
}


////////////////////////////////////////////////////////////////////////////////////////////////////

fallocator_t fallocatorCreate(void) {
	fallocatorImpl_t*  pPool = (fallocatorImpl_t*)malloc(sizeof(fallocatorImpl_t));
	IfTrue(pPool, ERR, "Out of memory");
	memset(pPool, 0, sizeof(fallocatorImpl_t));
	goto OnSuccess;
OnError:	
	if (pPool) {
		fallocatorDelete(pPool);
		pPool = NULL;
	}
OnSuccess:
	return pPool;
}

void fallocatorDelete(fallocator_t fallocator) {
	fallocatorImpl_t*  pPool = FALLOCATOR(fallocator);
	if (pPool) {
		int  bufferCount = 0, largeBufferCount = 0;
		bufferCount       = freeBufferList(pPool->pDefaultBuffers);
		largeBufferCount  = freeBufferList(pPool->pLargeBuffers);
		
		LOG(DEBUG, "Deleting pool %p with bufferCount %d largeBufferCount %d allocCount %d freeCount %d allocSize %d",
				pPool, bufferCount, largeBufferCount, pPool->allocCount, pPool->freeCount, pPool->allocSize);
		free(pPool);
		pPool = 0;
	}
}


void* fallocatorMalloc(fallocator_t fallocator, u_int32_t size) {
	fallocatorImpl_t* pPool       = FALLOCATOR(fallocator);
	void*             pointer     = 0;
	u_int32_t         alignedSize = (size + sizeof(void*)+ 7) & (~0x07);
	
	IfTrue(pPool, ERR, "Pool is NULL");
	if (alignedSize > DEFAULT_BUFFER_SIZE) {
		memoryBuffer_t* pBuffer = allocateMemoryBuffer(alignedSize);	
		IfTrue(pBuffer, WARN, "Error allocating memory");
		linkBuffer(pBuffer, &pPool->pLargeBuffers);
		pointer = getPointerFromBuffer(pBuffer, alignedSize);
		pPool->allocCount++;
		pPool->allocSize+=alignedSize;
	}else {
		memoryBuffer_t* pBuffer = 0;
		if (!pPool->pDefaultBuffers) {
			/* NO buffer - allocate one */
			pBuffer = allocateMemoryBuffer(DEFAULT_BUFFER_SIZE);
			IfTrue(pBuffer, WARN, "Out of memory");
			linkBuffer(pBuffer, &pPool->pDefaultBuffers);			
		}else {
			pBuffer = pPool->pDefaultBuffers;
		}		
		if (alignedSize <= (pBuffer->size - pBuffer->used)) {
			pointer = getPointerFromBuffer(pBuffer, alignedSize);			
			pPool->allocCount++;
			pPool->allocSize+=alignedSize;
		}else {			
			/* current buffer cannot handle the new request, we need to add another one */
			memoryBuffer_t* pNew = allocateMemoryBuffer(DEFAULT_BUFFER_SIZE);
			IfTrue(pNew, WARN, "Out of memory");
			linkBuffer(pNew, &pPool->pDefaultBuffers);			
			pointer = getPointerFromBuffer(pNew, alignedSize);
			pPool->allocCount++;
			pPool->allocSize+=alignedSize;
		}
	}	
	goto OnSuccess;
OnError:
	pointer = 0;
OnSuccess:
	return pointer;	
}

/* Free a pointer returned by fallocatorMalloc once it is no longer needed.
 * We have two choices here. One is to use this as a NO-OP. Life is simple in 
 * this case, but we might run into issues where memory is being alloced and 
 * freed in a tight loop. 
 */

void fallocatorFree(fallocator_t fallocator, void* pointer) {
	fallocatorImpl_t* pPool   = FALLOCATOR(fallocator);
	memoryPointer_t*  pMP     = (memoryPointer_t*)(pointer - sizeof(void*));
	memoryBuffer_t*   pBuffer = pMP->pBuffer;
	
	if (!pBuffer) {
		LOG(WARN, "double free detected ? pointer %p pool %p stats  allocCount %d freeCount %d allocSize %d",
				pointer, pPool, pPool->allocCount, pPool->freeCount, pPool->allocSize);			
		return;
	}	
	if (pBuffer->size > DEFAULT_BUFFER_SIZE) {
		delinkBuffer(pBuffer, &pPool->pLargeBuffers);							
		freeMemoryBuffer(pBuffer);
		pPool->freeCount++;
	}else {
		/* This is a pointer from a normal default buffers */
		pMP->pBuffer = 0; //set to null or track double free 
		pBuffer->refCount--;		
		pPool->freeCount++;
		
		if (pBuffer->refCount == 0) {
			if (pBuffer == pPool->pDefaultBuffers) {
				pBuffer->used = 0;
				memset(pBuffer->data, 0, pBuffer->size);
				/* this makes sure that the next set of allocations go fine*/
			}else {				
				delinkBuffer(pBuffer, &pPool->pDefaultBuffers);
				freeMemoryBuffer(pBuffer);
			}
		}
	}
}

void* fallocatorRealloc(fallocator_t fallocator, void* pointer, u_int32_t osize, u_int32_t nsize) {
	fallocatorImpl_t* pPool   = FALLOCATOR(fallocator);
	void*             newPointer = 0;

	newPointer = fallocatorMalloc(pPool, nsize);
	if (newPointer) {
		int size = osize > nsize ? nsize : osize;
		memcpy(newPointer, pointer, size);
		fallocatorFree(pPool, pointer);
		return newPointer;
	}
	return NULL;
}


void fallocatorInit(u_int32_t bufferCount) {
	MAX_BUFFER_COUNT = bufferCount;
}

