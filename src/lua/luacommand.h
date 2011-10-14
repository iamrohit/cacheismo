#ifndef LUACOMMAND_H_
#define LUACOMMAND_H_

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "../io/connection.h"
#include "../common/commands.h"
#include "binding.h"
#include "luaclustermap.h"

typedef struct luaContext_t {
	connection_t     connection;
	fallocator_t     fallocator;
	command_t*       pCommand;
	luaRunnable_t    runnable;
	int              threadRef;
	multiContext_t*  multiContext;
} luaContext_t;

int luaCommandNew(lua_State* L, connection_t connection, fallocator_t fallocator,
		command_t *pCommand, luaRunnable_t runnable);

void luaCommandRegister(lua_State* L);


#endif /* LUACOMMAND_H_ */
