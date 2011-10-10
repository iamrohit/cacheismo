#ifndef COMMON_MAP_H_
#define COMMON_MAP_H_

#include "common.h"

typedef void* map_t;

map_t          mapCreate(void);
void           mapDelete(map_t map);
int            mapPutElement(map_t map, char* key, void* value);
void*          mapGetElement(map_t map, char* key);
int            mapDeleteElement(map_t map, char* key);
u_int32_t      mapSize(map_t map);

#endif //COMMON_MAP_H_
