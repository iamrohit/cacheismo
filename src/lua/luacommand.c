

#include "luacommand.h"
#include "../cacheismo.h"

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


int luaCommandNew(lua_State* L, connection_t connection, fallocator_t fallocator,
		command_t *pCommand, luaRunnable_t runnable) {
   luaContext_t* context = (luaContext_t*)lua_newuserdata(L, sizeof(luaContext_t));
   context->pCommand     = pCommand;
   context->connection   = connection;
   context->fallocator   = fallocator;
   context->runnable     = runnable;
   context->threadRef    = LUA_NOREF;
   context->multiContext = 0;
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
  //  {"getFromServer",  luaCommandGetValueFromExternalServer},
  //  {"getInParallel",  luaCommandGetMultipleValuesFromExternalServers},
    {NULL, NULL}
};

void luaCommandRegister(lua_State* L) {
	luaL_register(L, "Command", command_methods);
	lua_pushvalue(L,-1);
	lua_setfield(L, -2, "__index");
}

