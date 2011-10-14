#ifndef LUA_BINDING_H_
#define LUA_BINDING_H_

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>


#include "../common/common.h"
#include "../common/commands.h"
#include "../hashmap/hashmap.h"
#include "../io/connection.h"
#include "../cluster/clustermap.h"


/* The business of enableVirtualKey and enableClusterMode
 *
 * Both these arguments small optimizations that help a bit
 * with TPS. Dont enable them if you dont need them
 *
 * - by default cacheismo runs in memcached compatability mode
 * - when enableVirtualKey is enabled, cacheismo executes
 *   extra code which checks if the key is a virtual key or
 *   normal key. If you are using memcached compatability
 *   mode, this check is not required as it only adds extra
 *   processing which can be avoided.
 *
 *- In both the cases above a single lua thread is used for
 *- all queries. Since we only have a single query running
 *- in the system at a given time, this works.
 *-
 *- when enableClusterMode is used, every request will be
 *- checked for it is a virtual key and also every request
 *- is executed in a separate lua thread. If the request
 *- can be satisfied locally, this thread will be deleted
 *- as soon as the request is over, but if the request needs
 *- to make requests to other servers using getFromServer
 *- or getInParallel, we preserve the stack and restart the
 *- script when we get back response from server. Thus in
 *- clustered mode we might have multiple active queries
 *- with their own stacks running in the system. If you are
 *- not using the ability of cacheismo to query other
 *- server, dont enable the enableClusterMode flag. It will
 *- save some cpu cycles associated with creating and
 *- deleting lua threads.
 */

typedef void* luaRunnable_t;

typedef struct {
	fallocator_t  fallocator;
	lua_State*    luaState;
	clusterMap_t  clusterMap;
}luaRunnableImpl_t;



#define LUA_RUNNABLE(x) (luaRunnableImpl_t*)(x)


luaRunnable_t luaRunnableCreate(char* directory, int enableVirtualKey);
void          luaRunnableDelete(luaRunnable_t runnable);
void          luaRunnableGC(luaRunnable_t runnable);
int           luaRunnableRun(luaRunnable_t runnable, connection_t connection,
			    fallocator_t fallocator, command_t* pCommand, int enableVirtualKey,
			    int enableClusterMode);

#endif /* LUA_BINDING_H_ */
