#ifndef LUA_BINDING_H_
#define LUA_BINDING_H_

#include "../common/common.h"
#include "../common/commands.h"
#include "../hashmap/hashmap.h"
#include "../io/connection.h"


typedef void* luaRunnable_t;

luaRunnable_t luaRunnableCreate(char* directory, int enableVirtualKey);
void          luaRunnableDelete(luaRunnable_t runnable);
void          luaRunnableGC(luaRunnable_t runnable);
int           luaRunnableRun(luaRunnable_t runnable, connection_t connection,
			    fallocator_t fallocator, command_t* pCommand, int enableVirtualKey,
			    int enableClusterMode);

#endif /* LUA_BINDING_H_ */
