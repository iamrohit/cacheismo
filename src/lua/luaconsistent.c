#include "luaconsistent.h"

int luaConsistentNew(lua_State* L) {
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

int luaConsistentDelete(lua_State* L) {
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

void luaConsistentRegister(lua_State* L) {
	luaL_register(L, "Consistent", consistent_methods);
	lua_pushvalue(L,-1);
	lua_setfield(L, -2, "__index");
}
