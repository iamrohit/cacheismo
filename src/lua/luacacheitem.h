#ifndef LUACACHEITEM_H_
#define LUACACHEITEM_H_

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../cacheitem/cacheitem.h"


int  luaCacheItemNew(lua_State* L, cacheItem_t item);
void luaCacheItemRegister(lua_State* L);

#endif /* LUACACHEITEM_H_ */
