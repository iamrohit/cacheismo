#ifndef LUA_BINDING_H_
#define LUA_BINDING_H_

#include "../common/common.h"
#include "../driver/commands.h"
#include "../hashmap/hashmap.h"
#include "../io/connection.h"


typedef void* luaRunnable_t;

luaRunnable_t luaRunnableCreate(char* scriptFile);
void          luaRunnableDelete(luaRunnable_t runnable);
void          luaRunnableGC(luaRunnable_t runnable);
int           luaRunnableRun(luaRunnable_t runnable, connection_t connection,
			     dataStream_t writeStream, fallocator_t fallocator,
			     command_t* pCommand, int enableVirtualKey);

#endif /* LUA_BINDING_H_ */
