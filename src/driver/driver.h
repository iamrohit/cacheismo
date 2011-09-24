#ifndef DRIVER_H_
#define DRIVER_H_

hashMap_t   getGlobalHashMap();
chunkpool_t getGlobalChunkpool();
int         writeCacheItemToStream(connection_t conn, cacheItem_t item);
int         writeRawStringToStream(connection_t conn, char* value, int length);
cacheItem_t createCacheItemFromCommand(command_t* pCommand);
void        setGlobalLogLevel(int level);

#endif /* DRIVER_H_ */
