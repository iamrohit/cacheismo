#include "skiplist.h"

/* What I am looking for is a simple data structure which will help
 * when the actual slab is empty. Idea is to quickly find the next
 * not-empty slab instead of traversing all 256 elements of the slab
 * list.
 *
 * Using a simple linked list of no empty slabs is possible, but it
 * would cause many wasted cycles when inserting nodes in the list
 */

#define MAX_SKIP_LEVEL 8

typedef struct slabSkipNode_t {
	u_int32_t              slabID;
	struct slabSkipNode_t* next[MAX_SKIP_LEVEL+1];
} slabSkipNode_t;

typedef struct freeNode_t {
	struct freeNode_t* next;
} freeNode_t;

typedef struct {
	slabSkipNode_t *head;
    int             level;
    freeNode_t     *freeList;
} skipListImpl_t;

#define NIL(x) (x)->head

#define SKIP_LIST(x)  (skipListImpl_t*)(x)

skipList_t skipListCreate(void) {
	skipListImpl_t* pList = 0;
	pList = ALLOCATE_1(skipListImpl_t);

	IfTrue(pList, ERR, "Error allocating memory");
	pList->head = ALLOCATE_1(slabSkipNode_t);
	IfTrue(pList->head, ERR, "Error allocating memory");

	for (int i = 0; i <= MAX_SKIP_LEVEL; i++) {
		pList->head->next[i] = NIL(pList);
	}
	pList->level = 0;
	goto OnSuccess;
OnError:
	if (pList) {
		skipListDelete(pList);
		pList = 0;
	}
OnSuccess:
	return pList;
}

void  skipListDelete(skipList_t skipList) {
	skipListImpl_t* pList = SKIP_LIST(skipList);
	if (pList) {
		slabSkipNode_t* pCurrent = pList->head->next[0];
		while (pCurrent != NIL(pList)) {
			slabSkipNode_t* pNext = pCurrent->next[0];
			FREE(pCurrent);
			pCurrent = pNext;
		}
		FREE(pList->head);
		FREE(pList);
	}
}

void skipListInsertSlab(skipList_t skipList, u_int32_t slabID) {
	int             i, newLevel;
	slabSkipNode_t *update[MAX_SKIP_LEVEL+1];
	slabSkipNode_t *pCurrent;
	skipListImpl_t* pList = SKIP_LIST(skipList);

	/* find where data belongs */
	pCurrent = pList->head;
	for (i = pList->level; i >= 0; i--) {
		while (pCurrent->next[i] != NIL(pList) &&
			  (pCurrent->next[i]->slabID < slabID)) {
			pCurrent = pCurrent->next[i];
		}
		update[i] = pCurrent;
	}
	pCurrent = pCurrent->next[0];
	//check if it already exists
	if (pCurrent != NIL(pList) && (pCurrent->slabID == slabID)) return;

	/* determine level */
	for (newLevel = 0; rand() < RAND_MAX/2 && newLevel < MAX_SKIP_LEVEL; newLevel++);

	if (newLevel > pList->level) {
		for (i = pList->level + 1; i <= newLevel; i++) {
			update[i] = NIL(pList);
		}
		pList->level = newLevel;
	}

	/* make new node */
	if (pList->freeList) {
		pCurrent = (slabSkipNode_t*)pList->freeList;
		pList->freeList = pList->freeList->next;
	}else {
		pCurrent = ALLOCATE_1(slabSkipNode_t);
		//TODO - we don't handle the failure here..
		// What would be nice is to prealloc all the
		// nodes at create time and don't depend on
		// malloc during execution
	}
	if (pCurrent) {
		pCurrent->slabID = slabID;
		for (i = 0; i <= newLevel; i++) {
			pCurrent->next[i] = update[i]->next[i];
			update[i]->next[i] = pCurrent;
		}
	}
}


void skipListDeleteSlab(skipList_t skipList, u_int32_t slabID) {
	slabSkipNode_t *update[MAX_SKIP_LEVEL+1];
	slabSkipNode_t *pCurrent;
	skipListImpl_t *pList = SKIP_LIST(skipList);

	pCurrent = pList->head;

	for (int i = pList->level; i >= 0; i--) {
		while (pCurrent->next[i] != NIL(pList)
		  && (pCurrent->next[i]->slabID < slabID)) {
			pCurrent = pCurrent->next[i];
		}
		update[i] = pCurrent;
	}
	pCurrent = pCurrent->next[0];
	if (pCurrent == NIL(pList) || (pCurrent->slabID != slabID)) return;

	/* adjust forward pointers */
	for (int i = 0; i <= pList->level; i++) {
		if (update[i]->next[i] != pCurrent) break;
		update[i]->next[i] = pCurrent->next[i];
	}

	freeNode_t* pFree = (freeNode_t*)pCurrent;
	pFree->next = pList->freeList;
	pList->freeList = pFree;
	pCurrent = 0;

	/* adjust header level */
	while ((pList->level > 0) && (pList->head->next[pList->level] == NIL(pList))) {
		pList->level--;
	}
}


u_int32_t  skipListFindNextSlab(skipList_t skipList, u_int32_t slabID) {
    skipListImpl_t *pList    = SKIP_LIST(skipList);
    slabSkipNode_t *pCurrent = pList->head;

    for (int i = pList->level; i >= 0; i--) {
        while (pCurrent->next[i] != NIL(pList)
          && (pCurrent->next[i]->slabID < slabID))
        	pCurrent = pCurrent->next[i];
    }
    pCurrent = pCurrent->next[0];
    if (pCurrent != NIL(pList) && (pCurrent->slabID > slabID)) return pCurrent->slabID;
    return 0;
}

u_int32_t  skipListFindPrevSlab(skipList_t skipList, u_int32_t slabID) {
    skipListImpl_t *pList    = SKIP_LIST(skipList);
    slabSkipNode_t *pCurrent = pList->head;

    for (int i = pList->level; i >= 0; i--) {
        while (pCurrent->next[i] != NIL(pList) && (pCurrent->next[i]->slabID < slabID)) {
        	pCurrent = pCurrent->next[i];
        }
    }

    if (pCurrent != NIL(pList) && (pCurrent->slabID < slabID)) {
    	return pCurrent->slabID;
    }
    return 0;
}
