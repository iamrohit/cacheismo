#include "cacheitem.h"

typedef struct {
	u_int32_t       expiryTime;    /* expire time */
	u_int32_t       dataLength;    /* size of data */
	u_int16_t       refcount;
	u_int16_t       keyLength;
	u_int32_t       flags;
	dataStream_t    dataStream;
	char            key[];
} cacheItemImpl_t;

#define CACHE_ITEM(x) (cacheItemImpl_t*)(x)

static u_int32_t calculateRequiredMemory(u_int32_t keyLength) {
	u_int32_t size = sizeof(cacheItemImpl_t) + keyLength + 1 ;
	return size;
}

static u_int32_t currentTimeInSeconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return  (u_int32_t) (ts.tv_sec);
}


cacheItem_t cacheItemCreate(chunkpool_t chunkpool, command_t* pCommand) {
	u_int32_t maxBufferSize  = chunkpoolMaxMallocSize(chunkpool);
	u_int32_t memoryRequired = calculateRequiredMemory(pCommand->keySize);
	cacheItemImpl_t* pItem   = 0;

	IfTrue(memoryRequired <= maxBufferSize, WARN,
			"Too big key size %d", pCommand->keySize);

	pItem = chunkpoolMalloc(chunkpool, memoryRequired);
	IfTrue(pItem, DEBUG, "Error allocating memory");
	pItem->keyLength  = pCommand->keySize;
	pItem->dataLength = pCommand->dataLength;
	if (pCommand->expiryTime) {
		pItem->expiryTime = currentTimeInSeconds() + pCommand->expiryTime;
	}else {
		pItem->expiryTime = UINT32_MAX;
	}
	pItem->flags      = pCommand->flags;
	pItem->refcount   = 1;
	memcpy(pItem->key, pCommand->key, pCommand->keySize);
	pItem->key[pCommand->keySize] = '\0';
	//now copy to data buffer
	pItem->dataStream = dataStreamClone(chunkpool, pCommand->dataStream);
	IfTrue(pItem->dataStream, DEBUG, "Error cloning data stream")

	goto OnSuccess;
OnError:
	if (pItem) {
		chunkpoolFree(chunkpool, pItem);
		pItem = 0;
	}
OnSuccess:
	return pItem;
}

u_int32_t cacheItemEstimateSize(command_t* pCommand) {
	if (pCommand) {
		return sizeof(cacheItemImpl_t) + pCommand->keySize + 1 + pCommand->dataLength ;
	}
	return 0;
}

u_int32_t  cacheItemGetTotalSize(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return sizeof(cacheItemImpl_t) + pItem->keyLength + 1 + dataStreamTotalSize(pItem->dataStream) ;
	}
	return 0;
}

void cacheItemDelete(chunkpool_t chunkpool, cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		pItem->refcount--;
		if (!pItem->refcount) {
			if (pItem->dataStream) {
				dataStreamDelete(pItem->dataStream);
				pItem->dataStream = 0;
			}
			chunkpoolFree(chunkpool, pItem);
		}
	}
}

char* cacheItemGetKey(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->key;
	}
	return 0;
}

u_int32_t cacheItemGetKeyLength(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->keyLength;
	}
	return 0;
}

u_int64_t  cacheItemGetCAS(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return 0; //pItem->casId;
	}
	return 0;
}

u_int32_t  cacheItemGetFlags(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->flags;
	}
	return 0;

}


dataStream_t  cacheItemGetDataStream(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->dataStream;
	}
	return 0;
}

u_int32_t cacheItemGetDataLength(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->dataLength;
	}
	return 0;
}

void cacheItemAddReference(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		pItem->refcount++;
	}
}


u_int32_t  cacheItemGetExpiry(cacheItem_t cacheItem) {
	cacheItemImpl_t* pItem = CACHE_ITEM(cacheItem);
	if (pItem) {
		return pItem->expiryTime;
	}
	return 0;
}


hashEntryAPI_t* cacheItemGetHashEntryAPI(chunkpool_t chunkpool) {
	hashEntryAPI_t* API = ALLOCATE_1(hashEntryAPI_t);
	if (API) {
		API->addReference    = cacheItemAddReference;
		API->getKey          = cacheItemGetKey;
		API->getKeyLength    = cacheItemGetKeyLength;
		API->onObjectDeleted = cacheItemDelete;
		API->getTotalSize    = cacheItemGetTotalSize;
		API->getExpiry       = cacheItemGetExpiry;
		API->context         = chunkpool;
	}
	return API;
}
