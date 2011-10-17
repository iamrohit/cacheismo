#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "luaclustermap.h"
#include "../common/list.h"
#include "../cacheismo.h"
#include "luacommand.h"


/* for handling parallel gets we need to keep track of
 * what was asked by the script and have we got all
 * of it.
 */
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

static multiContext_t* multiContextCreate() {
	multiContext_t* pMContext = ALLOCATE_1(multiContext_t);
	if (pMContext) {
		pMContext->serverList = listCreate(OFFSET(serverEntry_t, pNext), OFFSET(serverEntry_t, pPrev));
		if (!pMContext->serverList) {
			FREE(pMContext);
			pMContext = 0;
		}
	}
	return pMContext;
}

static void cleanupAndDeleteServerList(serverEntry_t* pServerEntry) {
	if (pServerEntry) {
		if (pServerEntry->keyValueList) {
			keyValue_t* pKeyValue = listGetFirst(pServerEntry->keyValueList);
			while (pKeyValue != 0) {
				keyValue_t* pNext = listGetNext(pServerEntry->keyValueList, pKeyValue);
				FREE(pKeyValue->key);
				pKeyValue->key = 0;
				if (pKeyValue->value) {
					FREE(pKeyValue->value);
					pKeyValue->value = 0;
				}
				FREE(pKeyValue);
				pKeyValue = pNext;
			}
			listFree(pServerEntry->keyValueList);
			pServerEntry->keyValueList = 0;
		}
		if (pServerEntry->serverName) {
			FREE(pServerEntry->serverName);
			pServerEntry->serverName = 0;
		}
		FREE(pServerEntry);
	}
}

static void multiContextDelete(multiContext_t* pMContext) {
	if (pMContext) {
		LOG(DEBUG, "total keys %d callback received %d", pMContext->total, pMContext->received);
		if (pMContext->serverList) {
			serverEntry_t* pServerEntry = listGetFirst(pMContext->serverList);
			while (pServerEntry != 0) {
				serverEntry_t* pNext = listGetNext(pMContext->serverList, pServerEntry);
				cleanupAndDeleteServerList(pServerEntry);
				pServerEntry = pNext;
			}
			listFree(pMContext->serverList);
			pMContext->serverList = 0;
		}
		FREE(pMContext);
	}
}

static serverEntry_t* multiGetServerEntry(multiContext_t* pMContext, char* serverName) {
	if (!pMContext || !serverName) {
		return 0;
	}
	serverEntry_t* pServerEntry = listGetFirst(pMContext->serverList);
	while (pServerEntry != 0) {
		if (0 == strcmp(serverName, pServerEntry->serverName)) {
			return pServerEntry;
		}
		pServerEntry = listGetNext(pMContext->serverList, pServerEntry);
	}
	//entry doesn't exist for this server
	pServerEntry = ALLOCATE_1(serverEntry_t);
	if (pServerEntry) {
		pServerEntry->serverName   = strdup(serverName);
		pServerEntry->keyValueList = listCreate(OFFSET(keyValue_t, pNext), OFFSET(keyValue_t, pPrev));
		//TODO - check for out of memory condition
		listAddFirst(pMContext->serverList, pServerEntry);
	}
	return pServerEntry;
}

static keyValue_t* multiAddNewKey(multiContext_t* pMContext, char* serverName, char* key) {
	keyValue_t*     pKeyValue    = 0;
	serverEntry_t*  pServerEntry = 0;

	IfTrue(pMContext && serverName && key, WARN, "Null arguments");
	pKeyValue = ALLOCATE_1(keyValue_t);
	IfTrue(pKeyValue, WARN, "Error allocating memory");
	pKeyValue->key = strdup(key);
	IfTrue(pKeyValue->key, WARN, "Error copying key");
	pServerEntry = multiGetServerEntry(pMContext, serverName);
	IfTrue(pServerEntry, WARN, "Error getting server entry");
	listAddFirst(pServerEntry->keyValueList, pKeyValue);
	pMContext->total++;
	goto OnSuccess;
OnError:
	if (pKeyValue) {
		if (pKeyValue->key) {
			FREE(pKeyValue->key);
		}
		FREE(pKeyValue);
		pKeyValue = 0;
	}
OnSuccess:
	return pKeyValue;
}

int luaCommandGetValueFromExternalServer(lua_State* L) {
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

	//current running thread is always present at the top of the
	//main lua stack
	context->threadRef    = luaL_ref(pRunnable->luaState, LUA_REGISTRYINDEX);
	if (context->multiContext) {
		LOG(ERR, "multi context not NULL..it should be");
		context->multiContext = 0;
	}

	IfTrue(0 >  clusterMapGet(pRunnable->clusterMap, context, NULL, server, key),
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


/* reads the stack and populates the multi-context data strcuture
 * which can be later used to sumbit the requests to the
 * cluster map
 */

static multiContext_t* createMultiContextFromStack(lua_State* L) {
	multiContext_t* pMContext = multiContextCreate();
	lua_pushvalue(L, -1);
	lua_pushnil(L);

	while (lua_next(L, -2))  {
		lua_pushvalue(L, -2);
		const char *serverName = lua_tostring(L, -1);
		if (lua_istable(L, -2)) {
			lua_pushvalue(L, -2);
			lua_pushnil(L);
			while (lua_next(L, -2))  {
				lua_pushvalue(L, -2);
				const char *arrayIndex = lua_tostring(L, -1);
				const char *virtualKey = lua_tostring(L, -2);
				IfTrue(multiAddNewKey(pMContext, (char*)serverName, (char*)virtualKey),
						ERR, "Error adding key to multiContext %s %s %s", arrayIndex, virtualKey, serverName);
				lua_pop(L, 2);
			}
			lua_pop(L, 1);
		}else {
			const char* virtualKey = lua_tostring(L, -2);
			IfTrue(multiAddNewKey(pMContext, (char*)serverName, (char*)virtualKey),
					ERR, "Error adding key to multicontext");
		}
		lua_pop(L, 2);
	}
	lua_pop(L, 1);
	goto OnSuccess;
OnError:
	if (pMContext) {
		multiContextDelete(pMContext);
		pMContext = 0;
	}
OnSuccess:
	return pMContext;
}


/*
 * We expect a table on the stack with server names as key
 * and the corresponding value is either a string (single key)
 * or a table as array with values as set of keys to fetch
 * from a single server.
 *
 *   server1 : {key1, key2}
 *   server2 : {1: key3, 2: key4, }
 *
 *
 *
 *   The final result is of the form
 *   server1 : key1 => value1, key2 => value2
 *   server2 : key3 => value3, key4 => value4
 *
 *   A table of server names as keys which internally
 *   has another table as value. This nested table has
 *   virtual key as the key and value as whatever is
 *   returned from the server.
 *   If server doesn't returns value for some key, or
 *   some error occurs, the corresponding value will be
 *   nil.
 */


int luaCommandGetMultipleValuesFromExternalServers(lua_State* L) {
	luaContext_t*      context       = (luaContext_t*) lua_touserdata(L, 1);
	luaRunnableImpl_t* pRunnable     = 0;
	int                result        = 0;
	serverEntry_t*     pServerEntry  = 0;
	keyValue_t*        pKeyValue     = 0;

	IfTrue(context, ERR, "Null context from lua stack");
	IfTrue(!context->multiContext, ERR, "MultiContext not Null in the lua context");

	pRunnable = context->runnable;
	context->multiContext = createMultiContextFromStack(L);
	context->threadRef = luaL_ref(pRunnable->luaState, LUA_REGISTRYINDEX);

	LOG(DEBUG, "Trying to submit %d parallel requests threadref %d ", context->multiContext->total, context->threadRef);

	pServerEntry = listGetFirst(context->multiContext->serverList);
	while (pServerEntry != 0) {
		pKeyValue = listGetFirst(pServerEntry->keyValueList);
		while (pKeyValue != 0) {
			result = clusterMapGet(pRunnable->clusterMap, context, pKeyValue, pServerEntry->serverName, pKeyValue->key);
			if (result < 0) {
				LOG(INFO, "Error submitting request for key %s to server %s", pKeyValue->key, pServerEntry->serverName);
				//we will not get that many callbacks
				context->multiContext->received++;
			}
			pKeyValue = listGetNext(pServerEntry->keyValueList, pKeyValue);
		}
		pServerEntry = listGetNext(context->multiContext->serverList, pServerEntry);
	}
	if (context->multiContext->total <= context->multiContext->received) {
		LOG(ERR, "We were not able to submit even a single request..");
		goto OnError;
	}
	goto OnSuccess;
OnError:
	result = -1;
	if (pRunnable) {
		//restore thread on the main stack
		lua_rawgeti(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		luaL_unref(pRunnable->luaState, LUA_REGISTRYINDEX, context->threadRef);
		context->threadRef = LUA_NOREF;
		if (context->multiContext) {
			multiContextDelete(context->multiContext);
			context->multiContext = 0;
		}
		lua_pushnil(L);
	}
OnSuccess:
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

static void pushMultiResultOnStack(lua_State* L, multiContext_t* pMContext) {
	int n = listGetSize(pMContext->serverList);
	lua_createtable(L, 0, n);
	serverEntry_t* pServerEntry = listGetFirst(pMContext->serverList);
	keyValue_t*    pKeyValue    = 0;

	while (pServerEntry) {
		lua_pushstring(L, pServerEntry->serverName);
		int noofkeys = listGetSize(pServerEntry->keyValueList);
		lua_createtable(L, 0, noofkeys);
		pKeyValue = listGetFirst(pServerEntry->keyValueList);
		while (pKeyValue) {
			lua_pushstring(L, pKeyValue->key);
			if (pKeyValue->value && (pKeyValue->length > 0)) {
				lua_pushlstring(L, pKeyValue->value, pKeyValue->length);
			}else {
				lua_pushnil(L);
			}
			lua_settable(L, -3);
			pKeyValue = listGetNext(pServerEntry->keyValueList, pKeyValue);
		}
		lua_settable(L, -3);
		pServerEntry = listGetNext(pMContext->serverList, pServerEntry);
	}
}


void clusterMapResultHandler(void* luaContext, void* keyContext, int status, dataStream_t data) {
	luaContext_t*      pContext  = (luaContext_t*)luaContext;
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(pContext->runnable);
	int                result    = 0;
	int                multi    = 0;

	if (keyContext && pContext->multiContext) {
		multi = 1;
		keyValue_t* pKeyValue = (keyValue_t*)keyContext;

		if (pKeyValue->value) {
			LOG(ERR, "Value is already set for this key..did we got multiple callbacks? ");
		}
		if ((status == 0) && data) {
			int   length = dataStreamGetSize(data);
			char* buffer = dataStreamToString(data);
			if (buffer) {
				pKeyValue->value  = buffer;
				pKeyValue->length = length;
			}else {
				pKeyValue->value = 0;
			}
		}else {
			pKeyValue->value = 0;
		}
		pContext->multiContext->received++;
		if (pContext->multiContext->total > pContext->multiContext->received) {
			return;
		}
	}

	lua_rawgeti(pRunnable->luaState, LUA_REGISTRYINDEX, pContext->threadRef);
	luaL_unref(pRunnable->luaState, LUA_REGISTRYINDEX, pContext->threadRef);
	pContext->threadRef = LUA_NOREF;

	lua_State*  localLuaState = lua_tothread(pRunnable->luaState, -1);
	//setup stack
	if (!multi) {
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
	}else {
		//setup stack for the multi case
		pushMultiResultOnStack(localLuaState, pContext->multiContext);
		multiContextDelete(pContext->multiContext);
		pContext->multiContext = 0;
	}

	//now we need to resume the script
	result = lua_resume(localLuaState, 1);

	if (result != 0) {
		if (result == LUA_YIELD) {
			return;
		}else {
			LOG(ERR, "lua_resume failed  [%s]", lua_tostring(localLuaState,-1));
			//stackdump(localLuaState);
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

