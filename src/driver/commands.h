#ifndef COMMANDS_H_
#define COMMANDS_H_

#include "../common/common.h"
#include "../io/datastream.h"
#include "../fallocator/fallocator.h"

enum commands_enum_t {
	COMMAND_GET = 1,
	COMMAND_BGET,
	COMMAND_ADD,
	COMMAND_SET,
	COMMAND_REPLACE,
	COMMAND_PREPEND,
	COMMAND_APPEND,
	COMMAND_CAS,
	COMMAND_INCR,
	COMMAND_DECR,
	COMMAND_GETS,
	COMMAND_DELETE,
	COMMAND_STATS,
	COMMAND_FLUSH_ALL,
	COMMAND_VERSION,
	COMMAND_QUIT,
	COMMAND_VERBOSITY
};

enum response_enum_t {
	RESPONSE_GET_SUCCESS = 1,
	RESPONSE_GET_FAILURE,
	RESPONSE_STORED,
	RESPONSE_NOT_STORED,
	RESPONSE_EXISTS,
    RESPONSE_NOT_FOUND,
    RESPONSE_DELETED,
    RESPONSE_COMMAND_ERROR,
    RESPONSE_CLIENT_ERROR,
    RESPONSE_SERVER_ERROR
};

typedef struct {
	enum commands_enum_t command;
	char*                key;
	u_int32_t            keySize;
	u_int64_t            cas;
	u_int64_t            delta;
	u_int32_t            noreply;
	u_int32_t            flags;
	u_int32_t            expiryTime;
	u_int32_t            dataLength;
	dataStream_t         dataStream;
	char**               multiGetKeys;
	int                  multiGetKeysCount;
	enum response_enum_t response;
	void*                cacheItem;
} command_t;

int   commandExecute();

void  commandDelete(fallocator_t fallocator, command_t* command);

#endif /* COMMANDS_H_ */
