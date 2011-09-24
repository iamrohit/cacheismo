#ifndef FALLOCATOR_H_
#define FALLOCATOR_H_

#include "../common/common.h"

//////  Fallocator = FAST Allocator /////////
 
typedef void* fallocator_t;

void          fallocatorInit(u_int32_t bufferCount);
fallocator_t  fallocatorCreate(void);
void          fallocatorDelete(fallocator_t fallocator);
void*         fallocatorMalloc(fallocator_t fallocator, u_int32_t size);
void*         fallocatorRealloc(fallocator_t fallocator, void* pointer, u_int32_t osize, u_int32_t nsize);
void          fallocatorFree(fallocator_t fallocator, void* pointer);


#endif /* FALLOCATOR_H_ */
