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

static int luaHashMapGetPrefixMatchingKeys(lua_State* L) {
	hashMap_t*   pHashMap = (hashMap_t*)lua_touserdata(L, 1);
	size_t l;
	const char *s = luaL_checklstring(L, -1, &l);
	if (s && l > 0) {
		u_int32_t count = 0;
		char*     keys  = 0;
		count = hashMapGetPrefixMatchingKeys(*pHashMap, (char*)s, &keys);
		LOG(DEBUG, "Got %d keys matching prefix %s", count, s);
		if (count > 0) {
			lua_createtable(L, count, 0);
			int newTable = lua_gettop(L);
			int index    = 1;
			int offset   = 0;
			while (count--) {
				char* key = keys+offset;
				int length = strlen(key);
				LOG(DEBUG, "Adding key %s to lua table.. offset %d", key, offset);
				lua_pushlstring(L, key, length);
				lua_rawseti(L, newTable, index);
				index++;
				offset += length + 1;
			}
			FREE(keys);
		}else {
			lua_pushnil(L);
		}
	}else {
		lua_pushnil(L);
	}
	return 1;
}

static const luaL_Reg hashmap_methods[] = {
    {"get",       luaHashMapGet},
    {"put",       luaHashMapPut},
    {"delete",    luaHashMapDelete},
    {"deleteLRU", luaHashMapDeleteLRU},
    {"getPrefixMatchingKeys", luaHashMapGetPrefixMatchingKeys},
    {NULL, NULL}
};

void luaHashMapRegister(lua_State* L) {
	luaL_register(L, "HashMap", hashmap_methods);
	lua_pushvalue(L,-1);
	lua_setfield(L, -2, "__index");
}

