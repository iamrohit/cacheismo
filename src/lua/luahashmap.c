#include "luahashmap.h"
#include "../hashmap/hashmap.h"
#include "../cacheitem/cacheitem.h"
#include "luacacheitem.h"

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


static const luaL_Reg hashmap_methods[] = {
    {"get",       luaHashMapGet},
    {"put",       luaHashMapPut},
    {"delete",    luaHashMapDelete},
    {"deleteLRU", luaHashMapDeleteLRU},
    {NULL, NULL}
};

void luaHashMapRegister(lua_State* L) {
	luaL_register(L, "HashMap", hashmap_methods);
	lua_pushvalue(L,-1);
	lua_setfield(L, -2, "__index");
}

