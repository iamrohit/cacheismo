
#include "luacacheitem.h"
#include "../cacheismo.h"

int luaCacheItemNew(lua_State* L, cacheItem_t item) {
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

void luaCacheItemRegister(lua_State* L) {
	luaL_register(L, "CacheItem", cacheitem_methods);
	lua_pushvalue(L,-1);
	lua_setfield(L, -2, "__index");
}

