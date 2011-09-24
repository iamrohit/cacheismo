#ifndef CACHEITEM_CACHEITEM_H_
#define CACHEITEM_CACHEITEM_H_

#include "../common/common.h"
#include "../io/datastream.h"
#include "../chunkpool/chunkpool.h"
#include "../driver/commands.h"
#include "../hashmap/hashentry.h"

typedef void* cacheItem_t;

u_int32_t       cacheItemEstimateSize(command_t* pCommand);
cacheItem_t     cacheItemCreate(chunkpool_t chunkpool, command_t* pCommand);
void            cacheItemDelete(chunkpool_t chunkpool, cacheItem_t cacheItem);
char*           cacheItemGetKey(cacheItem_t cacheItem);
u_int32_t       cacheItemGetKeyLength(cacheItem_t cacheItem);
u_int64_t       cacheItemGetCAS(cacheItem_t cacheItem);
u_int32_t       cacheItemGetFlags(cacheItem_t cacheItem);
dataStream_t    cacheItemGetDataStream(cacheItem_t cacheItem);
u_int32_t       cacheItemGetDataLength(cacheItem_t cacheItem);
void            cacheItemAddReference(cacheItem_t cacheItem);
u_int32_t       cacheItemGetTotalSize(cacheItem_t cacheItem);
u_int32_t       cacheItemGetExpiry(cacheItem_t cacheItem);
hashEntryAPI_t* cacheItemGetHashEntryAPI();

#endif /* CACHEITEM_CACHEITEM_H_ */
