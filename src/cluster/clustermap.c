#include "clustermap.h"
#include "../io/connection.h"
#include "../parser/parser.h"
#include "../common/list.h"
#include "../cacheismo.h"
#include "../common/map.h"

typedef struct request_t {
	struct request_t* pNext;
	struct request_t* pPrev;
	char*             key;
	void*             keyContext;
    void*             luaContext;
} request_t;


enum connection_status_t {
	status_connecting  = 1,
	status_active,
	status_pooled,
    status_waiting_read,
    status_waiting_write
};

/* This object handles the actual request by sending data to the server
 * parsing the response and reporting results.It takes a set of request
 * objects at a time. If a single object is given it uses get otherwise
 * it will use multi-get.
 *
 * TODO: Since we use fallocator, it might be a good idea to close the
 * connection periodically, say after handling million requests.
 */

typedef struct connectionContext_t {
	struct connectionContext_t* pNext;
	struct connectionContext_t* pPrev;
	enum connection_status_t    status;
	connection_t                connection;
	dataStream_t                readStream;
	dataStream_t                writeStream;
	responseParser_t            parser;
	list_t                      currentRequests; //list of request_t
	fallocator_t                fallocator;
	void*                       pExternalServer;
} connectionContext_t;


typedef struct externalServer_t {
	char*      serverName;           //ip:port
	char*      serverIP;
	int        serverPort;
	list_t     unassignedRequests;   //list of request_t
    list_t     freeConnections;      //list of connectionContext_t
    list_t     activeConnections;    //list of connectionContext_t
    void*      pClusterMap;
} externalServer_t;

typedef struct clusterMapImpl_t {
	clusterMapResultHandler_t resultHandler;
    map_t                     serverMap;
} clusterMapImpl_t;


#define CLUSTER_MAP(x) (clusterMapImpl_t*)(x)

static void deleteRequest(request_t* pRequest) {
	if (pRequest) {
		if (pRequest->key) {
			FREE(pRequest->key);
			pRequest->key = 0;
		}
		FREE(pRequest);
	}
}

/**
 * creates a request object which encapsulates all the information
 * necessary for making a request
 */
static request_t*  createRequest(char* virtualKey, void* luaContext, void* keyContext) {
	request_t* pRequest = ALLOCATE_1(request_t);

	IfTrue(pRequest, ERR, "Error allocating memory");
	pRequest->key        = strdup(virtualKey);
	IfTrue(pRequest->key, ERR, "Error copying key");
	pRequest->luaContext = luaContext;
	pRequest->keyContext = keyContext;
	goto OnSuccess;
OnError:
	if (pRequest) {
		deleteRequest(pRequest);
		pRequest = 0;
	}
OnSuccess:
	return pRequest;
}


static void connectionContextDelete(connectionContext_t* pContext, int move) {
	if (pContext) {
		externalServer_t* pServer  = pContext->pExternalServer;
		clusterMapImpl_t* pCMap    = pServer->pClusterMap;
		request_t*        pRequest = 0;

		LOG(DEBUG, "deleting connection context");

		while ((pRequest = listRemoveLast(pContext->currentRequests)) != 0) {
			if (move) {
				//move the existing requests to another socket
				listAddFirst(pServer->unassignedRequests, pRequest);
			}else {
				//report error
				pCMap->resultHandler(pRequest->luaContext, pRequest->keyContext,  -1,  NULL);
				deleteRequest(pRequest);
			}
		}
		if (pContext->connection) {
			connectionClose(pContext->connection);
		}
		if (pContext->readStream) {
			dataStreamDelete(pContext->readStream);
			pContext->readStream = 0;
		}
		if (pContext->writeStream) {
			dataStreamDelete(pContext->writeStream);
			pContext->writeStream = 0;
		}
		if (pContext->parser) {
			responseParserDelete(pContext->parser);
			pContext->parser = 0;
		}
		if (pContext->currentRequests) {
			listFree(pContext->currentRequests);
			pContext->currentRequests = 0;
		}
		if (pContext->fallocator) {
			fallocatorDelete(pContext->fallocator);
		}
		FREE(pContext);
	}
}

/*  create get request from request objects in the current request list
 *  and write it to the writeStream.
 */

static int connectionMakeGetRequest(connectionContext_t* pCContext){
	int count = listGetSize(pCContext->currentRequests);
	if (count < 1) {
		return 0;
	}
	request_t* pCurrent = listGetFirst(pCContext->currentRequests);
	int estimatedBufferSize   = 0;
	while (pCurrent) {
		estimatedBufferSize = strlen(pCurrent->key) + 1;
		pCurrent = listGetNext(pCContext->currentRequests, pCurrent);
	}
	estimatedBufferSize -= 1;     //don't need space after last key
	estimatedBufferSize += 4 + 2; // strlen("get ") + strlen("\r\n")

	u_int32_t offset     = 0;
	char*     buffer     = 0;
	int       written    = 0;
	buffer = connectionGetBuffer(pCContext->connection, pCContext->fallocator,
			        estimatedBufferSize, &offset);
    if (buffer) {
    	request_t* pCurrent = listGetFirst(pCContext->currentRequests);
    	strncpy(buffer+offset, "get ", 4);
    	written += 4;
		while (pCurrent) {
			request_t* pNext = listGetNext(pCContext->currentRequests, pCurrent);
			int keyLength = strlen(pCurrent->key);
			strncpy(buffer+offset+written, pCurrent->key, keyLength);
			written+= keyLength;
			if (pNext) {
				strncpy(buffer+offset+written, " ", 1);
				written+= 1;
			}
			pCurrent = pNext;
		}
		strncpy(buffer+offset+written, "\r\n", 2);
		written += 2;
		LOG(DEBUG, "created request %.*s", written, buffer+offset);
		dataStreamAppendData(pCContext->writeStream, buffer, offset, written);
		//TODO : check return value
    }
	return written;
}


static connectionContext_t* connectionContextCreate(connection_t conn, void* pServer) {
	connectionContext_t* pContext = ALLOCATE_1(connectionContext_t);

	IfTrue(conn, ERR, "Null Connection");
	IfTrue(pContext, ERR, "Error allocating memory");

	pContext->readStream = dataStreamCreate();
	IfTrue(pContext->readStream, WARN, "Error creating read stream");
	pContext->writeStream = dataStreamCreate();
	IfTrue(pContext->writeStream, WARN, "Error creating write stream");
	pContext->fallocator = fallocatorCreate();
	IfTrue(pContext->fallocator, WARN, "Error creating fallocator");
	pContext->parser = responseParserCreate(pContext->fallocator);
	IfTrue(pContext->parser, WARN, "Error creating parser");
	pContext->currentRequests = listCreate(OFFSET(request_t, pNext),
			                               OFFSET(request_t, pPrev));
	IfTrue(pContext->currentRequests, WARN, "Error creating request list");
	pContext->connection      = conn;
	pContext->pExternalServer = pServer;
	goto OnSuccess;
OnError:
	if (pContext) {
		connectionContextDelete(pContext, 0);
		pContext = 0;
	}
OnSuccess:
	return pContext;
}




static int completeWrite(connectionContext_t* pContext) {
	u_int32_t size    = dataStreamGetSize(pContext->writeStream);
	u_int32_t written = 0;
	int       err     = 0;
	if (size > 0) {
		err =  connectionWrite(pContext->connection, pContext->fallocator,
				pContext->writeStream,size, &written);
		if (err < 0) {
			return err;
		}
		if (written > 0) {
			dataStreamTruncateFromStart(pContext->writeStream, (size - written));
		}
		LOG(DEBUG, "writing to socket err %d written %d", err, written);
		return err;
	}
	return 0;
}

static int connectionSubmitRequests(externalServer_t* pServer, connectionContext_t* pCContext);

static void connectCompleteImpl(connection_t connection, int status) {
	connectionContext_t* pContext  = connectionGetContext(connection);
	if (status != 0) {
		connectionClose(connection);
		connectionContextDelete(pContext, 0);
	}else {
		LOG(DEBUG, "got connect complete callback ");
        connectionConnect(connection);
		pContext->status = status_active;
		connectionSubmitRequests(pContext->pExternalServer, pContext);
	}
}

// forward declaration...is called form write available
static void readAvailableImpl(connection_t connection);

static void writeAvailableImpl(connection_t connection) {
	connectionContext_t* pCContext  = connectionGetContext(connection);

	if (pCContext->status != status_waiting_write) {
		LOG(ERR, "Invalid state... ");
	}

    int err = completeWrite(pCContext);
    if (err < 0) {
    	externalServer_t* pServer  = pCContext->pExternalServer;
    	listRemove(pServer->activeConnections, pCContext);
		connectionContextDelete(pCContext, 1);
    }else {
    	if (err == 0) {
    		//write compelete
    		pCContext->status = status_active;
    		if (dataStreamGetSize(pCContext->readStream) > 0) {
    			readAvailableImpl(connection);
    		}else {
    			pCContext->status = status_waiting_read;
    			connectionWaitForRead(pCContext->connection, getGlobalEventBase());
    		}
    	}else {
    		pCContext->status = status_waiting_write;
    		connectionWaitForWrite(pCContext->connection, getGlobalEventBase());
    	}
    }
}

static void readAvailableImpl(connection_t connection){
	connectionContext_t* pCContext    = connectionGetContext(connection);
	u_int32_t            bytesRead   = 0;
	int                  returnValue = 0;

	LOG(DEBUG, "got something to read on socket");

	//if it is one of the pooled connections
	if (pCContext->status == status_pooled) {
		LOG(DEBUG, "socket was pooled..closing on read");
		//socket is closed
		externalServer_t* pServer = pCContext->pExternalServer;
		listRemove(pServer->freeConnections, pCContext);
		connectionContextDelete(pCContext, 0);
		goto OnSuccess;
	}

	if (pCContext->status == status_waiting_read) {
		pCContext->status = status_active;
	}else {
		LOG(ERR, "Invalid connection state");
		goto OnError;
	}

	returnValue = connectionRead(pCContext->connection, pCContext->fallocator,
			                   pCContext->readStream, 8 * 1024 , &bytesRead);
	LOG(DEBUG, "connection read status %d bytesRead %d", returnValue, bytesRead);
	IfTrue(returnValue >= 0, INFO, "Socket closed");

doParseMore:

	returnValue = responseParserParse(pCContext->parser, pCContext->readStream);
	LOG(DEBUG, "response parser status %d", returnValue);
	IfTrue(returnValue >= 0, INFO, "Parsing Error %d", returnValue);

	if (returnValue == 1) {
		LOG(DEBUG, "need to wait for read");
		pCContext->status = status_waiting_read;
		connectionWaitForRead(pCContext->connection, getGlobalEventBase());
		goto OnSuccess;
	}

	char*         key   = 0;
	dataStream_t  value = 0;
	u_int32_t     flags = 0;

	returnValue = responseParserGetResponse(pCContext->parser, pCContext->readStream,
			                                &key, &value, &flags);
	LOG(DEBUG, "got resposonse %d key %s", returnValue, key);

	if (returnValue == 0) {
		externalServer_t* pServer  = pCContext->pExternalServer;
		clusterMapImpl_t* pCM      = pServer->pClusterMap;
		request_t*        pRequest = 0;

tryNext:
	    pRequest = listRemoveFirst(pCContext->currentRequests);
	    if (pRequest) {
			if (0 == strcmp(pRequest->key, key)) {
				//we got the response for the key
				LOG(DEBUG, "giving callback for success result");
				pCM->resultHandler(pRequest->luaContext, pRequest->keyContext, 0, value);
				dataStreamDelete(value);
				value    = 0;
				fallocatorFree(pCContext->fallocator, key);
				key      = 0;
				deleteRequest(pRequest);
				pRequest = 0;
				goto doParseMore;
			}else {
				//this key is different from the key we expected
				//the request for current key failed, so notify
				LOG(DEBUG, "giving callback for fail result");

				pCM->resultHandler(pRequest->luaContext, pRequest->keyContext,  -1, NULL);
				deleteRequest(pRequest);
				pRequest = 0;
				//move on to the next key, may be its her response
				goto tryNext;
			}
	    }else {
	    	// no more request in the currentRequests list?
            // what this means is that we got a response for a key
	    	// which we didn't asked for...
	    	// that is strange.., some server issue..we can't do much
	    	dataStreamDelete(value);
	    	value = 0;
	        fallocatorFree(pCContext->fallocator, key);
	    	key   = 0;
	    	// now so just close this connection

	    	externalServer_t* pServer = pCContext->pExternalServer;
			listRemove(pServer->activeConnections, pCContext);
			connectionContextDelete(pCContext, 0);
			pCContext = 0;
	    }
	}else {
		LOG(DEBUG, "end of results from response parser");
		// we go END response from parser
		externalServer_t* pServer = pCContext->pExternalServer;
		clusterMapImpl_t* pCM      = pServer->pClusterMap;
		request_t*        pRequest = 0;
		//anything pending in the currentRequests..not found
		while ((pRequest = listRemoveLast(pCContext->currentRequests)) != 0) {
			//report error
			pCM->resultHandler(pRequest->luaContext, pRequest->keyContext, -1,  NULL);
			deleteRequest(pRequest);
		}
		// add this connections to free connections list
	    listRemove(pServer->activeConnections, pCContext);
	    connectionSubmitRequests(pServer, pCContext);
	}
	goto OnSuccess;
OnError:
	if (pCContext) {
		externalServer_t* pServer = pCContext->pExternalServer;
		listRemove(pServer->activeConnections, pCContext);
		connectionContextDelete(pCContext, 1);
		pCContext = 0;
	}
OnSuccess:
	return;
}


static connectionHandler_t* pClientConnectionHandler = 0;

static connectionHandler_t* createConnectionHandler() {
	if (pClientConnectionHandler) {
		return pClientConnectionHandler;
	}
	pClientConnectionHandler = ALLOCATE_1(connectionHandler_t);
	pClientConnectionHandler->newConnection   = 0;
	pClientConnectionHandler->readAvailable   = &readAvailableImpl;
	pClientConnectionHandler->writeAvailable  = &writeAvailableImpl;
	pClientConnectionHandler->connectComplete = &connectCompleteImpl;
	return pClientConnectionHandler;
}

static void externalServerDelete(externalServer_t* pServer) {
	if (pServer) {
		if (pServer->serverName) {
			FREE(pServer->serverName);
		}
		if (pServer->serverIP) {
			FREE(pServer->serverIP);
		}
		// all these lists should be empty as we are just deleting the
		// head and not all the objects inside the list
		listFree(pServer->activeConnections);
		listFree(pServer->freeConnections);
		listFree(pServer->unassignedRequests);
		FREE(pServer);
	}
}


static externalServer_t* externalServerCreate(char* serverName, clusterMapImpl_t* pCM) {
	externalServer_t* pServer = ALLOCATE_1(externalServer_t);
	char* serverNameCopy      = 0;
	char* ip                  = 0;
	char* port                = 0;

	IfTrue(pServer, ERR, "Error allocating memory");
	pServer->serverName = strdup(serverName);
	IfTrue(pServer->serverName, ERR, "Error allocating memory");

	serverNameCopy = strdup(pServer->serverName);
	IfTrue(serverNameCopy, ERR, "Error allocating memory");

	ip = strtok(serverNameCopy, ":");
	IfTrue(ip, ERR, "Error parsing serverName");
	pServer->serverIP = strdup(ip);
	IfTrue(ip, ERR, "Error copying server ip");
	port = strtok(0, ":");
	IfTrue(port, ERR, "Error parsing server port");
	pServer->serverPort = atoi(port);
	FREE(serverNameCopy);
	serverNameCopy = 0;

	pServer->activeConnections  = listCreate(OFFSET(connectionContext_t, pNext),
								             OFFSET(connectionContext_t, pPrev));
	IfTrue(pServer->activeConnections, ERR, "Error allocating memory");
	pServer->freeConnections    = listCreate(OFFSET(connectionContext_t, pNext),
								     	     OFFSET(connectionContext_t, pPrev));
	IfTrue(pServer->freeConnections, ERR, "Error allocating memory");
	pServer->unassignedRequests = listCreate(OFFSET(request_t, pNext),
											 OFFSET(request_t, pPrev));
	IfTrue(pServer->unassignedRequests, ERR, "Error allocating memory");
	pServer->pClusterMap = pCM;
	goto OnSuccess;
OnError:
	if (serverNameCopy) {
		FREE(serverNameCopy);
	}
	if (pServer) {
		externalServerDelete(pServer);
		pServer = 0;
	}
OnSuccess:
	return pServer;
}


#define MAX_MULTI_GET_REQUESTS         16

static int connectionSubmitRequests(externalServer_t* pServer, connectionContext_t* pCContext) {
	//we have a free connection
	int  count = MAX_MULTI_GET_REQUESTS;
	while (count-- > 0) {
		request_t* pRequest = listRemoveFirst(pServer->unassignedRequests);
		if (!pRequest) {
			break;
		}
		listAddLast(pCContext->currentRequests, pRequest);
	}
	if (listGetSize(pCContext->currentRequests) > 0) {
		listAddLast(pServer->activeConnections, pCContext);
		int bytesWritten = connectionMakeGetRequest(pCContext);
		if (bytesWritten > 0) {
			int err = completeWrite(pCContext);
			if (err == 0) {
				pCContext->status = status_waiting_read;
				connectionWaitForRead(pCContext->connection, getGlobalEventBase());
			}else {
				if (err == 1) {
					pCContext->status = status_waiting_write;
					connectionWaitForWrite(pCContext->connection, getGlobalEventBase());
				}else {
					// error writing to server/ connection closed ?
					listRemove(pServer->activeConnections, pCContext);
					connectionContextDelete(pCContext, 1);
				}
			}
		}
	}else {
		//pooled connections also wait for read, incase they are close by the server
		pCContext->status = status_pooled;
		listAddLast(pServer->freeConnections, pCContext);
		connectionWaitForRead(pCContext->connection, getGlobalEventBase());
	}
	return 0;
}

#define MAX_CONCURRENT_CONNECTIONS     64

/*
 * Request is always submitted (May be later we need to put a limit)
 * 0 - submitted and assigned to a connection
 * 1 - submitted but not assigned because no available connection
 */

static int externalServerSubmit(externalServer_t* pServer, request_t* pRequest) {
	int                  returnValue   = 0;
	connectionContext_t* pCContext     = 0;
	connection_t         newConnection = 0;

	listAddLast(pServer->unassignedRequests, pRequest);
	pCContext = listRemoveFirst(pServer->freeConnections);

	if (pCContext) {
		connectionWaitCancel(pCContext->connection, getGlobalEventBase());
	}

	if (!pCContext) {
		//check if we have reached max concurrent connections limit
		// if not, create a new connection
		if (listGetSize(pServer->activeConnections) < MAX_CONCURRENT_CONNECTIONS) {
			LOG(DEBUG, "creating new external connection");
			newConnection = connectionClientCreate(pServer->serverIP,
												pServer->serverPort,
												createConnectionHandler());
			IfTrue(newConnection, ERR, "Error creating new connection to %s", pServer->serverName);
			//got a new connection
			pCContext = connectionContextCreate(newConnection, pServer);
			IfTrue(pCContext, ERR, "Error allocting memory for connection context");
			connectionSetContext(newConnection, pCContext);
			newConnection = 0;

			int err = connectionConnect(pCContext->connection);
			IfTrue(err >= 0, ERR, "connect failed");

			if (err == 1) {
				LOG(DEBUG, "waiting for connect to complete");
				pCContext->status = status_connecting;
				connectionWaitForConnect(pCContext->connection, getGlobalEventBase());
				goto OnSuccess;
			}
		}else {
			//if we have reached max connection limit, we will let the request rest
			//in the queue. Whenever one of the current connections get free, we will
			//use that to send the request.
			returnValue = 1;
			goto OnSuccess;
		}
	}

	if (pCContext) {
		pCContext->status = status_active;
		connectionSubmitRequests(pServer, pCContext);
	}
	goto OnSuccess;
OnError:
	if (newConnection) {
		connectionClose(newConnection);
	}
	if (pCContext) {
		connectionContextDelete(pCContext, 0);
	}
	returnValue = 1;
OnSuccess:
	return returnValue;
}


clusterMap_t clusterMapCreate(clusterMapResultHandler_t resultHandler) {
	clusterMapImpl_t* pCM = ALLOCATE_1(clusterMapImpl_t);
	if (pCM) {
		pCM->resultHandler   = resultHandler;
		pCM->serverMap       = mapCreate();
	}
	return pCM;
}

/*
 *  1 - if request is not submitted since local server can handle it
 *  0 - if request is submitted successfully
 * -1 - in case of error submitting request
 *        - out of memory
 *        - invalid arguments
 *        - other system limitations
 */
int clusterMapGet(clusterMap_t clusterMap, void* luaContext,  void* keyContext, char* server, char* key ) {
	clusterMapImpl_t* pCM         = CLUSTER_MAP(clusterMap);
	request_t*        newRequest  = 0;
	externalServer_t* pEServer    = 0;
	int               returnValue = 0;

	IfTrue(pCM && server && key &&  luaContext, ERR, "Null argument found");
	newRequest = createRequest(key, luaContext, keyContext);
	IfTrue(newRequest, ERR, "Error allocting memory for new request");

	pEServer = mapGetElement(pCM->serverMap, server);
	if (!pEServer) {
		pEServer = externalServerCreate(server, pCM);
		IfTrue(pEServer, ERR, "Error creating server entry for server %s", server);
		mapPutElement(pCM->serverMap, server, pEServer);
	}
	externalServerSubmit(pEServer, newRequest);
	goto OnSuccess;
OnError:
	if (newRequest) {
		deleteRequest(newRequest);
		newRequest = 0;
	}
	returnValue = -1;
OnSuccess:
	return returnValue;
}
