#ifndef COMMON_SKIPLIST_H_
#define COMMON_SKIPLIST_H_

#include "common.h"

typedef void* skipList_t;

skipList_t skipListCreate(void);
void       skipListDelete(skipList_t skipList);
void       skipListInsertSlab(skipList_t skipList, u_int32_t slabID);
void       skipListDeleteSlab(skipList_t skipList, u_int32_t slabID);
u_int32_t  skipListFindNextSlab(skipList_t skipList, u_int32_t slabID);
u_int32_t  skipListFindPrevSlab(skipList_t skipList, u_int32_t slabID);

#endif /* COMMON_SKIPLIST_H_ */
