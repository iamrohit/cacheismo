#ifndef COMMON_LIST_H_
#define COMMON_LIST_H_

#include "common.h"

#define OFFSET(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

typedef void* list_t;

list_t      listCreate(int nextOff, int prevOff);
void        listFree(list_t list);
void*       listRemove(list_t list, void* pObjToRemove);
int         listGetSize(list_t list);
void*       listAddFirst(list_t list, void* pObjToAdd);
void*       listRemoveFirst(list_t list);
void*       listAddLast(list_t list, void* pObjToAdd);
void*       listRemoveLast(list_t list);
void*       listGetFirst(list_t list);
void*       listGetLast(list_t list);
void*       listGetNext(list_t list, void* pCurObj);
void*       listGetPrev(list_t list, void* pCurObj);

#endif /* COMMON_LIST_H_ */
