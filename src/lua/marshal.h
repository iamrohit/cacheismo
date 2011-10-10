#ifndef LUA_MARSHAL_H_
#define LUA_MARSHAL_H_
#include "../common/common.h"
#include "../fallocator/fallocator.h"

int luaopen_marshal(lua_State *L, fallocator_t fallocator);

#endif /* LUA_MARSHAL_H_ */
