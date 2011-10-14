#ifndef LUACONSISTENT_H_
#define LUACONSISTENT_H_

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../cluster/consistent.h"

int  luaConsistentNew(lua_State* L);
int  luaConsistentDelete(lua_State* L);
void luaConsistentRegister(lua_State* L);

#endif /* LUACONSISTENT_H_ */
