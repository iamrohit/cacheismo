#include "datastream.h"

typedef struct _dataVector {
	void*      buffer;
	u_int16_t  length;
	u_int16_t  offset;
} dataVector_t;

/**
 *  TODO - use dataVectorList_t instead of dataVector_t in the
 *         dataStreamImpl_t. This will remove the dependecny on
 *         realloc and also allow creation of messages larger
 *         than 4K * 4K/sizeof(dataVector_t)
 */
typedef struct _dataVectorList_t {
	struct _dataVectorList_t* pNext;
	u_int16_t                 vectorLength;
	u_int16_t                 vectorUsed;
	dataVector_t              vector[];
} dataVectorList_t;

typedef struct {
	dataVector_t* pVector;
	u_int16_t     vectorLength;
	u_int16_t     vectorUsed;
	u_int32_t     size;
	chunkpool_t   chunkpool;
} dataStreamImpl_t;

typedef struct {
	dataVector_t*     pVector;
	u_int32_t         vectorLength;
	u_int32_t         size;
} dataStreamIteratorImpl_t;


#define DATA_STREAM(x) ((dataStreamImpl_t*)(x))

#define MIN_VECTOR_LENGTH 1

typedef struct {
	u_int16_t refcount;
	u_int16_t isChunkpool;
	void*     chunkpool;
	char      dataStream[];
} bufferImpl_t;

void* dataStreamBufferAllocate(chunkpool_t chunkpool, fallocator_t fallocator, u_int32_t size) {
	bufferImpl_t* pBuffer = 0;
	if (chunkpool) {
		pBuffer = chunkpoolMalloc(chunkpool, sizeof(bufferImpl_t)+size);
	}else {
		pBuffer = fallocatorMalloc(fallocator, (sizeof(bufferImpl_t)+size));
		//pBuffer = malloc(sizeof(bufferImpl_t)+size);
	}
	if (pBuffer) {
		pBuffer->refcount = 1;
		if (chunkpool) {
			pBuffer->chunkpool   = chunkpool;
			pBuffer->isChunkpool = 1;
		}else {
			pBuffer->chunkpool   = fallocator;
			pBuffer->isChunkpool = 0;

		}
		return ((char*)pBuffer)+sizeof(bufferImpl_t);
	}

	return 0;
}

static void* dataStreamBufferAllocateRelaxed(chunkpool_t chunkpool, u_int32_t size, u_int32_t* actualSize) {
	bufferImpl_t* pBuffer = 0;
	if (chunkpool) {
		pBuffer = chunkpoolMalloc(chunkpool, sizeof(bufferImpl_t)+size);
		if (pBuffer) {
			pBuffer->refcount    = 1;
			pBuffer->isChunkpool = 1;
			pBuffer->chunkpool   = chunkpool;
			*actualSize          = size;
			return ((char*)pBuffer)+sizeof(bufferImpl_t);
		}else {
			//error allocating buffer of required size..lets see what we got
			pBuffer = chunkpoolRelaxedMalloc(chunkpool, (size + sizeof(bufferImpl_t)), actualSize);
			if (pBuffer) {
				if (*actualSize > (2 * sizeof(bufferImpl_t))) {
					pBuffer->refcount = 1;
					pBuffer->chunkpool = chunkpool;
					pBuffer->isChunkpool = 1;
					*actualSize -= sizeof(bufferImpl_t);
					return ((char*)pBuffer)+sizeof(bufferImpl_t);
				}else {
					chunkpoolFree(chunkpool, pBuffer);
					pBuffer = 0;
				}
			}
			*actualSize = 0;
			return 0;
		}
	}
	return 0;
}


void dataStreamBufferFree(void* buffer) {
	bufferImpl_t* pBuffer = (bufferImpl_t*)((char*)buffer - sizeof(bufferImpl_t));
	if (pBuffer) {
		pBuffer->refcount--;
		if (pBuffer->refcount == 0) {
			if (pBuffer->isChunkpool) {
				chunkpoolFree(pBuffer->chunkpool, pBuffer);
			}else {
				fallocatorFree(pBuffer->chunkpool, pBuffer);
			}
		}
	}
}

static void dataStreamBufferIncrementRefCount(void* buffer) {
	if (buffer) {
		bufferImpl_t* pBuffer = (bufferImpl_t*)((char*)buffer - sizeof(bufferImpl_t));
		if (pBuffer->refcount > 0) {
			pBuffer->refcount++;
		}else {
			 char* a = 0;
			 *a      = 1;
		}
	}
}

void dataStreamBufferPrint(void* buffer) {
	bufferImpl_t* pBuffer = (bufferImpl_t*)((char*)buffer - sizeof(bufferImpl_t));
	if (pBuffer) {
		//LOG("buffer %p refcount %u\n", buffer, pBuffer->refcount);
	}else {
		//LOG ("NULL buffer");
	}
}

int dataStreamTruncateFromStart(dataStream_t dataStream, u_int32_t finalSize) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	int               returnValue = 0;

	IfTrue(pDataStream, ERR, "Null data buffer passed");
	IfTrue(!pDataStream->chunkpool, ERR, "truncate to chunkpool data stream not allowed");

	if (finalSize >= pDataStream->size) {
		LOG(INFO, "No need to truncate. finalsize %u is greater than buffer size %u", finalSize, pDataStream->size);
		goto OnSuccess;
	}
	// data buffer size is greater than finalSize
	u_int32_t bytesToDelete  = pDataStream->size - finalSize;
	u_int32_t vectorsDeleted = 0;

	for (int i = 0; i < pDataStream->vectorUsed; i++) {
		u_int32_t currentBufferLength = pDataStream->pVector[i].length;
		if (bytesToDelete > currentBufferLength) {
			//delete the complete buffer
			bytesToDelete -= currentBufferLength;
			dataStreamBufferFree(pDataStream->pVector[i].buffer);
			pDataStream->pVector[i].buffer = 0;
			pDataStream->pVector[i].offset = 0;
			pDataStream->pVector[i].length = 0;
			vectorsDeleted++;
		}else {
			// adjust the partial buffer
			pDataStream->pVector[i].offset+= bytesToDelete;
			pDataStream->pVector[i].length-= bytesToDelete;
			bytesToDelete = 0;
			if (pDataStream->pVector[i].length == 0) {
				dataStreamBufferFree(pDataStream->pVector[i].buffer);
				pDataStream->pVector[i].buffer = 0;
				vectorsDeleted++;
			}
		}
		if (!bytesToDelete) {
			break;
		}
	}
	//shift the vectors to the start
	if (vectorsDeleted) {
		memmove(pDataStream->pVector, pDataStream->pVector+vectorsDeleted,
				(pDataStream->vectorUsed-vectorsDeleted) * (sizeof(dataVector_t)));
		pDataStream->vectorUsed -= vectorsDeleted;
		memset(pDataStream->pVector+pDataStream->vectorUsed, 0,
			(pDataStream->vectorLength - pDataStream->vectorUsed) * (sizeof(dataVector_t)));
	}

	pDataStream->size = finalSize;
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}

int  dataStreamTruncateFromEnd(dataStream_t dataStream, u_int32_t finalSize) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	int               returnValue = 0;

	IfTrue(pDataStream, ERR, "Null data buffer passed");
	IfTrue(!pDataStream->chunkpool, ERR, "truncate to chunkpool data stream not allowed");

	if (finalSize >= pDataStream->size) {
		LOG(INFO, "No need to truncate. finalsize %u is greater than buffer size %u", finalSize, pDataStream->size);
		goto OnSuccess;
	}
	// data buffer size is greater than finalSize
	u_int32_t bytesToDelete = pDataStream->size - finalSize;
	u_int32_t vectorsDeleted = 0;

	for (int i = pDataStream->vectorUsed-1; i >= 0; i--) {
		u_int32_t currentBufferLength = pDataStream->pVector[i].length;
		if (bytesToDelete > currentBufferLength) {
			//delete the complete buffer
			bytesToDelete -= currentBufferLength;
			dataStreamBufferFree(pDataStream->pVector[i].buffer);
			pDataStream->pVector[i].buffer = 0;
			pDataStream->pVector[i].offset = 0;
			pDataStream->pVector[i].length = 0;
			vectorsDeleted++;
		}else {
			// adjust the partial buffer
			pDataStream->pVector[i].length-= bytesToDelete;
			bytesToDelete = 0;
			if (pDataStream->pVector[i].length == 0) {
				dataStreamBufferFree(pDataStream->pVector[i].buffer);
				pDataStream->pVector[i].buffer = 0;
				vectorsDeleted++;
			}
		}
	}
	pDataStream->vectorUsed -= vectorsDeleted;
	pDataStream->size        = finalSize;

	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}


dataStream_t dataStreamCreate(void) {
	dataStreamImpl_t* pDataStream = ALLOCATE_1(dataStreamImpl_t);
	if (pDataStream) {
		pDataStream->pVector      = ALLOCATE_N(MIN_VECTOR_LENGTH, dataVector_t);
		pDataStream->vectorLength = MIN_VECTOR_LENGTH;
	}
	return pDataStream;
}


/*
 *
 */
dataStream_t dataStreamClone(chunkpool_t chunkpool, dataStream_t original) {
	dataStreamImpl_t* pOriginal   = DATA_STREAM(original);
	dataStreamImpl_t* pClone      = chunkpoolMalloc(chunkpool, sizeof(dataStreamImpl_t));
	u_int32_t         noOfVectors = (pOriginal->size / (chunkpoolMaxMallocSize(chunkpool) - sizeof(bufferImpl_t))) + 1;
	u_int32_t         bufferSize  = pOriginal->size;
	u_int32_t         totalCopied = 0;
    char*             buffer      = 0;
	u_int32_t         bufferUsed  = 0;
	//int               j           = 0;

	IfTrue(pClone, DEBUG, "Error allocating memory from chunkpool");
	pClone->chunkpool = chunkpool;
	pClone->size      = pOriginal->size;
	pClone->pVector   = chunkpoolMalloc(chunkpool, noOfVectors * sizeof(dataVector_t));

	IfTrue(pClone->pVector, DEBUG, "Error allocating memory from chunkpool");

	pClone->vectorLength = noOfVectors;
	pClone->vectorUsed   = 0;

	//now copy the buffers

    for (int i = 0; i < pOriginal->vectorUsed; i++) {
    	int copied = 0;
    	while (copied < pOriginal->pVector[i].length) {
			if (!buffer) {
				buffer = dataStreamBufferAllocateRelaxed(chunkpool, pOriginal->size - totalCopied, &bufferSize);
				IfTrue(buffer, DEBUG, "Error allocating memory");
				bufferUsed = 0;

			}
			if ((pOriginal->pVector[i].length - copied) <= (bufferSize - bufferUsed)) {
				memcpy(buffer+bufferUsed, (char*)pOriginal->pVector[i].buffer + (pOriginal->pVector[i].offset + copied),
						                 (pOriginal->pVector[i].length - copied));
				bufferUsed  += pOriginal->pVector[i].length - copied;
				totalCopied += pOriginal->pVector[i].length - copied;
				copied      = pOriginal->pVector[i].length;
			}else {
				memcpy(buffer+bufferUsed, (char*)pOriginal->pVector[i].buffer + pOriginal->pVector[i].offset + copied, bufferSize - bufferUsed);
				copied      += (bufferSize - bufferUsed);
				totalCopied += (bufferSize - bufferUsed);
				bufferUsed   = bufferSize;
			}

			if (bufferUsed == bufferSize) {
				pClone->pVector[pClone->vectorUsed].buffer = buffer;
				pClone->pVector[pClone->vectorUsed].offset = 0;
				pClone->pVector[pClone->vectorUsed].length = bufferSize;
				pClone->vectorUsed++;
				buffer = 0;

				if (pClone->vectorUsed == pClone->vectorLength) {
					dataVector_t* newVector =  chunkpoolRealloc(chunkpool, pClone->pVector, pClone->vectorLength * 2 * (sizeof(dataVector_t)));
					IfTrue(newVector, DEBUG,  "Error in realloc for vector of size %ld",  (pClone->vectorLength * 2 * (sizeof(dataVector_t))));
					pClone->pVector = newVector;
					pClone->vectorLength = pClone->vectorLength * 2;
					newVector = 0;
				}
			}
    	}
    }
    if (buffer) {
    	//copy the last buffer
		pClone->pVector[pClone->vectorUsed].buffer = buffer;
		pClone->pVector[pClone->vectorUsed].offset = 0;
		pClone->pVector[pClone->vectorUsed].length = bufferUsed;
		buffer = 0;
		pClone->vectorUsed++;
    }
	goto OnSuccess;
OnError:
	if (buffer) {
		dataStreamBufferFree(buffer);
		buffer = 0;
	}
	if (pClone) {
		dataStreamDelete(pClone);
		pClone = 0;
	}
OnSuccess:
	return pClone;
}

void dataStreamDelete(dataStream_t dataStream) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	if (pDataStream) {
		for (int i = 0; i < pDataStream->vectorUsed; i++) {
			dataVector_t pVector = pDataStream->pVector[i];
			if (pVector.buffer != NULL) {
				dataStreamBufferFree(pVector.buffer);
			}
		}
		if (pDataStream->chunkpool) {
			chunkpoolFree(pDataStream->chunkpool, pDataStream->pVector);
			chunkpoolFree(pDataStream->chunkpool, pDataStream);
		}else {
			FREE(pDataStream->pVector);
			FREE(pDataStream);
		}
	}
}

u_int32_t dataStreamGetSize(dataStream_t dataStream) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	if (pDataStream) {
		return pDataStream->size;
	}
	return 0;
}

u_int32_t dataStreamTotalSize(dataStream_t dataStream) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	u_int32_t         totalSize   = 0;
	if (pDataStream) {
		totalSize += sizeof(dataStreamImpl_t);
		totalSize += pDataStream->vectorLength * sizeof(dataVector_t);
		for (int i = 0; i < pDataStream->vectorUsed; i++) {
			totalSize += pDataStream->pVector[i].length + sizeof(bufferImpl_t);
		}
	}
	return totalSize;
}

int dataStreamAppendData(dataStream_t dataStream, void* buffer, u_int32_t offset, u_int32_t length) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	int               returnValue = 0;

	IfTrue(pDataStream, ERR, "Null dataStream");
	IfTrue(buffer, ERR, "Null buffer");
	IfTrue(length > 0, WARN, "Zero size buffer");
	IfTrue(!pDataStream->chunkpool, ERR, "Append to chunkpool data stream not allowed");

	if (pDataStream->vectorUsed == pDataStream->vectorLength) {
		//TODO : handle realloc failure later
		pDataStream->pVector = realloc(pDataStream->pVector, pDataStream->vectorLength * 2 * sizeof(dataVector_t));
		pDataStream->vectorLength *= 2;
	}

	if (pDataStream->vectorUsed < pDataStream->vectorLength) {
		//LOG("ADDING buffer %p offset %d length %d\n", ((char*)buffer-(sizeof(bufferImpl_t))), offset, length);
		pDataStream->pVector[pDataStream->vectorUsed].buffer = buffer;
		pDataStream->pVector[pDataStream->vectorUsed].length = length;
		pDataStream->pVector[pDataStream->vectorUsed].offset = offset;
		pDataStream->size += length;
		pDataStream->vectorUsed++;
		dataStreamBufferIncrementRefCount(buffer);
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}

int dataStreamAppendDataStream(dataStream_t dataStream, dataStream_t toAppend) {
	dataStreamImpl_t* pDataStream    = DATA_STREAM(dataStream);
	dataStreamImpl_t* pAppendStream  = DATA_STREAM(toAppend);
	int               returnValue    = 0;
	u_int32_t         originalLength = 0;

	IfTrue(pDataStream, ERR, "Null dataStream");
	IfTrue(pAppendStream, ERR, "Null dataStream");

	originalLength = dataStreamGetSize(dataStream);
	for (int i = 0; i < pAppendStream->vectorUsed; i++) {
		returnValue = dataStreamAppendData(pDataStream, pAppendStream->pVector[i].buffer,
				                                        pAppendStream->pVector[i].offset,
				                                        pAppendStream->pVector[i].length);
		IfTrue(returnValue == 0, ERR, "Error appending data");
	}

	goto OnSuccess;
OnError:
	returnValue = -1;
	//no partial appends
	dataStreamTruncateFromEnd(dataStream, originalLength);
OnSuccess:
	return returnValue;
}

void dataStreamPrint(dataStream_t dataStream) {
//#if DEBUG_ME
#if 0
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	LOG(" vectors %d used %d size %d\n", pDataStream->vectorLength , pDataStream->vectorUsed, pDataStream->size);
	for (int i = 0; i < pDataStream->vectorUsed; i++) {
		for (int j = 0; j < pDataStream->pVector[i].length; j++) {
			char c  = ((char*)pDataStream->pVector[i].buffer)[pDataStream->pVector[i].offset+j];
			if (c != '\r') {
				printf("%c", c);
			}
		}
	}
#endif
}

char*  dataStreamToString(dataStream_t dataStream) {
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	char*             buffer      = ALLOCATE_N((pDataStream->size+1), char);
	u_int32_t         copied      = 0;

	if (!buffer) {
		return 0;
	}

	for (int i = 0; i < pDataStream->vectorUsed; i++) {
		memcpy(buffer+copied,
				((char*)(pDataStream->pVector[i].buffer))+pDataStream->pVector[i].offset,
				pDataStream->pVector[i].length);
		copied += pDataStream->pVector[i].length;
	}
	buffer[pDataStream->size] = 0;
	return buffer;
}

dataStream_t dataStreamSubStream(fallocator_t fallocator, dataStream_t dataStream, u_int32_t offset, u_int32_t length) {
	dataStreamIterator_t iter          = 0;
	dataStreamImpl_t*    subDataStream = 0;

	IfTrue(dataStream, ERR, "Null dataStream passed");
	IfTrue(dataStreamGetSize(dataStream) >= (offset + length), WARN, "substream offset/length not in range");

	iter = dataStreamIteratorCreate(fallocator, dataStream,offset,length);
	IfTrue(iter, WARN, "Error creating iterator");
	subDataStream = dataStreamCreate();
	IfTrue(subDataStream, WARN, "Error creating sub data stream");

	int bufferCount = dataStreamIteratorGetBufferCount(iter);
	IfTrue(bufferCount > 0, WARN, "invalid buffer count");
	for (int i = 0; i < bufferCount; i++) {
		u_int32_t offset = 0;
		u_int32_t length = 0;
		void*     buffer = dataStreamIteratorGetBufferAtIndex(iter, i, &offset, &length);
		IfTrue(buffer, ERR, "Got null buffer from iterator");
	    IfTrue( 0 == dataStreamAppendData(subDataStream, buffer, offset, length),
	    		ERR, "Error appending data buffer");
	}
	goto OnSuccess;
OnError:
	if (subDataStream) {
		dataStreamDelete(subDataStream);
		subDataStream = 0;
	}
OnSuccess:
	if (iter) {
		dataStreamIteratorDelete(fallocator, iter);
		iter = 0;
	}
	return subDataStream;
}


static int calculateNumberOfVectors(dataStreamImpl_t* db, u_int32_t offset, u_int32_t length, int* start, int *end, u_int32_t* rangeStart) {

	u_int32_t byteRangeStart = 0;
	u_int32_t byteRangeEnd   = 0;
	int       count          = 0;
	int       started        = 0;
	int       ended          = 0;

	for (int i = 0; i < db->vectorUsed; i++) {
		int added = 0;
		byteRangeStart = byteRangeEnd;
		byteRangeEnd  += db->pVector[i].length;

		if (!started) {
			if (byteRangeStart > offset) {
				continue;
			}else {
				started = 1;
			}
		}

		if ((byteRangeStart <= offset) && (byteRangeEnd > offset)) {
			added  = 1;
			*start = i;
			*rangeStart = byteRangeStart;
		}

		if ((byteRangeStart < (offset+length)) && (byteRangeEnd >= (offset+length))) {
			added = 1;
			*end  = i;
			ended = 1;
		}

		if (byteRangeStart > offset && byteRangeEnd < (offset+length)) {
			added  = 1;
		}

		if (added) {
			count++;
			if (ended) {
				break;
			}
		}
	}
	return count;
}

dataStreamIterator_t dataStreamIteratorCreate(fallocator_t fallocator, dataStream_t dataStream, u_int32_t offset, u_int32_t length) {
	dataStreamImpl_t*         pDataStream = DATA_STREAM(dataStream);
	dataStreamIteratorImpl_t* pIterator   = fallocatorMalloc(fallocator, sizeof(dataStreamIteratorImpl_t));

	IfTrue(pDataStream, ERR, "Null data stream passed");
	IfTrue(pIterator, WARN, "Error allocating memory");

	u_int32_t byteRangeStart = 0;
	u_int32_t byteRangeEnd   = 0;

	int start = 0, end = 0;
	int noOfVectors         = calculateNumberOfVectors(pDataStream, offset, length, &start, &end, &byteRangeEnd);
	pIterator->size         = length;
	pIterator->pVector      = fallocatorMalloc(fallocator, noOfVectors * sizeof(dataVector_t));
	pIterator->vectorLength = noOfVectors;

	IfTrue(pIterator->pVector, WARN, "Error allocating memory %ld", noOfVectors * sizeof(dataVector_t));


	int count = 0;
	for (int i = start; i <= end; i++) {
		bool added     = false;
		byteRangeStart = byteRangeEnd;
		byteRangeEnd  += pDataStream->pVector[i].length;

		//* first vector
		if ((byteRangeStart <= offset) && (byteRangeEnd > offset)) {
			pIterator->pVector[count].buffer = pDataStream->pVector[i].buffer;
			pIterator->pVector[count].offset = pDataStream->pVector[i].offset + (offset - byteRangeStart);
			pIterator->pVector[count].length = pDataStream->pVector[i].length - (offset - byteRangeStart);
			added = true;
			dataStreamBufferIncrementRefCount(pDataStream->pVector[i].buffer);
		}

		// last vector
		if ((byteRangeStart < (offset+length)) && (byteRangeEnd >= (offset+length))) {
			if (!added) {
				pIterator->pVector[count].buffer = pDataStream->pVector[i].buffer;
				pIterator->pVector[count].offset = pDataStream->pVector[i].offset;
				pIterator->pVector[count].length = pDataStream->pVector[i].length - (byteRangeEnd - (offset+length));
				added = true;
				dataStreamBufferIncrementRefCount(pDataStream->pVector[i].buffer);
			}else {
				// if it is both first and last vector at the same time
				// adjust the length of the vector
				pIterator->pVector[count].length -= (byteRangeEnd - (length +offset));
			}
		}

		if (byteRangeStart > offset && byteRangeEnd < (offset+length)) {
			if (!added) {
				pIterator->pVector[count].buffer = pDataStream->pVector[i].buffer;
				pIterator->pVector[count].offset = pDataStream->pVector[i].offset;
				pIterator->pVector[count].length = pDataStream->pVector[i].length;
				added = true;
				dataStreamBufferIncrementRefCount(pDataStream->pVector[i].buffer);
			}
		}

		if (added) {
			count++;
			if (count == noOfVectors) {
				break;
			}
		}
	}
	goto OnSuccess;
OnError:
	if (pIterator) {
		dataStreamIteratorDelete(fallocator, pIterator);
		pIterator = NULL;
	}
OnSuccess:
	return pIterator;
}

u_int32_t dataStreamIteratorGetSize(dataStreamIterator_t iterator) {
	dataStreamIteratorImpl_t* pIterator = (dataStreamIteratorImpl_t*)iterator;
	if (pIterator) {
		return pIterator->size;
	}
	return 0;
}

void dataStreamIteratorDelete(fallocator_t fallocator, dataStreamIterator_t iterator) {
	dataStreamIteratorImpl_t* pIterator = (dataStreamIteratorImpl_t*)iterator;
	if (pIterator) {
		if (pIterator->pVector) {
			for (int i = 0; i < pIterator->vectorLength; i++) {
				dataStreamBufferFree(pIterator->pVector[i].buffer);
			}
			fallocatorFree(fallocator, pIterator->pVector);
			pIterator->pVector = NULL;
		}
		fallocatorFree(fallocator, pIterator);
	}
}

u_int32_t dataStreamIteratorGetBufferCount(dataStreamIterator_t iterator) {
	dataStreamIteratorImpl_t* pIterator = (dataStreamIteratorImpl_t*)iterator;
	if (pIterator) {
		return pIterator->vectorLength;
	}
	return 0;
}


void* dataStreamIteratorGetBufferAtIndex(dataStreamIterator_t iterator, u_int32_t index, u_int32_t* offset, u_int32_t* length) {
	dataStreamIteratorImpl_t* pIterator = (dataStreamIteratorImpl_t*)iterator;
	if (pIterator && index < pIterator->vectorLength) {
		*offset = pIterator->pVector[index].offset;
		*length = pIterator->pVector[index].length;
		return pIterator->pVector[index].buffer;
	}
	return NULL;
}

int dataStreamFindEndOfLine(dataStream_t dataStream) {
	u_int8_t current = 0, previous = 0;
	int result = -1;
	int sofar  = 0;
	dataStreamImpl_t* pDataStream = DATA_STREAM(dataStream);
	IfTrue(dataStream, WARN, "Null data Stream");

	for (int vi = 0; vi < pDataStream->vectorUsed; vi++) {
		for (u_int32_t i = 0; i < pDataStream->pVector[vi].length; i++) {
			previous = current;
			current = *(((char*)pDataStream->pVector[vi].buffer) + ( pDataStream->pVector[vi].offset + i));
			if (current == '\n') {
				if (previous == '\r') {
					result =  sofar + i - 1;
					goto OnSuccess;
				} else {
					goto OnError;
				}
			}
		}
		sofar += pDataStream->pVector[vi].length;
	}
	goto OnSuccess;
OnError:
	result = -1;
OnSuccess:
	return result;
}

char* dataStreamIteratorGetString(fallocator_t fallocator, dataStreamIterator_t iterator, u_int32_t offset, u_int32_t length) {
	dataStreamIteratorImpl_t* pIterator = (dataStreamIteratorImpl_t*)iterator;
	char* output = 0;

	u_int32_t startRange = 0;
	u_int32_t endRange   = 0;
	int       copied     = 0;

	output = fallocatorMalloc(fallocator, (length+1));
	if (!output) {
		LOG(WARN, "Error allocating memory");
		return 0;
	}

	for (int i = 0; i < pIterator->vectorLength; i++) {
		startRange = endRange;
		endRange  += pIterator->pVector[i].length;

		if (startRange < offset && endRange < offset) {
			continue;
		}
		//normal case
		if (startRange <= offset && endRange >= (offset+length)) {
			memcpy(output, (char*)pIterator->pVector[i].buffer+pIterator->pVector[i].offset + ( offset - startRange), length);
			output[length] = 0;
			break;
		}

		if (startRange <= offset && endRange < (offset+length)) {
			//not complete
			memcpy(output, (char*)pIterator->pVector[i].buffer+pIterator->pVector[i].offset + (offset - startRange), (endRange - offset));
			copied = endRange - offset;
		}

		if (startRange > offset && endRange >= (offset+length)) {
			//complete
			memcpy(output+copied, (char*)pIterator->pVector[i].buffer+pIterator->pVector[i].offset, (offset+length - startRange));
			output[length] = 0;
			break;
		}


		if (startRange > offset && endRange < (offset + length)) {
			memcpy(output+copied, (char*)pIterator->pVector[i].buffer+pIterator->pVector[i].offset, pIterator->pVector[i].length);
			copied += pIterator->pVector[i].length;
		}
	}
	return output;
}
