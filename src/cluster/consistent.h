#ifndef CLUSTER_CONSISTENT_H_
#define CLUSTER_CONSISTENT_H_

#include "../common/common.h"

typedef void* consistent_t;

/*
 * Implementation of consistent hashing based on libketama.
 * libketama using file based server specification and
 * uses shared memory so that multiple processes can
 * find this information.
 *
 * "consistent" on the other hand does everything in memory.
 * Any sharing required can be done via network calls.
 * As cacheismo is single threaded, cacheismo cluster
 * will many times exist on a single machine also. Don't
 * see much point in optimizing this path.
 */

/* comma or while space separated ip:port */
consistent_t consistentCreate(char* serverNames);
int          consistentGetServerCount(consistent_t consistent);

/* If server is marked unavailable, next server will be returned when lookup is done */
/* 0 if not available 1 if available */
int          consistentSetServerAvailable(consistent_t consistent, char* serverName, int available);
/* 1 if available 0 if not */
int          consistentIsServerAvailable(consistent_t consistent, char* serverName);
void         consistentDelete(consistent_t consistent);
/* returned value is pointing to local copy of "consistent"
 * don't free and don't use if "consistent" is deleted */
const char*  consistentFindServer(consistent_t consistent, char* key);

#endif /* CLUSTER_CONSISTENT_H_ */
