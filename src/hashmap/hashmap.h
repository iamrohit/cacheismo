#ifndef HASHMAP_HASHMAP_H_
#define HASHMAP_HASHMAP_H_

#include "../common/common.h"
#include "../common/list.h"
#include "hashentry.h"

typedef void* hashMap_t;

hashMap_t      hashMapCreate(hashEntryAPI_t* API);
void           hashMapDelete(hashMap_t hashMap);
int            hashMapPutElement(hashMap_t hashMap, void* value);
void*          hashMapGetElement(hashMap_t hashMap, char* key, u_int32_t  keyLength);
int            hashMapDeleteElement(hashMap_t hashMap, char* key, u_int32_t  keyLength);
u_int32_t      hashMapDeleteExpired(hashMap_t hashMap);
u_int64_t      hashMapDeleteLRU(hashMap_t hashMap, u_int64_t requiredSpace);
u_int32_t      hashMapSize(hashMap_t hashMap);
u_int32_t      hashMapGetPrefixMatchingKeys(hashMap_t hashMap, char* prefix, char** keys);

#endif //HASHMAP_HASHMAP_H_
