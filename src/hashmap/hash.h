#ifndef HASHMAP_HASH_H
#define HASHMAP_HASH_H

#ifdef    __cplusplus
extern "C" {
#endif

uint32_t hashbig(const void *key, size_t length, const uint32_t initval);

uint32_t hashlittle(const void *key, size_t length, const uint32_t initval);

#ifdef    __cplusplus
}
#endif

#endif    /* HASHMAP_HASH_H */

