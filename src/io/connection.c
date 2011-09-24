#include "connection.h"
#include <netinet/tcp.h>

typedef struct {
	bool               isServer;
	int                fd;
	struct sockaddr_in address;
	struct event       event;
	char*              buffer;
	int                bufferSize;
	int                bufferUsed;
	void*              context;
}connectionImpl_t;

#define CONNECTION(x) ((connectionImpl_t*)(x))

/* Most commands are small */
#define DEFAULT_BUFFER_SIZE 1024
#define DEFAULT_BACKLOG     128

static connectionHandler_t* pGlobalConnectionHandler;

#define GCH() pGlobalConnectionHandler

static void connectionEventHandler(const int fd, const short which, void *arg) {
	connectionImpl_t* pC = CONNECTION(arg);
    assert(pC != NULL);

    if (fd != pC->fd) {
    	connectionClose(pC);
        return;
    }

    if (pC->isServer) {
    	int done = 8;
    	while (--done) {
    		connection_t newConnection = connectionAccept(pC);
    		if (newConnection) {
    			GCH()->newConnection(newConnection);
    		}else {
    			break;
    		}
    	}
    }else {
    	if (which == EV_READ) {
    		GCH()->readAvailable(pC);
       	}else {
       		GCH()->writeAvailable(pC);
    	}
    }
    return;
}

connection_t  connectionCreate(u_int16_t port, connectionHandler_t* handler) {
	connectionImpl_t* pC = ALLOCATE_1(connectionImpl_t);

	IfTrue(pC, ERR, "Error allocating memory");
	pC->fd = socket(AF_INET, SOCK_STREAM, 0);
	IfTrue(pC->fd > 0, ERR, "Error creating new socket");

	{
		int flags = fcntl(pC->fd, F_GETFL, 0);
	    IfTrue(fcntl(pC->fd, F_SETFL, flags | O_NONBLOCK) == 0,
	    		ERR, "Error setting non blocking");
	}

	bzero((char*) &pC->address, sizeof(pC->address));
	pC->address.sin_family        = AF_INET;
	pC->address.sin_addr.s_addr   = INADDR_ANY;
	pC->address.sin_port          = htons(port);

	IfTrue(bind(pC->fd, (struct sockaddr *) &pC->address,sizeof(pC->address)) == 0,  ERR, "Error binding");
	IfTrue(listen(pC->fd, DEFAULT_BACKLOG) == 0,  ERR, "Error listening");
	pC->isServer = true;

	//TODO : bad design...no class level stuff
	pGlobalConnectionHandler = handler;
	goto OnSuccess;
OnError:
	if (pC) {
		connectionClose(pC);
		pC = 0;
	}
OnSuccess:
	return pC;
}

void connectionClose(connection_t conn) {
	connectionImpl_t* pConnection = CONNECTION(conn);
	if (pConnection) {
		if (pConnection->fd > 0) {
			close(pConnection->fd);
			pConnection->fd = 0;
		}
		if (pConnection->buffer) {
			dataStreamBufferFree(pConnection->buffer);
			pConnection->buffer = NULL;
		}
		FREE(pConnection);
	}
}



void* connectionGetBuffer(connection_t conn, fallocator_t fallocator, u_int32_t size, u_int32_t* offset){
	connectionImpl_t* pC     = CONNECTION(conn);
	void*             buffer = 0;

	IfTrue(pC, ERR, "Null connection");
	IfTrue(size <= DEFAULT_BUFFER_SIZE, ERR, "size too big");

	if (size > (pC->bufferSize - pC->bufferUsed)) {
		if (pC->buffer) {
			dataStreamBufferFree(pC->buffer);
			pC->buffer = NULL;
		}
		pC->buffer  = dataStreamBufferAllocate(NULL, fallocator, DEFAULT_BUFFER_SIZE);
		IfTrue(pC->buffer, ERR, "Out of memory - can't allocate new buffer");
		pC->bufferSize = DEFAULT_BUFFER_SIZE;
		pC->bufferUsed = 0;
	}
	*offset = pC->bufferUsed;
	buffer  = pC->buffer;
	pC->bufferUsed+= size;
	goto OnSuccess;
OnError:
	buffer = 0;
OnSuccess:
	return buffer;
}

int connectionRead(connection_t conn, fallocator_t fallocator, dataStream_t dataStream, u_int32_t maxBytesToRead, u_int32_t* bytesRead) {
	connectionImpl_t* pC        = CONNECTION(conn);
	int              done      = 0;

	while (!done) {
		if (pC->bufferUsed == pC->bufferSize) {
			if (pC->buffer) {
				dataStreamBufferFree(pC->buffer);
				pC->buffer = NULL;
			}
			pC->buffer = dataStreamBufferAllocate(NULL, fallocator, DEFAULT_BUFFER_SIZE);
			if (!pC->buffer) {
				LOG(ERR, "Out of memory - can't allocate new buffer");
				return -1;
			}
			pC->bufferSize = DEFAULT_BUFFER_SIZE;
			pC->bufferUsed = 0;
		}
		int bytesToRead = (pC->bufferSize - pC->bufferUsed) > (maxBytesToRead - *bytesRead) ?
                           (maxBytesToRead - *bytesRead) : (pC->bufferSize - pC->bufferUsed);

		if (bytesToRead == 0) {
			//printf("bufferUsed %d maxBytesToRead %d bytesRead %d conn %p\n", pC->bufferUsed, maxBytesToRead, *bytesRead,  pC);
			return 0;
		}

		//printf("Read - bufferSize %d conn %p\n", bytesToRead, pC);
		int bytesCount  = read(pC->fd, pC->buffer + pC->bufferUsed, bytesToRead);

		switch (bytesCount) {
			case -1:
			{
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					done = 1;
				}else {
					//error on socket
					done = -1;
					LOG(INFO, "Error reading from socket");
				}
				break;
			}
			case 0:
			{
				done = -1;
				LOG(INFO, "connection closed by peer");
				break;
			}
			default:
			{
				//some bytes read
				*bytesRead += bytesCount;
				dataStreamAppendData(dataStream, pC->buffer, pC->bufferUsed, bytesCount);
				pC->bufferUsed+= bytesCount;
				if (bytesCount == bytesToRead) {
					done = 0;
				}else {
					done = 1;
				}
			}
		}
	}
	return done;
}

static int connectionWriteHelper(connection_t conn, fallocator_t fallocator, dataStream_t dataStream, u_int32_t offset, u_int32_t maxBytesToWrite, u_int32_t* bytesWritten) {
	int                  returnValue   = 0;
	connectionImpl_t*    pConnection   = CONNECTION(conn);
	struct msghdr        messageHeader = {0, 0, 0, 0, 0, 0, 0};
	struct iovec*        vector = 0;
	int                  messageSize = 0;
	dataStreamIterator_t iter        = 0;

	IfTrue(pConnection, ERR, "Null Connection pointer");
	IfTrue(dataStream, ERR, "Null data buffer");


	iter = dataStreamIteratorCreate(fallocator, dataStream, offset, maxBytesToWrite);
	IfTrue(iter, ERR, "Error gettig iterator");

	u_int32_t  bufferCount = dataStreamIteratorGetBufferCount(iter);
	IfTrue(bufferCount > 0, WARN, "No data in the data buffer");
	vector = calloc(bufferCount, sizeof(struct iovec));
	IfTrue(vector, ERR, "Error allocating memory");
	for (int i = 0; i < bufferCount; i++) {
		u_int32_t offset = 0;
		u_int32_t length = 0;
		char* buffer = dataStreamIteratorGetBufferAtIndex(iter, i, &offset,
				&length);
		IfTrue(buffer, ERR, "Got Null buffer from iterator");
		vector[i].iov_base = buffer + offset;
		vector[i].iov_len = length;
		messageSize += length;
	}

	messageHeader.msg_iov = vector;
	messageHeader.msg_iovlen = bufferCount;
	int bytesCount = sendmsg(pConnection->fd, &messageHeader, 0);
	switch (bytesCount) {
		case -1: {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				returnValue = 1;
				goto OnSuccess;
			} else {
				goto OnError;
			}
		}
		case 0: {
			goto OnError;
		}
		default: {
			*bytesWritten += bytesCount;
			if (bytesCount != messageSize) {
				//not all bytes were written, we will get EAGAIN in the next call
				returnValue = 1;
				goto OnSuccess;
			}
		}
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	if (iter) {
		dataStreamIteratorDelete(fallocator, iter);
	}
	if (vector) {
		free(vector);
	}
	return returnValue;
}


#define MAX_BYTES_TO_WRITE  (64 * 1024)

int connectionWrite(connection_t conn, fallocator_t fallocator, dataStream_t dataStream, u_int32_t maxBytesToWrite, u_int32_t* bytesWritten) {
	u_int32_t localBytesWritten    = 0;
	int       result               = 0;
	while (result == 0 &&  (localBytesWritten < maxBytesToWrite)) {
		u_int32_t  bytesWrittenInThisCall = 0;
		u_int32_t  localMaxBytesToWrite = (maxBytesToWrite -localBytesWritten)  > MAX_BYTES_TO_WRITE ? MAX_BYTES_TO_WRITE : (maxBytesToWrite -localBytesWritten);
		result = connectionWriteHelper(conn, fallocator, dataStream, localBytesWritten, localMaxBytesToWrite, &bytesWrittenInThisCall);
		localBytesWritten += bytesWrittenInThisCall;
	}
	*bytesWritten = localBytesWritten;
	return result;
}


connection_t  connectionAccept(connection_t serverConnection) {
	connectionImpl_t* pServer = CONNECTION(serverConnection);
	connectionImpl_t* pNew    = ALLOCATE_1(connectionImpl_t);
	socklen_t    socketLength = 0;

	IfTrue(pNew, ERR, "Error allocating memory");
	socketLength = sizeof(pNew->address);

	pNew->fd = accept(pServer->fd, &(pNew->address), &socketLength);
	IfTrue(pNew->fd > 0, INFO, "Error accepting new connection %d %s\n", pNew->fd, strerror(errno));
	{
		int flags = fcntl(pNew->fd, F_GETFL, 0);
		IfTrue(fcntl(pNew->fd, F_SETFL, flags | O_NONBLOCK) == 0,
				INFO, "Error setting non blocking");

        IfTrue(setsockopt(pNew->fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags)) == 0,
        		INFO, "Error setting Keep-Alive");
        IfTrue(setsockopt(pNew->fd, IPPROTO_TCP, TCP_NODELAY, (void *)&flags, sizeof(flags)) == 0,
                		INFO, "Error setting tcp no delay");
	}
	goto OnSuccess;
OnError:
	if (pNew) {
		connectionClose(pNew);
		pNew = NULL;
	}
OnSuccess:
	return pNew;
}


void  connectionWaitForRead(connection_t conn, struct event_base *base) {
	connectionImpl_t* pC = CONNECTION(conn);
	int flags = EV_READ;
	if (pC->isServer) {
		flags = EV_READ | EV_PERSIST;
	}
	event_set(&pC->event, pC->fd, flags, connectionEventHandler, (void *)pC);
    event_base_set(base, &pC->event);
    event_add(&pC->event, 0);
}

void connectionWaitForWrite(connection_t conn, struct event_base *base) {
	connectionImpl_t* pC = CONNECTION(conn);
	event_set(&pC->event, pC->fd, EV_WRITE, connectionEventHandler, (void *)pC);
	event_base_set(base, &pC->event);
	event_add(&pC->event, 0);
}

void connectionSetContext(connection_t connection, void* context) {
	connectionImpl_t* pC = CONNECTION(connection);
	if (pC) {
		pC->context = context;
	}
}

void* connectionGetContext(connection_t connection) {
	connectionImpl_t* pC = CONNECTION(connection);
	if (pC) {
		return pC->context;
	}
	return 0;
}

