#ifndef LUACLUSTERMAP_H_
#define LUACLUSTERMAP_H_

#include "../common/list.h"
#include "../datastream/datastream.h"

typedef struct keyValue_t {
	struct keyValue_t* pNext;
	struct keyValue_t* pPrev;
	char*              key;
	char*              value;
	int                length;
}keyValue_t;

typedef struct serverEntry_t {
	struct serverEntry_t* pNext;
	struct serverEntry_t* pPrev;
	char*                 serverName;
	list_t                keyValueList;
}serverEntry_t;


typedef struct {
	int             total;
	int             received;
	list_t          serverList;
} multiContext_t;


int luaCommandGetValueFromExternalServer(lua_State* L);
int luaCommandGetMultipleValuesFromExternalServers(lua_State* L);
void clusterMapResultHandler(void* luaContext, void* keyContext, int status, dataStream_t data) ;

#endif /* LUACLUSTERMAP_H_ */
