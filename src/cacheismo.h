#ifndef CACHEISMO_H_
#define CACHEISMO_H_

#include "common/common.h"
#include "hashmap/hashmap.h"
#include "chunkpool/chunkpool.h"
#include "cacheitem/cacheitem.h"
#include "io/connection.h"

hashMap_t           getGlobalHashMap(void);
chunkpool_t         getGlobalChunkpool(void);
struct event_base*  getGlobalEventBase(void);
int                 writeCacheItemToStream(connection_t conn, cacheItem_t item);
int                 writeRawStringToStream(connection_t conn, char* value, int length);
cacheItem_t         createCacheItemFromCommand(command_t* pCommand);
void                setGlobalLogLevel(int level);
void                onLuaResponseAvailable(connection_t connection, int result);

#endif /* CACHEISMO_H_ */
