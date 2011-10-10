#ifndef HASHMAP_HASH_H
#define HASHMAP_HASH_H

#ifdef    __cplusplus
extern "C" {
#endif

#include "../common/common.h"

uint32_t hash(const void *key, size_t length, const uint32_t initval);

#ifdef    __cplusplus
}
#endif

#endif    /* HASHMAP_HASH_H */

