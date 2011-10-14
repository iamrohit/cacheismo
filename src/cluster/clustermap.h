#ifndef CLUSTER_CLUSTERMAP_H_
#define CLUSTER_CLUSTERMAP_H_

#include "../common/common.h"
#include "../datastream/datastream.h"

/* This is the primary interface to access the cluster wide map
 * of key value pairs. It is actually the visible part of the
 * async client for getting data from other server.
 *
 *
 *TODO: keep the consistent hashing implementation separate from
 *TODO: the clusterMap. Basically if for some reason someone
 *TODO: wants to have another way of storing data, let him use it.
 *TODO: So consistent hash is exposed to lua...
 *TODO: The call to fetch from server will take server name as an
 *TODO: argument. This will ensure that caller can either use
 *TODO: consistent hashing object to do consistent hashing or
 *TODO: choose his own routing protocol..
 *TODO: Example could be use of one machine specifically for
 *TODO: real time stats. So stats are always stored on one machine
 *TODO: so that any computation on stats can refer to all data
 *TODO: locally
 */


typedef void* clusterMap_t;

//data is readOnly, create a copy for using it
typedef void (*clusterMapResultHandler_t)(void* luaContext, void* keyContext,
		                           int status, dataStream_t data);

clusterMap_t       clusterMapCreate(clusterMapResultHandler_t resultHandler);

int                clusterMapGet(clusterMap_t clusterMap,
					   void* luaContext, void* keyContext, char* server, char* key);

#endif /* CLUSTER_CLUSTERMAP_H_ */
