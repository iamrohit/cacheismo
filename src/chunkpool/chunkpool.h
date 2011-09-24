/* slabs memory allocation */
#ifndef CHUNKPOOL_CHUNKPOOL_H
#define CHUNKPOOL_CHUNKPOOL_H

#include "../common/common.h"

typedef void* chunkpool_t;

chunkpool_t  chunkpoolCreate(u_int32_t maxSizeInPages);
void         chunkpoolDelete(chunkpool_t chunkpool);
void*        chunkpoolMalloc(chunkpool_t chunkpool, u_int32_t size);
void*        chunkpoolRelaxedMalloc(chunkpool_t chunkpool, u_int32_t prefferedSize, u_int32_t *pActualSize);
void*        chunkpoolRealloc(chunkpool_t chunkpool, void* pointer, u_int32_t newSize);
void         chunkpoolFree(chunkpool_t  chunkpool, void* pointer);
void         chunkpoolGC(chunkpool_t  chunkpool);
void         chunkpoolPrint(chunkpool_t  chunkpool);
u_int32_t    chunkpoolMaxMallocSize(chunkpool_t chunkpool);
u_int32_t    chunkpoolMemoryUsed(chunkpool_t chunkpool);

#endif //CHUNKPOOL_CHUNKPOOL_H
