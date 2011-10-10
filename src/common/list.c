#include "list.h"

#define POINTER(op, off) ((void*)(((char*)op)+(off)))
#define PREV(list, op) ((void**)(POINTER(op, (list)->prevOff)))
#define NEXT(list, op) ((void**)(POINTER(op, (list)->nextOff)))

typedef struct listImpl_t {
	u_int32_t count;
    u_int16_t nextOff;
    u_int16_t prevOff;
    void*     pFirst;
    void*     pLast;
} listImpl_t;

#define LIST(x) (listImpl_t*)(x)

static void* listMakeNew(listImpl_t* this, void* pCurObj, void* pPrev, void* pNext) {
    if (!pCurObj)
        return NULL;

   if (*NEXT(this, pCurObj) || *PREV(this, pCurObj))
        return NULL;

    *NEXT(this, pCurObj) = pNext;
    if (pPrev) {
         *NEXT(this, pPrev) = pCurObj;
    }else {
        this->pFirst = pCurObj;
    }

    *(PREV(this, pCurObj)) = pPrev;
    if (pNext) {
        *(PREV(this, pNext)) = pCurObj;
    }else {
        this->pLast = pCurObj;
    }

    this->count++;
    return pCurObj;
}

void* listRemove(list_t list, void* pCurObj) {
    listImpl_t* this = LIST(list);
    void*   pNext;
    void*   pPrev;

    if (!pCurObj)
        return NULL;

    pPrev = *(PREV(this, pCurObj));
    *(PREV(this, pCurObj)) = NULL;
    pNext = *(NEXT(this, pCurObj));
    *(NEXT(this, pCurObj)) = NULL;

    if (pPrev) {
         *(NEXT(this, pPrev)) = pNext;
    }else {
        this->pFirst = pNext;
    }

    if (pNext) {
    	*(PREV(this, pNext)) =  pPrev;
    }else {
    	this->pLast = pPrev;
    }

    this->count--;
    return pCurObj;
}


list_t listCreate(int nextOff, int prevOff) {
    listImpl_t* pList = (listImpl_t*)ALLOCATE_1(listImpl_t);
    if (pList) {
        pList->count   = 0;
        pList->pFirst  = 0;
        pList->pLast   = 0;
        pList->nextOff = nextOff;
        pList->prevOff = prevOff;
    }
    return  pList;
}

void listFree(list_t list) {
    listImpl_t* this = LIST(list);
    if (this) {
        FREE(this);
    }
}

int listGetSize(list_t list) {
	listImpl_t* this = LIST(list);
    if (this) {
        return this->count;
    }
    return -1;
}

void* listAddFirst(list_t list, void* pObjToAdd) {
	listImpl_t* this = LIST(list);
    return  listMakeNew(this, pObjToAdd, NULL, this->pFirst);
}

void* listRemoveFirst(list_t list) {
	listImpl_t* this = LIST(list);
    return listRemove(this, this->pFirst);
}

void* listAddLast(list_t list, void* pObjToAdd) {
	listImpl_t* this = LIST(list);
    return  listMakeNew(this, pObjToAdd, this->pLast, NULL);
}

void* listRemoveLast(list_t list) {
	listImpl_t* this = LIST(list);
    return listRemove(this, this->pLast);
}

void* listGetFirst(list_t list) {
	listImpl_t* this = LIST(list);
    return this->pFirst;
}

void* listGetLast(list_t list) {
	listImpl_t* this = LIST(list);
    return this->pLast;
}

void* listGetNext(list_t list, void* pCurObj) {
	listImpl_t* this = LIST(list);
    return (*(NEXT(this, pCurObj)));
}

void* listGetPrev(list_t list, void* pCurObj) {
	listImpl_t* this = LIST(list);
     return (*(PREV(this, pCurObj)));
}
