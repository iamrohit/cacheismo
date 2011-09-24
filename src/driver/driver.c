#include "../common/common.h"
#include "../io/connection.h"
#include "../hashmap/hashmap.h"
#include "../chunkpool/chunkpool.h"
#include "../cacheitem/cacheitem.h"
#include "../driver/parser.h"
#include "../lua/binding.h"
#include "driver.h"

int logLevel = 3;

typedef struct global_t {
	u_int32_t          port;
	u_int32_t          pageCount;
	char*              interface;
	char*              scriptsDirectory;
	int                enableVirtualKeys;
	u_int32_t          ioBufferCount;
	connection_t       server;
	chunkpool_t        chunkpool;
	struct event_base *base;
	hashMap_t          hashMap;
	luaRunnable_t      runnable;
	struct event*      timer;
}global_t;


typedef struct connectionContext_t {
	bool          isWriting;
	connection_t  connection;
	dataStream_t  readStream;
	dataStream_t  writeStream;
	parser_t      parser;
	fallocator_t  fallocator;
} connectionContext_t;

global_t ENV;

void  setGlobalLogLevel(int level) {
	if ((level <= 3)  && (level >= 0)) {
		logLevel = level;
	}
}

void connectionContextDelete(connectionContext_t* pContext) {
	if (pContext) {
		if (pContext->readStream) {
			dataStreamDelete(pContext->readStream);
			pContext->readStream = 0;
		}
		if (pContext->writeStream) {
			dataStreamDelete(pContext->writeStream);
			pContext->writeStream = 0;
		}
		if (pContext->parser) {
			parserDelete(pContext->parser);
			pContext->parser = 0;
		}
		if (pContext->fallocator) {
			fallocatorDelete(pContext->fallocator);
		}
		FREE(pContext);
	}
}

connectionContext_t* connectionContextCreate(connection_t conn) {
	connectionContext_t* pContext = ALLOCATE_1(connectionContext_t);

	IfTrue(conn, ERR, "Null Connection");
	IfTrue(pContext, ERR, "Error allocating memory");

	pContext->readStream = dataStreamCreate();
	IfTrue(pContext->readStream, WARN, "Error creating read stream");
	pContext->writeStream = dataStreamCreate();
	IfTrue(pContext->writeStream, WARN, "Error creating write stream");
	pContext->fallocator = fallocatorCreate();
	IfTrue(pContext->fallocator, WARN, "Error creating fallocator");
	pContext->parser = parserCreate(pContext->fallocator);
	IfTrue(pContext->parser, WARN, "Error creating parser");
	pContext->connection = conn;
	goto OnSuccess;
OnError:
	if (pContext) {
		connectionContextDelete(pContext);
		pContext = 0;
	}
OnSuccess:
	return pContext;
}

hashMap_t   getGlobalHashMap() {
	return ENV.hashMap;
}

chunkpool_t getGlobalChunkpool() {
	return ENV.chunkpool;
}

static void newConnectionImpl(connection_t connection) {
	LOG(DEBUG, "got a new connection %p", connection);
	if (connection) {
		connectionContext_t* pContext = connectionContextCreate(connection);
		if (pContext) {
			connectionSetContext(connection, pContext);
			connectionWaitForRead(connection, ENV.base);
		}else {
			LOG(DEBUG, "Error creating new connection context. Closing connection");
			connectionClose(connection);
		}
	}
}

cacheItem_t  createCacheItemFromCommand(command_t* pCommand) {
	cacheItem_t item = cacheItemCreate(ENV.chunkpool, pCommand);
	if (!item) {
		int freeSize = 2 * cacheItemEstimateSize(pCommand);
		while (!item && (freeSize < 2 * 1024 * 1024)) {
			hashMapDeleteLRU(ENV.hashMap, freeSize);
			item = cacheItemCreate(ENV.chunkpool, pCommand);
			if (!item) {
				freeSize = 2 * freeSize;
			}
		}
	}
	return item;
}

#define MAX_APPEND_DATA_SIZE 512

/* int appendError needs to be defined by the caller  */

#define APPEND_DATA(conn, fallo, stream, format, ...)                  \
	{                                                           \
		char       temp[MAX_APPEND_DATA_SIZE];                  \
		int        length = 0;                                  \
		u_int32_t  offset = 0;                                  \
		void*      buffer = 0;                                  \
		                                                        \
		length = snprintf(temp, MAX_APPEND_DATA_SIZE, format, ## __VA_ARGS__); \
		buffer = connectionGetBuffer(conn, fallo, length, &offset);    \
		if (buffer) {                                           \
			memcpy((char*)buffer+offset, temp, length);         \
			dataStreamAppendData(stream, buffer, offset, length);\
		}else {                                                 \
			LOG(WARN, "\nAPPEND_DATA failed \n");               \
			appendError = -1;                                   \
		}                                                       \
	}                                                           \


int writeCacheItemToStream(connection_t conn, cacheItem_t item) {
	int appendError = 0;
	connectionContext_t* pContext  = connectionGetContext(conn);

	APPEND_DATA(conn, pContext->fallocator, pContext->writeStream, "VALUE %s %d %d\r\n",
												  cacheItemGetKey(item),
												  cacheItemGetFlags(item),
												  cacheItemGetDataLength(item));
	IfTrue(appendError == 0, WARN, "Error appending");
	IfTrue(0 == dataStreamAppendDataStream(pContext->writeStream, cacheItemGetDataStream(item)),
			WARN, "Error appending stream");
	APPEND_DATA(pContext->connection, pContext->fallocator,  pContext->writeStream, "\r\n");
	IfTrue(appendError == 0, WARN, "Error appending");
	goto OnSuccess;
OnError:
	appendError = -1;
OnSuccess:
	return appendError;

}

int writeRawStringToStream(connection_t connection, char* value, int length) {
	connectionContext_t* pContext  = connectionGetContext(connection);
	u_int32_t  offset = 0;
	void*      buffer = 0;

	buffer = connectionGetBuffer(connection, pContext->fallocator, length, &offset);
	if (buffer) {
		memcpy((char*)buffer+offset, value, length);
		return dataStreamAppendData(pContext->writeStream, buffer, offset, length);
	}
	return -1;
}


static int handleCommandLUA(connectionContext_t* pContext, command_t* pCommand) {
	return luaRunnableRun(ENV.runnable, pContext->connection, pContext->writeStream,
			pContext->fallocator, pCommand, ENV.enableVirtualKeys);
}


static int completeWrite(connectionContext_t* pContext) {
	u_int32_t size    = dataStreamGetSize(pContext->writeStream);
	u_int32_t written = 0;
	int       err     = 0;
	if (size > 0) {
		err =  connectionWrite(pContext->connection, pContext->fallocator, pContext->writeStream,size, &written);
		if (err < 0) {
			return err;
		}
		if (written > 0) {
			dataStreamTruncateFromStart(pContext->writeStream, (size - written));
		}
		return err;
	}
	return 0;
}

// forward declaration...is called form write available
static void readAvailableImpl(connection_t connection);

static void writeAvailableImpl(connection_t connection) {
	connectionContext_t* pContext  = connectionGetContext(connection);
    int err = completeWrite(pContext);
    if (err < 0) {
    	connectionClose(pContext->connection);
    	pContext->connection = 0;
    	connectionContextDelete(pContext);
    }else {
    	if (err == 0) {
    		//write compelete
    		pContext->isWriting = false;
    		if (dataStreamGetSize(pContext->readStream) > 0) {
    			readAvailableImpl(connection);
    		}else {
    			connectionWaitForRead(pContext->connection, ENV.base);
    		}
    	}else {
    		connectionWaitForWrite(pContext->connection, ENV.base);
    	}
    }
}

static void readAvailableImpl(connection_t connection){
	connectionContext_t* pContext    = connectionGetContext(connection);
	u_int32_t            bytesRead   = 0;
	int                  returnValue = 0;

doParsing:
	returnValue = connectionRead(pContext->connection, pContext->fallocator, pContext->readStream, 8 * 1024 , &bytesRead);
	IfTrue(returnValue >= 0, INFO, "Socket closed");

	returnValue = parserParse(pContext->parser, pContext->readStream);
	IfTrue(returnValue >= 0, INFO, "Parsing Error %d", returnValue);

	if (returnValue == 1) {
		connectionWaitForRead(pContext->connection, ENV.base);
		goto OnSuccess;
	}

	command_t* pCommand = parserGetCommandAndReset(pContext->parser, pContext->readStream);
	IfTrue(pCommand, INFO, "Error getting command from parser");
	returnValue = handleCommandLUA(pContext, pCommand);
	if (pCommand) {
		commandDelete(pContext->fallocator, pCommand);
		pCommand = 0;
	}
	if (returnValue < 0) {
		LOG(INFO, "return value from lua %d", returnValue);
		goto OnError;
	}
	if (dataStreamGetSize(pContext->writeStream) > 0) {
		pContext->isWriting = true;
		int err = completeWrite(pContext);
		IfTrue(err >= 0, INFO, "Error writing response");
		if (err == 0) {
			pContext->isWriting = false;
			if (dataStreamGetSize(pContext->readStream) > 0) {
				goto doParsing;
			}else {
				connectionWaitForRead(pContext->connection, ENV.base);
			}
		}else {
			connectionWaitForWrite(pContext->connection, ENV.base);
		}
	}else {
		if (dataStreamGetSize(pContext->readStream) > 0) {
			goto doParsing;
		}else {
			connectionWaitForRead(pContext->connection, ENV.base);
		}
	}
	goto OnSuccess;
OnError:
	if (pContext) {
		connectionClose(pContext->connection);
		pContext->connection = 0;
		connectionContextDelete(pContext);
		pContext = 0;
	}
OnSuccess:
	return;
}

static connectionHandler_t* createGlobalConnectionHandler() {
	connectionHandler_t* pGCH = ALLOCATE_1(connectionHandler_t);
	pGCH->newConnection  = &newConnectionImpl;
	pGCH->readAvailable  = &readAvailableImpl;
	pGCH->writeAvailable = &writeAvailableImpl;
	return pGCH;
}



static void timerCallback(evutil_socket_t ignore, short events, void *ptr)
{

    hashMapDeleteExpired(ENV.hashMap);
    chunkpoolGC(ENV.chunkpool);
    luaRunnableGC(ENV.runnable);
/*
    u_int32_t count   = hashMapSize(ENV.hashMap);
    if (count == 0) {
    	count = 1;
    }
    u_int64_t usedMem = chunkpoolMemoryUsed(ENV.chunkpool);
    printf("Memory %lu m Items %d PerItem %lu b\n",
    		(usedMem/(1024 * 1024)), count, (usedMem/count));
*/
    struct timeval  one_sec = { 1 , 0 };
    event_add(ENV.timer, &one_sec);
}



static void usage() {
	printf("valid options are \n\n");
	printf("-h    <prints help information>                    \n");
	printf("-m    <Memory in MB>           default <64MB>      \n");
	printf("-p    <port number>            default <11211>     \n");
	printf("-l    <ip address>             default <0.0.0.0>   \n");
	printf("-d    <scripts directory>      default <./scripts> \n");
	printf("-e    <enable virtual keys>    default <Disabled>  \n");
	printf("-i    <IO Memory Cache in MB>  default <16MB>      \n");
	printf("-v    <Log Level debug(0), info(1), warn(2), err(3)>   default <err(3)> \n");
	exit(1);
}


static int parseArgs(int argc, char** argv) {
	int c = 0;
	ENV.port              = 11211;
	ENV.pageCount         = 64 * (1024/4);
	ENV.interface         = "0.0.0.0";
	ENV.scriptsDirectory  = "./scripts";
	ENV.enableVirtualKeys = 0;
	ENV.ioBufferCount     = 16 * (1024/4);

    while (-1 != (c = getopt(argc, argv,
          "p:"  /* TCP port number to listen on */
          "m:"  /* max memory to use for items in megabytes */
          "l:"  /* interface to listen on */
    	  "d:"  /* scripts directory */
    	  "e"	/* enable virtual keys */
    	  "i:"	/* IO memory cache size */
    	  "v:"	/* logging level */
    	  "h"	/* help information */
        ))) {
        switch (c) {
        case 'h':
        	usage();
        	break;
        case 'p':
            ENV.port = atoi(optarg);
            break;
        case 'm':
            ENV.pageCount = atoi(optarg) * (1024 / 4);
            break;
        case 'l':
            ENV.interface = strdup(optarg);
            break;
        case 'd':
			ENV.scriptsDirectory = strdup(optarg);
			break;
        case 'e':
			ENV.enableVirtualKeys = 1;
			break;
        case 'i':
			ENV.ioBufferCount = atoi(optarg) * (1024 / 4);
			break;
        case 'v':
        {
        	int level = atoi(optarg);
        	setGlobalLogLevel(level);
        	break;
        }
         default:
            fprintf(stderr, "Illegal argument \"%c\"\n", c);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
	struct timeval  one_sec = { 1 , 0 };

	IfTrue( 0 == parseArgs(argc, argv), ERR, "Error parsing options");
	event_init();
	fallocatorInit(ENV.ioBufferCount);

	ENV.chunkpool   = chunkpoolCreate(ENV.pageCount);
	IfTrue(ENV.chunkpool, ERR, "Error creating chunkpool for size %d", (ENV.pageCount * 4096));

	ENV.base        = event_base_new();
	ENV.server      = connectionCreate(ENV.port, ENV.interface, createGlobalConnectionHandler());
	IfTrue(ENV.server, ERR, "Error opening %s port %d", ENV.interface, ENV.port);

	ENV.hashMap     = hashMapCreate(cacheItemGetHashEntryAPI(ENV.chunkpool));
	IfTrue(ENV.server, ERR, "Error creating hashMap");

	ENV.runnable    = luaRunnableCreate(ENV.scriptsDirectory, ENV.enableVirtualKeys);
	IfTrue(ENV.runnable, ERR, "Error setting up lua environment [%s]", ENV.scriptsDirectory);

	connectionWaitForRead(ENV.server, ENV.base);

	ENV.timer        = evtimer_new(ENV.base, timerCallback, NULL);

	event_add(ENV.timer, &one_sec);
	event_base_dispatch(ENV.base);
	goto OnSuccess;
OnError:
	usage();
OnSuccess:
	if (ENV.server) {
		connectionClose(ENV.server);
	}
	return 0;
}
