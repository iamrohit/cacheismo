#include <dirent.h>

#include "binding.h"
#include "marshal.h"
#include "luaconsistent.h"
#include "luacacheitem.h"
#include "luahashmap.h"
#include "luacommand.h"

#include "../common/commands.h"
#include "../common/list.h"
#include "../cacheitem/cacheitem.h"
#include "../hashmap/hashmap.h"
#include "../io/connection.h"
#include "../cacheismo.h"

#include "../cluster/consistent.h"
#include "../cluster/clustermap.h"



#define LUA_CONFIG_FILE "config.lua"
#define LUA_CORE_FILE   "core.lua"

static void stackdump(lua_State* l)
{
    int i;
    int top = lua_gettop(l);

    LOG(ERR, "total items in stack %d\n",top);

    for (i = 1; i <= top; i++)
    {  /* repeat for each level */
        int t = lua_type(l, i);
        switch (t) {
            case LUA_TSTRING:  /* strings */
                LOG(ERR, "string: '%s'\n", lua_tostring(l, i));
                break;
            case LUA_TBOOLEAN:  /* booleans */
            	LOG(ERR, "boolean %s\n",lua_toboolean(l, i) ? "true" : "false");
                break;
            case LUA_TNUMBER:  /* numbers */
            	LOG(ERR, "number: %g\n", lua_tonumber(l, i));
                break;
            default:  /* other values */
            	LOG(ERR, "%s\n", lua_typename(l, t));
                break;
        }
    }
}


static int luaGetGlobalHashMap(lua_State* L) {
	hashMap_t* p = (hashMap_t*)lua_newuserdata(L, sizeof(hashMap_t));
   *p =  getGlobalHashMap();
   lua_getglobal(L, "HashMap");
   lua_setmetatable(L, -2);
   return 1;
}


static int luaSetGlobalLogLevel(lua_State* L) {
	int   level = lua_tointeger(L, 1);
	setGlobalLogLevel(level);
    return 0;
}

static int isCoreOrConfigFile(char* fileName) {
	if ((strcmp(fileName, LUA_CONFIG_FILE)) == 0 || (strcmp(fileName, LUA_CORE_FILE) == 0)) {
		return 1;
	}
	return 0;
}

static int isLuaFile(char* fileName) {
	int length = strlen(fileName);
	if ((length > 4) && (strcmp(".lua", fileName+(length-4)) == 0)) {
		return 1;
	}
	return 0;
}


static char *path_cat (const char *str1, char *str2, int addSlash) {
	char *result = 0;
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	result      = malloc(len1 + addSlash + len2 + 1);

	if (result) {
		strncpy (result, str1, len1);
		if (addSlash) {
			result[len1] = '/';
		}
        strncpy(result+len1+addSlash, str2, len2);
        result[len1+len2+addSlash] = '\0';
	}
	return result;
}

static int loadLuaFile(luaRunnableImpl_t* pRunnable, char* fileName) {
	int err = 0;
	err = luaL_loadfile(pRunnable->luaState, fileName);
	if (err != 0) {
		LOG(ERR, "Error %d in luaL_loadfile %s", err, fileName);
		LOG(ERR, "Message %s",lua_tostring(pRunnable->luaState,-1));
		return -1;
	}
	err = lua_pcall(pRunnable->luaState, 0, 0, 0);
	if (err != 0) {
		LOG(ERR, "Error %d in lua_pcall for file %s", err, fileName);
		LOG(ERR, "Message %s",lua_tostring(pRunnable->luaState,-1));
		return -1;
	}
	return 0;
}

static int loadDirectory (luaRunnableImpl_t* pRunnable, char* dir_path, int enableVirtualKey) {
	int            addSlash = dir_path[strlen(dir_path) -1] != '/';
    struct dirent *dp       = 0;
    DIR           *dir      = opendir(dir_path);
    char          *tmp      = 0;

    //first load the config and core files
    tmp  = path_cat(dir_path, LUA_CORE_FILE, addSlash);
    IfTrue(tmp, ERR, "Error allocating memory");
	IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
	free(tmp); tmp = 0;
    tmp  = path_cat(dir_path, LUA_CONFIG_FILE, addSlash);
    IfTrue(tmp, ERR, "Error allocating memory");
	IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
	free(tmp); tmp = 0;
	if (enableVirtualKey) {
		while ((dp = readdir(dir)) != NULL) {
			tmp = path_cat(dir_path, dp->d_name, addSlash);
			IfTrue(tmp, ERR, "Error allocating memory");
			if (isLuaFile(tmp) && (!isCoreOrConfigFile(tmp)) ) {
				IfTrue( 0 == loadLuaFile(pRunnable, tmp), ERR, "Error loading file %s", tmp);
				LOG(INFO, "Loaded file [%s]", tmp);
			}
			free(tmp);
			tmp = 0;
		}
	}
	goto OnSuccess;
OnError:
	if (tmp) {
		free(tmp);
		tmp = 0;
	}
OnSuccess:
	if (dir) {
		closedir(dir);
	}
	return 0;
}


static void* fallocatorBasedAlloc (void *ud, void *ptr, size_t osize, size_t nsize) {
	fallocator_t fallocator = ud;
	if (nsize == 0) {
		if (ptr) {
			fallocatorFree(fallocator, ptr);
		}
		return NULL;
	}else {
		if (osize == 0) {
			return fallocatorMalloc(fallocator, nsize);
		}else {
			void* newPtr = fallocatorMalloc(fallocator, nsize);
			if (newPtr) {
				int size = osize > nsize ? nsize : osize;
				memcpy(newPtr, ptr, size);
				if (ptr) {
					fallocatorFree(fallocator, ptr);
				}
				return newPtr;
			}
			return NULL;
		}
	}
}


luaRunnable_t luaRunnableCreate(char* directory, int enableVirtualKey) {
	luaRunnableImpl_t* pRunnable = ALLOCATE_1(luaRunnableImpl_t);
	pRunnable->fallocator = fallocatorCreate();
	pRunnable->luaState = lua_newstate(fallocatorBasedAlloc, pRunnable->fallocator);
	pRunnable->clusterMap = clusterMapCreate(clusterMapResultHandler);
	luaL_openlibs(pRunnable->luaState);
	lua_register(pRunnable->luaState, "getHashMap",       luaGetGlobalHashMap);
	lua_register(pRunnable->luaState, "setLogLevel",      luaSetGlobalLogLevel);
	lua_register(pRunnable->luaState, "newConsistent",    luaConsistentNew);
	lua_register(pRunnable->luaState, "deleteConsistent", luaConsistentDelete);

	//open the marshling library
	luaopen_marshal(pRunnable->luaState, pRunnable->fallocator);
	luaHashMapRegister(pRunnable->luaState);
	luaCacheItemRegister(pRunnable->luaState);

	luaConsistentRegister(pRunnable->luaState);

	if (0 == loadDirectory(pRunnable, directory, enableVirtualKey)) {
		return pRunnable;
	}
	luaRunnableDelete(pRunnable);
	return 0;
}


void luaRunnableDelete(luaRunnable_t runnable) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	if (pRunnable) {
		lua_close(pRunnable->luaState);
		fallocatorDelete(pRunnable->fallocator);
		FREE(pRunnable);
	}
}
/**
 * Periodically called by the event loop
 */
void luaRunnableGC(luaRunnable_t runnable) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	lua_gc(pRunnable->luaState, LUA_GCCOLLECT, 0);
}


/**
 * In non cluster mode we either run with virtual keys enabled or not.
 * When virtual keys are enabled, we load the script files and in case
 * of get requests, we check if the first two tokens in the key
 * correspond to some script/function pair. This adds may be 5%
 * overhead which can be avoided if virtual key support is not
 * required.
 *
 */
static int luaRunnableRunNoCluster(luaRunnableImpl_t* pRunnable, connection_t connection,
		 fallocator_t fallocator, command_t* pCommand,
		 int enableVirtualKey) {

	int result = 0;

	if (enableVirtualKey) {
		lua_getglobal(pRunnable->luaState, "mainVirtualKey");
	}else {
		lua_getglobal(pRunnable->luaState, "mainNormal");
	}
	luaCommandNew(pRunnable->luaState, connection, fallocator, pCommand, pRunnable);
    result = lua_pcall(pRunnable->luaState, 1, 1, 0);

 	if (result != 0) {
		LOG(ERR, "lua_pcall failed  [%s]", lua_tostring(pRunnable->luaState,-1));
		stackdump(pRunnable->luaState);
	}else {
		result = lua_tointeger(pRunnable->luaState, -1);
		lua_remove(pRunnable->luaState, lua_gettop(pRunnable->luaState));
		if (result != 0) {
			LOG(WARN, "main lua function returned error %d", result);
		}
	}
 	//TODO : may be in case of lua error we should simply
 	//       delete and recreate luaState
	return result;
}


/**
 * In cluster mode each request gets a new lua thread to handle the request.
 * This again has some 8% overhead and we don't want to incur it, in case
 * cluster support is not required.
 */

int luaRunnableRun(luaRunnable_t runnable, connection_t connection, fallocator_t fallocator,
		 command_t* pCommand, int enableVirtualKey, int enableClusterMode) {
	luaRunnableImpl_t* pRunnable = LUA_RUNNABLE(runnable);
	int                result    = 0;

	if (!enableClusterMode) {
		return luaRunnableRunNoCluster(pRunnable, connection, fallocator, pCommand, enableVirtualKey);
	}

	lua_State*  localLuaState = lua_newthread(pRunnable->luaState);

	if (enableVirtualKey) {
		lua_getglobal(localLuaState, "mainVirtualKey");
	}else {
		lua_getglobal(localLuaState, "mainNormal");
	}

	if (lua_isnil (localLuaState, 1)) {
		stackdump(pRunnable->luaState);
	}
	luaCommandNew(localLuaState, connection, fallocator, pCommand, pRunnable);
	LOG(ERR, "stackdump for local thread befor resume");
 			stackdump(localLuaState);
	LOG(ERR, "stackdump for main thread befor resume");
				stackdump(pRunnable->luaState);

    result = lua_resume(localLuaState, 1);

 	if (result != 0) {
 		LOG(ERR, "Result value not zero %d", result);
 		//this can only happen for getFromServer call
 		if (result == LUA_YIELD) {
 			LOG(ERR, "Yield from thread ");
 			stackdump(localLuaState);
 			return 1;
 		}else {
 			LOG(ERR, "Eror from thread %d", result);
 			LOG(ERR, "lua_resume failed  [%s]", lua_tostring(localLuaState,-1));
 			LOG(ERR, "stackdump for local thread");
 			stackdump(localLuaState);
 			LOG(ERR, "stackdump for main thread");
 			stackdump(pRunnable->luaState);
 			result = -1;
 		}
	}else {
		LOG(ERR, "return value from resume %d", result);
		result = lua_tointeger(localLuaState, -1);
		LOG(ERR, "return value on stack %d", result);
		LOG(ERR, "stackdump for local thread");
		stackdump(localLuaState);
		lua_remove(localLuaState, lua_gettop(localLuaState));
		if (result != 0) {
			LOG(WARN, "main lua function returned error %d", result);
		}
		LOG(ERR, "stackdump for main thread before poping out thread");
			stackdump(pRunnable->luaState);
	}
	lua_remove(pRunnable->luaState, lua_gettop(pRunnable->luaState));
	LOG(ERR, "stackdump for main thread");
	stackdump(pRunnable->luaState);
	return result;
}
