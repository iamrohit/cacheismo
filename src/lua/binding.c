#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <dirent.h>

#include "binding.h"
#include "marshal.h"

#include "../common/commands.h"
#include "../common/map.h"
#include "../cacheitem/cacheitem.h"
#include "../hashmap/hashmap.h"
#include "../io/connection.h"
#include "../cacheismo.h"

#include "../cluster/consistent.h"
#include "../cluster/clustermap.h"


#define LUA_CONFIG_FILE "config.lua"
#define LUA_CORE_FILE   "core.lua"


typedef struct {
	fallocator_t  fallocator;
	lua_State*    luaState;
	clusterMap_t  clusterMap;
}luaRunnableImpl_t;


typedef struct luaContext_t {
	connection_t    connection;
	fallocator_t    fallocator;
	command_t*      pCommand;
	luaRunnable_t   runnable;
	int             threadRef;
} luaContext_t;

/* for handling parallel gets we need to keep track of
 * what was asked by the script and have we got all
 * of it.
 */

typedef struct keyValue_t {
	struct keyValue_t* pNext;
	struct keyValue_t* pPrev;
	char*              key;
	char*              value;
}keyValue_t;

/* We initialize the map and set the total to nunber of
 * keys we have asked for. Everytime we get a callback
 * we insert the keyValue_t in the map against the
 * correct server value.
 *
 * if received == total, we have got all the callbacks and
 * now we will resume the script.
 * The results will be returned to the lua script in the
 * form of a map...
 *  {
 *    server1 => {{key1 => value1}, {key2 => value2}}
 *  }
 *  If we got error from the server, the corresponding
 *  entry will have nil value
 */

typedef struct {
	int    total;
	int    received;
	map_t  serverMap;
} multiContext_t;


#define LUA_RUNNABLE(x) (luaRunnableImpl_t*)(x)

/* these are from driver.c */

/*
static multiContext_t* multiContextCreate(int count) {
	multiContext_t* pMContext = ALLOCATE_1(multiContext_t);
	if (pMContext) {
		pMContext->serverMap = mapCreate();
		pMContext->total     = count;
		if (!pMContext->serverMap) {
			FREE(pMContext);
			pMContext = 0;
		}
	}
	return pMContext;
}

static void multiContextDelete(multiContext_t* pMContext) {

}
*/

static const char* command2String(enum commands_enum_t command) {
	switch (command) {
	case COMMAND_GET:        return "get";
	case COMMAND_BGET:       return "bget";
	case COMMAND_ADD:        return "add";
	case COMMAND_SET:        return "set";
	case COMMAND_REPLACE:    return "replace";
	case COMMAND_PREPEND:    return "prepend";
	case COMMAND_APPEND:     return "append";
	case COMMAND_CAS:        return "cas";
	case COMMAND_INCR:       return "incr";
	case COMMAND_DECR:       return "decr";
	case COMMAND_GETS:       return "gets";
	case COMMAND_DELETE:     return "delete";
	case COMMAND_STATS:      return "stats";
	case COMMAND_FLUSH_ALL:  return "flush_all";
	case COMMAND_VERSION:    return "version";
	case COMMAND_QUIT:       return "quit";
	case COMMAND_VERBOSITY:  return "verbosity";
	}
	return 0;
}

static void stackdump(lua_State* l)
{
    int i;
    int top = lua_gettop(l);

    LOG(ERR, "total items in stack %d\n",top);

    for (i = 1; i <= top; i++)
    {  /* repeat for each level */
        int t = lua_type(l, i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
                LOG(ERR, "string: '%s'\n", lua_tostring(l, i));
                break;
            case LUA_TBOOLEAN:  /* booleans */
            	LOG(ERR, "boolean %s\n",lua_toboolean(l, i) ? "true" : "false");
                break;
            case LUA_TNUMBER:  /* numbers */
            	LOG(ERR, "number: %g\n", lua_tonumber(l, i));
                break;
            default:  /* other values */
            	LOG(ERR, "%s\n", lua_typename(l, t));
                break;
        }
    }
}


static int luaCommandNew(lua_State* L, connection_t connection, fallocator_t fallocator,
		command_t *pCommand, luaRunnable_t runnable) {
   luaContext_t* context = (luaContext_t*)lua_newuserdata(L, sizeof(luaContext_t));
   context->pCommand    = pCommand;
   context->connection  = connection;
   context->fallocator  = fallocator;
   context->runnable    = runnable;
   context->threadRef   = LUA_NOREF;
   lua_getglobal(L, "Command");
   lua_setmetatable(L, -2);
   return 1;
}

static int luaCommandGetCommand(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    const char* value = command2String(context->pCommand->command);
    if (value) {
		lua_pushstring(L, value);
    }else {
    	lua_pushnil(L);
    }
    return 1;
}

static int luaCommandGetKeySize(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->keySize);
    return 1;
}

static int luaCommandGetKey(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	if (context->pCommand->key) {
		lua_pushstring(L, context->pCommand->key);
	}else {
		lua_pushnil(L);
	}
    return 1;
}

//TODO -- we should return some success/failure here
static int luaCommandSetKey(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	size_t l;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		char* newKey = fallocatorMalloc(context->fallocator, l + 1);
		if (newKey) {
			strncpy(newKey, s, l);
			newKey[l] = 0;
			fallocatorFree(context->fallocator, context->pCommand->key);
			context->pCommand->key = newKey;
			context->pCommand->keySize = l;
		}
	}
	return 0;
}

//TODO -- we should return some success/failure here
static int luaCommandSetData(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	size_t l;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		dataStream_t ds = dataStreamCreate();
		if (ds) {
			char*  buffer = (char*) dataStreamBufferAllocate(NULL, context->fallocator, l);
			if (buffer) {
				memcpy(buffer, s, l);
				if (0 == dataStreamAppendData(ds, buffer, 0, l)) {
					if (context->pCommand->dataStream) {
					  	dataStreamDelete(context->pCommand->dataStream);
					}
					context->pCommand->dataStream = ds;
					context->pCommand->dataLength = l;
					ds = 0;
				}
				//decrease our refcount
				dataStreamBufferFree(buffer);
				buffer = 0;
			}
		}
	}
	return 0;
}

static int luaCommandGetData(lua_State* L) {
	char*         buffer  = 0;
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	if (context->pCommand->dataStream && context->pCommand->dataLength > 0) {
		buffer = dataStreamToString(context->pCommand->dataStream);
		if (buffer) {
			lua_pushlstring(L, buffer, context->pCommand->dataLength);
			FREE(buffer);
			return 1;
		}
	}
	//failed .. just return nil
	lua_pushnil(L);
    return 1;
}

static int luaCommandSetExpiryTime(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	int        expiryTime = lua_tointeger(L, 2);
    context->pCommand->expiryTime = expiryTime;
    return 0;
}

static int luaCommandSetDelta(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	int             delta = lua_tointeger(L, 2);
	context->pCommand->delta = delta;
	return 0;
}

static int luaCommandSetNoReply(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	int           noReply = lua_tointeger(L, 2);
	context->pCommand->noreply = noReply;
    return 0;
}

static int luaCommandSetFlags(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	int           flags   = lua_tointeger(L, 2);
	context->pCommand->flags = flags;
    return 0;
}

static int luaCommandGetExpiryTime(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->expiryTime);
    return 1;
}

static int luaCommandGetDelta(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->delta);
    return 1;
}

static int luaCommandGetNoReply(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->noreply);
    return 1;
}

static int luaCommandGetFlags(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->flags);
    return 1;
}


static int luaCommandGetDataSize(lua_State* L) {
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, context->pCommand->dataLength);
    return 1;
}

static int luaCacheItemNewFromCommand(lua_State* L) {
	cacheItem_t   item    = 0;
	luaContext_t* context = (luaContext_t*) lua_touserdata(L, 1);
	item = createCacheItemFromCommand(context->pCommand);
	if (item) {
		cacheItem_t *pItem = (cacheItem_t *)lua_newuserdata(L, sizeof(cacheItem_t));
		*pItem = item;
		lua_getglobal(L, "CacheItem");
		lua_setmetatable(L, -2);
	}else {
		lua_pushnil(L);
	}
   return 1;
}

static int luaCommandWriteCacheItem(lua_State* L) {
	luaContext_t*  context = (luaContext_t*)lua_touserdata(L, 1);
	cacheItem_t*   pItem   = (cacheItem_t*)lua_touserdata(L, 2);
	int result = writeCacheItemToStream(context->connection, *pItem);
    lua_pushnumber(L, result);
    return 1;
}

static int luaCommandWriteString(lua_State* L) {
	luaContext_t*  context = (luaContext_t*)lua_touserdata(L, 1);
	size_t l;
	int result = -1;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		result = writeRawStringToStream(context->connection, (char*)s, l);
	}
	lua_pushnumber(L, result);
	return 1;
}

static int luaCommandHasMultipleKeys(lua_State* L) {
	luaContext_t*  context = (luaContext_t*)lua_touserdata(L, 1);
	lua_pushnumber(L, context->pCommand->multiGetKeysCount);
	return 1;
}

static int luaCommandGetMultipleKeys(lua_State* L) {
	luaContext_t*  context = (luaContext_t*)lua_touserdata(L, 1);
	if (context->pCommand->multiGetKeysCount) {
		lua_createtable(L, context->pCommand->multiGetKeysCount, 0);
		int newTable = lua_gettop(L);
		int index    = 1;
		for (int i = 0; i < context->pCommand->multiGetKeysCount; i++) {
			lua_pushstring(L, context->pCommand->multiGetKeys[i]);
			lua_rawseti(L, newTable, index);
			index++;
		}
	}else {
		lua_pushnil(L);
	}
    return 1;
}

static int luaCommandGetValueFromExternalServer(lua_State* L) {
	luaContext_t*      context   = (luaContext_t*) lua_touserdata(L, 1);
	char*              key       = 0;
	char*              server    = 0;
	const char*        luaKey    = 0;
	const char*        luaServer = 0;
	size_t             l         = 0;
	luaRunnableImpl_t* pRunnable = 0;
	int                result    = 0;

	IfTrue(context, ERR, "Null context from lua stack");
	pRunnable = context->runnable;

	luaKey  = luaL_checklstring(L, -1, &l);
	IfTrue(luaKey, WARN, "Unable to get key from lua stack");
	key = strdup(luaKey);
	IfTrue(key, WARN, "Out of memory copying key");

	luaServer = luaL_checklstring(L, -2, &l);
	IfTrue(luaServer, WARN, "Unable to get server from lua stack");
	server = strdup(luaServer);
	IfTrue(server, WARN, "Out of memory copying server");

	LOG(ERR, "context %p server %s key %s\n", context, server, key);
	//current running thread is always present at the top of the
	//main lua stack
	context->threadRef = luaL_ref(pRunnable->luaState, LUA_REGISTRYINDEX);

	IfTrue(0 == clusterMapGet(pRunnable->clusterMap, context, server, key),
			WARN, "Error submitting request to clusterMap");

	goto OnSuccess;
OnError:
	result = -1;
	if (pRunnable) {
		//restore thread on the main stack
		lua_rawgeti(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		luaL_unref(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		context->threadRef = LUA_NOREF;
		lua_pushnil(L);
	}
OnSuccess:
	if (server) {
		free(server);
	}
	if (key) {
		free(key);
	}
	if (result < 0) {
		//one value on the stack, which is nil...
		//script continues to run without any interruption
		return 1;
	}else {
		//we do a yield, nothing on the stack
		//this for the script to stop execution
		//and return result to driver.c
		return lua_yield (L, 0);
	}
}

/*
static int luaCommandPrintTable(lua_State* L) {
	luaContext_t*   context  = (luaContext_t*) lua_touserdata(L, 1);
	lua_pushvalue(L, -1);
	lua_pushnil(L);
	while (lua_next(L, -2))  {
		lua_pushvalue(L, -2);
		const char *key = lua_tostring(L, -1);
		if (lua_istable(L, -2)) {
			lua_pushvalue(L, -2);
			lua_pushnil(L);
			while (lua_next(L, -2))  {
				lua_pushvalue(L, -2);
				const char *key2 = lua_tostring(L, -1);
				const char *value2 = lua_tostring(L, -2);
				lua_pop(L, 2);
			}
			lua_pop(L, 1);
		}else {
			const char *value = lua_tostring(L, -2);
		}
		lua_pop(L, 2);
	}
	lua_pop(L, 1);
	return 0;
}
*/

/*
 * We expect a table on the stack
 *   server1 : key1, key2
 *   server2 : key3, key4, ...
 *
 *   The final result is of the form
 *   server1 : key1 => value1, key2 => value2
 *   server2 : key3 => value3, key4 => value4
 */

/*
static int luaCommandGetMultipleValuesFromExternalServers(lua_State* L) {
	luaContext_t*      context   = (luaContext_t*) lua_touserdata(L, 1);
	char*              key       = 0;
	char*              server    = 0;
	const char*        luaKey    = 0;
	const char*        luaServer = 0;
	size_t             l         = 0;
	luaRunnableImpl_t* pRunnable = 0;
	int                result    = 0;

	IfTrue(context, ERR, "Null context from lua stack");
	pRunnable = context->runnable;

	luaKey  = luaL_checklstring(L, -1, &l);
	IfTrue(luaKey, WARN, "Unable to get key from lua stack");
	key = strdup(luaKey);
	IfTrue(key, WARN, "Out of memory copying key");

	luaServer = luaL_checklstring(L, -2, &l);
	IfTrue(luaServer, WARN, "Unable to get server from lua stack");
	server = strdup(luaServer);
	IfTrue(server, WARN, "Out of memory copying server");

	LOG(ERR, "context %p server %s key %s\n", context, server, key);
	//current running thread is always present at the top of the
	//main lua stack
	context->threadRef = luaL_ref(pRunnable->luaState, LUA_REGISTRYINDEX);

	IfTrue(0 == clusterMapGet(pRunnable->clusterMap, context, server, key),
			WARN, "Error submitting request to clusterMap");

	goto OnSuccess;
OnError:
	result = -1;
	if (pRunnable) {
		//restore thread on the main stack
		lua_rawgeti(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		luaL_unref(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		context->threadRef = LUA_NOREF;
		lua_pushnil(L);
	}
OnSuccess:
	if (server) {
		free(server);
	}
	if (key) {
		free(key);
	}
	if (result < 0) {
		//one value on the stack, which is nil...
		//script continues to run without any interruption
		return 1;
	}else {
		//we do a yield, nothing on the stack
		//this for the script to stop execution
		//and return result to driver.c
		return lua_yield (L, 0);
	}
}
*/

// our methods..
static const luaL_Reg command_methods[] = {
    {"getCommand",     luaCommandGetCommand},
    {"getKey",         luaCommandGetKey},
    {"setKey",         luaCommandSetKey},
    {"getKeySize",     luaCommandGetKeySize},
    {"getExpiryTime",  luaCommandGetExpiryTime},
    {"setExpiryTime",  luaCommandSetExpiryTime},
    {"getDelta",       luaCommandGetDelta},
    {"setDelta",       luaCommandSetDelta},
    {"getFlags",       luaCommandGetFlags},
    {"setFlags",       luaCommandSetFlags},
    {"getNoReply",     luaCommandGetNoReply},
    {"setNoReply",     luaCommandSetNoReply},
    {"getDataSize",    luaCommandGetDataSize},
    {"setData",        luaCommandSetData},
    {"getData",        luaCommandGetData},
    {"newCacheItem",   luaCacheItemNewFromCommand},
    {"writeCacheItem", luaCommandWriteCacheItem},
    {"writeString",    luaCommandWriteString},
    {"hasMultipleKeys",luaCommandHasMultipleKeys},
    {"getMultipleKeys",luaCommandGetMultipleKeys},
    {"getFromExternalServer",luaCommandGetValueFromExternalServer},
    {NULL, NULL}
};


static int luaCacheItemNew(lua_State* L, cacheItem_t item) {
	cacheItem_t *p = (cacheItem_t *)lua_newuserdata(L, sizeof(cacheItem_t));
   *p = item;
   lua_getglobal(L, "CacheItem");
   lua_setmetatable(L, -2);
   return 1;
}



static int luaCacheItemGetKeySize(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
    lua_pushnumber(L,  cacheItemGetKeyLength(*p));
    return 1;
}

static int luaCacheItemGetKey(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
	lua_pushstring(L, cacheItemGetKey(*p));
	return 1;
}


static int luaCacheItemGetData(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
	char* buffer = dataStreamToString(cacheItemGetDataStream(*p));
	if (buffer) {
		lua_pushlstring(L, buffer, cacheItemGetDataLength(*p));
		FREE(buffer);
	}else {
		lua_pushnil(L);
	}
    return 1;
}

static int luaCacheItemGetExpiryTime(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, cacheItemGetExpiry(*p));
    return 1;
}

static int luaCacheItemGetDataSize(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, cacheItemGetDataLength(*p));
    return 1;
}

static int luaCacheItemGetFlags(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
    lua_pushnumber(L, cacheItemGetFlags(*p));
    return 1;
}



static int luaCacheItemDelete(lua_State* L) {
	cacheItem_t* p = (cacheItem_t*) lua_touserdata(L, 1);
    cacheItemDelete(getGlobalChunkpool(), *p);
    return 0;
}

static int luaGetGlobalHashMap(lua_State* L) {
	hashMap_t* p = (hashMap_t*)lua_newuserdata(L, sizeof(hashMap_t));
   *p =  getGlobalHashMap();
   lua_getglobal(L, "HashMap");
   lua_setmetatable(L, -2);
   return 1;
}


static int luaSetGlobalLogLevel(lua_State* L) {
	int   level = lua_tointeger(L, 1);
	setGlobalLogLevel(level);
    return 0;
}


static int luaHashMapGet(lua_State* L) {
	hashMap_t* pHashMap = (hashMap_t*) lua_touserdata(L, 1);
	size_t l;
	cacheItem_t item = 0;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		item = hashMapGetElement(*pHashMap, (char*)s, l);
		if (item) {
			luaCacheItemNew(L, item);
		}else {
			lua_pushnil(L);
		}
	}else {
		lua_pushnil(L);
	}
	return 1;
}

static int luaHashMapDelete(lua_State* L) {
	hashMap_t* pHashMap = (hashMap_t*) lua_touserdata(L, 1);
	size_t l;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		hashMapDeleteElement(*pHashMap, (char*)s, l);
	}
	return 0;
}

static int luaHashMapPut(lua_State* L) {
	hashMap_t*   pHashMap = (hashMap_t*)lua_touserdata(L, 1);
	cacheItem_t* pItem    = (cacheItem_t*)lua_touserdata(L, 2);
	hashMapPutElement(*pHashMap,*pItem);
	return 0;
}

static int luaHashMapDeleteLRU(lua_State* L) {
	hashMap_t*   pHashMap = (hashMap_t*)lua_touserdata(L, 1);
	u_int64_t    freeBytes = lua_tointeger(L, 2);
	hashMapDeleteLRU(*pHashMap, freeBytes);
	return 0;
}


// r methods..
static const luaL_Reg cacheitem_methods[] = {
    {"getKey",        luaCacheItemGetKey},
    {"getKeySize",    luaCacheItemGetKeySize},
    {"getExpiryTime", luaCacheItemGetExpiryTime},
    {"getFlags",      luaCacheItemGetFlags},
    {"getDataSize",   luaCacheItemGetDataSize},
    {"getData",       luaCacheItemGetData},
    {"delete",        luaCacheItemDelete},
    {NULL, NULL}
};


static const luaL_Reg hashmap_methods[] = {
    {"get",       luaHashMapGet},
    {"put",       luaHashMapPut},
    {"delete",    luaHashMapDelete},
    {"deleteLRU", luaHashMapDeleteLRU},
    {NULL, NULL}
};



static int luaConsistentNew(lua_State* L) {
	consistent_t consistent = 0;
	size_t l;
	const char *serverList = luaL_checklstring(L, -1, &l);
	if (serverList && l > 0) {
		consistent =  consistentCreate((char*)serverList);
	}

	if (consistent) {
		consistent_t* pC = (consistent_t*)lua_newuserdata(L, sizeof(consistent_t));
		*pC = consistent;
		lua_getglobal(L, "Consistent");
		lua_setmetatable(L, -2);
	}else {
		lua_pushnil(L);
	}
   return 1;
}

static int luaConsistentDelete(lua_State* L) {
	consistent_t* pC = (consistent_t*)lua_touserdata(L, 1);
	consistentDelete(*pC);
	return 0;
}

static int luaConsistentFindServer(lua_State* L) {
	consistent_t*   pC = (consistent_t*)lua_touserdata(L, 1);
	size_t l = 0;
	const char* server = 0;
	const char *key = luaL_checklstring(L, -1, &l);
	if (key && l > 0) {
		server =  consistentFindServer(*pC, (char*)key);
	}
	if (server) {
		lua_pushstring(L, server);
	}else {
		lua_pushnil(L);
	}
	return 1;
}


static int luaConsistentGetServerCount(lua_State* L) {
	consistent_t*   pC = (consistent_t*)lua_touserdata(L, 1);
	lua_pushinteger(L, consistentGetServerCount(*pC));
	return 1;
}

static int luaConsistentIsServerAvailable(lua_State* L) {
	consistent_t*   pC = (consistent_t*)lua_touserdata(L, 1);
	size_t l;
	int result = 0;
	const char* server = luaL_checklstring(L, -1, &l);
	if (server && l > 0) {
		result =  consistentIsServerAvailable(*pC, (char*)server);
	}
	lua_pushinteger(L, result);
	return 1;
}

static int luaConsistentSetServerAvailable(lua_State* L) {
	consistent_t*   pC = (consistent_t*)lua_touserdata(L, 1);
	size_t l;
	const char* server = luaL_checklstring(L, -1, &l);
	if (server && l > 0) {
		int valueToSet = lua_tointeger(L, 2);
		consistentSetServerAvailable(*pC, (char*)server, valueToSet);
	}
	return 0;
}


// r methods..
static const luaL_Reg consistent_methods[] = {
    {"findServerForKey",   luaConsistentFindServer},
    {"getServerCount",     luaConsistentGetServerCount},
    {"isServerAvailable",  luaConsistentIsServerAvailable},
    {"setServerAvailable", luaConsistentSetServerAvailable},
    {NULL, NULL}
};

static int isCoreOrConfigFile(char* fileName) {
	if ((strcmp(fileName, LUA_CONFIG_FILE)) == 0 || (strcmp(fileName, LUA_CORE_FILE) == 0)) {
		return 1;
	}
	return 0;
}

static int isLuaFile(char* fileName) {
	int length = strlen(fileName);
	if ((length > 4) && (strcmp(".lua", fileName+(length-4)) == 0)) {
		return 1;
	}
	return 0;
}


static char *path_cat (const char *str1, char *str2, int addSlash) {
	char *result = 0;
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	result      = malloc(len1 + addSlash + len2 + 1);

	if (result) {
		strncpy (result, str1, len1);
		if (addSlash) {
			result[len1] = '/';
		}
        strncpy(result+len1+addSlash, str2, len2);
        result[len1+len2+addSlash] = '\0';
	}
	return result;
}

static int loadLuaFile(luaRunnableImpl_t* pRunnable, char* fileName) {
	int err = 0;
	err = luaL_loadfile(pRunnable->luaState, fileName);
	if (err != 0) {
		LOG(ERR, "Error %d in luaL_loadfile %s", err, fileName);
		LOG(ERR, "Message %s",lua_tostring(pRunnable->luaState,-1));
		return -1;
	}
	err = lua_pcall(pRunnable->luaState, 0, 0, 0);
	if (err != 0) {
		LOG(ERR, "Error %d in lua_pcall for file %s", err, fileName);
		LOG(ERR, "Message %s",lua_tostring(pRunnable->luaState,-1));
		return -1;
	}
	return 0;
}

static int loadDirectory (luaRunnableImpl_t* pRunnable, char* dir_path, int enableVirtualKey) {
	int            addSlash = dir_path[strlen(dir_path) -1] != '/';
    struct dirent *dp       = 0;
    DIR           *dir      = opendir(dir_path);
    char          *tmp      = 0;

    //first load the config and core files
    tmp  = path_cat(dir_path, LUA_CORE_FILE, addSlash);
    IfTrue(tmp, ERR, "Error allocating memory");
	IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
	free(tmp); tmp = 0;
    tmp  = path_cat(dir_path, LUA_CONFIG_FILE, addSlash);
    IfTrue(tmp, ERR, "Error allocating memory");
	IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
	free(tmp); tmp = 0;
	if (enableVirtualKey) {
		while ((dp = readdir(dir)) != NULL) {
			tmp = path_cat(dir_path, dp->d_name, addSlash);
			IfTrue(tmp, ERR, "Error allocating memory");
			if (isLuaFile(tmp) && (!isCoreOrConfigFile(tmp)) ) {
				IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
				LOG(INFO, "Loaded file [%s]", tmp);
			}
			free(tmp);
			tmp = 0;
		}
	}
	goto OnSuccess;
OnError:
	if (tmp) {
		free(tmp);
		tmp = 0;
	}
OnSuccess:
	if (dir) {
		closedir(dir);
	}
	return 0;
}


static void* fallocatorBasedAlloc (void *ud, void *ptr, size_t osize, size_t nsize) {
	fallocator_t fallocator = ud;
	if (nsize == 0) {
		if (ptr) {
			fallocatorFree(fallocator, ptr);
		}
		return NULL;
	}else {
		if (osize == 0) {
			return fallocatorMalloc(fallocator, nsize);
		}else {
			void* newPtr = fallocatorMalloc(fallocator, nsize);
			if (newPtr) {
				int size = osize > nsize ? nsize : osize;
				memcpy(newPtr, ptr, size);
				if (ptr) {
					fallocatorFree(fallocator, ptr);
				}
				return newPtr;
			}
			return NULL;
		}
	}
}



static void clusterMapResultHandler(void* luaContext, char* key, int status, dataStream_t data);


luaRunnable_t luaRunnableCreate(char* directory, int enableVirtualKey) {
	luaRunnableImpl_t* pRunnable = ALLOCATE_1(luaRunnableImpl_t);
	pRunnable->fallocator = fallocatorCreate();
	pRunnable->luaState = lua_newstate(fallocatorBasedAlloc, pRunnable->fallocator);
	pRunnable->clusterMap = clusterMapCreate(clusterMapResultHandler);
	luaL_openlibs(pRunnable->luaState);
	lua_register(pRunnable->luaState, "getHashMap",       luaGetGlobalHashMap);
	lua_register(pRunnable->luaState, "setLogLevel",      luaSetGlobalLogLevel);
	lua_register(pRunnable->luaState, "newConsistent",    luaConsistentNew);
	lua_register(pRunnable->luaState, "deleteConsistent", luaConsistentDelete);

	//open the marshling library
	luaopen_marshal(pRunnable->luaState, pRunnable->fallocator);
	luaL_register(pRunnable->luaState, "HashMap", hashmap_methods);
	lua_pushvalue(pRunnable->luaState,-1);
	lua_setfield(pRunnable->luaState, -2, "__index");
	luaL_register(pRunnable->luaState, "CacheItem", cacheitem_methods);
	lua_pushvalue(pRunnable->luaState,-1);
	lua_setfield(pRunnable->luaState, -2, "__index");
	luaL_register(pRunnable->luaState, "Command", command_methods);
	lua_pushvalue(pRunnable->luaState,-1);
	lua_setfield(pRunnable->luaState, -2, "__index");
	luaL_register(pRunnable->luaState, "Consistent", consistent_methods);
	lua_pushvalue(pRunnable->luaState,-1);
	lua_setfield(pRunnable->luaState, -2, "__index");
	if (0 == loadDirectory(pRunnable, directory, enableVirtualKey)) {
		return pRunnable;
	}
	luaRunnableDelete(pRunnable);
	return 0;
}


void luaRunnableDelete(luaRunnable_t runnable) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	if (pRunnable) {
		lua_close(pRunnable->luaState);
		fallocatorDelete(pRunnable->fallocator);
		FREE(pRunnable);
	}
}
/**
 * Periodically called by the event loop
 */
void luaRunnableGC(luaRunnable_t runnable) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	lua_gc(pRunnable->luaState, LUA_GCCOLLECT, 0);
}


/**
 * In non cluster mode we either run with virtual keys enabled or not.
 * When virtual keys are enabled, we load the script files and in case
 * of get requests, we check if the first two tokens in the key
 * correspond to some script/function pair. This adds may be 5%
 * overhead which can be avoided if virtual key support is not
 * required.
 *
 */
static int luaRunnableRunNoCluster(luaRunnableImpl_t* pRunnable, connection_t connection,
		 fallocator_t fallocator, command_t* pCommand,
		 int enableVirtualKey) {

	int result = 0;

	if (enableVirtualKey) {
		lua_getglobal(pRunnable->luaState, "mainVirtualKey");
	}else {
		lua_getglobal(pRunnable->luaState, "mainNormal");
	}
	luaCommandNew(pRunnable->luaState, connection, fallocator, pCommand, pRunnable);
    result = lua_pcall(pRunnable->luaState, 1, 1, 0);

 	if (result != 0) {
		LOG(ERR, "lua_pcall failed  [%s]", lua_tostring(pRunnable->luaState,-1));
		stackdump(pRunnable->luaState);
	}else {
		result = lua_tointeger(pRunnable->luaState, -1);
		lua_remove(pRunnable->luaState, lua_gettop(pRunnable->luaState));
		if (result != 0) {
			LOG(WARN, "main lua function returned error %d", result);
		}
	}
 	//TODO : may be in case of lua error we should simply
 	//       delete and recreate luaState
	return result;
}

static void clusterMapResultHandler(void* luaContext, char* key, int status, dataStream_t data) {
	luaContext_t* pContext = (luaContext_t*)luaContext;
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(pContext->runnable);
	int result                   = 0;
	lua_rawgeti(pRunnable->luaState, LUA_REGISTRYINDEX, pContext->threadRef);
	luaL_unref(pRunnable->luaState, LUA_REGISTRYINDEX, pContext->threadRef);
	pContext->threadRef = LUA_NOREF;

	lua_State*  localLuaState = lua_tothread(pRunnable->luaState, -1);

	if (status == 0) {
		int   length = dataStreamGetSize(data);
		char* buffer = dataStreamToString(data);
		if (buffer) {
			lua_pushlstring(localLuaState, buffer, length);
			FREE(buffer);
		}else {
			lua_pushnil(localLuaState);
		}
	}else {
		lua_pushnil(localLuaState);
	}

	//now we need to resume the script
	result = lua_resume(localLuaState, 1);

	if (result != 0) {
		if (result == LUA_YIELD) {
			return;
		}else {
			LOG(ERR, "lua_resume failed  [%s]", lua_tostring(localLuaState,-1));
			stackdump(localLuaState);
		}
	}else {
		result = lua_tointeger(localLuaState, -1);
		lua_remove(localLuaState, lua_gettop(localLuaState));
		if (result != 0) {
			LOG(WARN, "main lua function returned error %d", result);
		}
	}
	//pop the thread from stack so that it can be garbage collected
	lua_remove(pRunnable->luaState, lua_gettop(pRunnable->luaState));
	onLuaResponseAvailable(((luaContext_t*)luaContext)->connection, result);
	return;
}


/**
 * In cluster mode each request gets a new lua thread to handle the request.
 * This again has some 8% overhead and we don't want to incur it, in case
 * cluster support is not required.
 */

int luaRunnableRun(luaRunnable_t runnable, connection_t connection, fallocator_t fallocator,
		 command_t* pCommand, int enableVirtualKey, int enableClusterMode) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	int                result    = 0;

	if (!enableClusterMode) {
		return luaRunnableRunNoCluster(pRunnable, connection, fallocator, pCommand, enableVirtualKey);
	}

	lua_State*  localLuaState = lua_newthread(pRunnable->luaState);

	if (enableVirtualKey) {
		lua_getglobal(localLuaState, "mainVirtualKey");
	}else {
		lua_getglobal(localLuaState, "mainNormal");
	}

	if (lua_isnil (localLuaState, 1)) {
		stackdump(pRunnable->luaState);
	}
	luaCommandNew(localLuaState, connection, fallocator, pCommand, pRunnable);
	LOG(ERR, "stackdump for local thread befor resume");
 			stackdump(localLuaState);
	LOG(ERR, "stackdump for main thread befor resume");
				stackdump(pRunnable->luaState);

    result = lua_resume(localLuaState, 1);

 	if (result != 0) {
 		LOG(ERR, "Result value not zero %d", result);
 		//this can only happen for getFromServer call
 		if (result == LUA_YIELD) {
 			LOG(ERR, "Yield from thread ");
 			stackdump(localLuaState);
 			return 1;
 		}else {
 			LOG(ERR, "Eror from thread %d", result);
 			LOG(ERR, "lua_resume failed  [%s]", lua_tostring(localLuaState,-1));
 			LOG(ERR, "stackdump for local thread");
 			stackdump(localLuaState);
 			LOG(ERR, "stackdump for main thread");
 			stackdump(pRunnable->luaState);
 			result = -1;
 		}
	}else {
		LOG(ERR, "return value from resume %d", result);
		result = lua_tointeger(localLuaState, -1);
		LOG(ERR, "return value on stack %d", result);
		LOG(ERR, "stackdump for local thread");
		stackdump(localLuaState);
		lua_remove(localLuaState, lua_gettop(localLuaState));
		if (result != 0) {
			LOG(WARN, "main lua function returned error %d", result);
		}
		LOG(ERR, "stackdump for main thread before poping out thread");
			stackdump(pRunnable->luaState);
	}
	lua_remove(pRunnable->luaState, lua_gettop(pRunnable->luaState));
	LOG(ERR, "stackdump for main thread");
	stackdump(pRunnable->luaState);
	return result;
}
