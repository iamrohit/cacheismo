#ifndef CONNECTION_H_
#define CONNECTION_H_

#include "../common/common.h"
#include "datastream.h"
#include <event.h>

typedef void* connection_t;

typedef struct {
	void (*newConnection)(connection_t connection);
	void (*writeAvailable)(connection_t connection);
	void (*readAvailable)(connection_t connection);
} connectionHandler_t;

connection_t  connectionCreate(u_int16_t port, connectionHandler_t* handler);
void          connectionClose(connection_t conn);
int           connectionRead(connection_t conn, fallocator_t fallocator, dataStream_t dataStream, u_int32_t maxBytesToRead, u_int32_t* bytesRead);
int           connectionWrite(connection_t conn, fallocator_t fallocator, dataStream_t dataStream, u_int32_t maxBytesToWrite, u_int32_t* bytesWritten);
connection_t  connectionAccept(connection_t serverConnection);
void          connectionSetContext(connection_t connection, void* context);
void*         connectionGetContext(connection_t connection);
void*         connectionGetBuffer(connection_t conn, fallocator_t fallocator, u_int32_t size, u_int32_t* offset);
void          connectionWaitForRead(connection_t conn, struct event_base *base);
void          connectionWaitForWrite(connection_t conn, struct event_base *base);

#endif /* CONNECTION_H_ */
