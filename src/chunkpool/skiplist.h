#ifndef SKIPLIST_H_
#define SKIPLIST_H_

#include "../common/common.h"

typedef void* skipList_t;

skipList_t skipListCreate();
void       skipListDelete(skipList_t skipList);
void       skipListInsertSlab(skipList_t skipList, u_int32_t slabID);
void       skipListDeleteSlab(skipList_t skipList, u_int32_t slabID);
u_int32_t  skipListFindNextSlab(skipList_t skipList, u_int32_t slabID);
u_int32_t  skipListFindPrevSlab(skipList_t skipList, u_int32_t slabID);

#endif /* SKIPLIST_H_ */
