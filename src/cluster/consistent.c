#include "consistent.h"
#include "../hashmap/hash.h"

//TODO : put libketama's license here...


typedef struct point_t {
	u_int32_t hashPoint;
	u_int32_t serverIndex;
}point_t;

typedef struct server_t {
	int       available;
	char*     serverName;
} server_t;

typedef struct consistentImpl_t {
	u_int32_t      spread;
	u_int32_t      serverCount;
    server_t*      servers;
    point_t*       points;
    int            pointsCount;
} consistentImpl_t;

#define CONSISTENT(x) (consistentImpl_t*)(x)

static u_int32_t hashcode( consistentImpl_t* pC, const char* key, u_int32_t keyLength) {
	return hash((u_int32_t*)key, (size_t)(keyLength), 0xFEEDDEED);
}

static int pointsCompare(const void* aa, const void* bb) {
	point_t* a = (point_t*)aa;
	point_t* b = (point_t*)bb;
    return ( a->hashPoint < b->hashPoint ) ?  -1 : ( ( a->hashPoint > b->hashPoint ) ? 1 : 0 );
}

#define MAX_SERVER_NAME_SIZE 32

static int calculatePoints(consistentImpl_t* pC) {
	char key[MAX_SERVER_NAME_SIZE];
	int length       = 0, returnValue = 0;
	int requiredSize = pC->spread * pC->serverCount;

	if (pC->pointsCount != requiredSize) {
		if (pC->points) {
			free(pC->points);
			pC->points      = 0;
			pC->pointsCount = 0;
		}
		pC->points = calloc(requiredSize, sizeof(point_t));
		IfTrue(pC->points, ERR, "Error allocating memory for points");
		pC->pointsCount = requiredSize;
	}

	for (int i = 0; i < pC->serverCount; i++) {
		for (int j = 0; j < pC->spread; j++) {
			memset(key, 0 , MAX_SERVER_NAME_SIZE);
			length = snprintf(key, MAX_SERVER_NAME_SIZE, "%s-%d", pC->servers[i].serverName, j);
			IfTrue(length < MAX_SERVER_NAME_SIZE, ERR, "server names to big");
			pC->points[(i*pC->spread)+j].hashPoint   = hashcode(pC, key, length);
			pC->points[(i*pC->spread)+j].serverIndex = i;
		}
	}
	qsort(pC->points, pC->pointsCount, sizeof(point_t), pointsCompare);

	goto OnSuccess;
OnError:
	if (pC->points) {
		free(pC->points);
		pC->points      = 0;
		pC->pointsCount = 0;
	}
	returnValue = -1;
OnSuccess:
	return returnValue;
}


static void freeServers(consistentImpl_t* pC) {
    if (pC->servers) {
    	for (int i = 0; i < pC->serverCount; i++) {
    		if (pC->servers[i].serverName) {
    			free(pC->servers[i].serverName);
    			pC->servers[i].serverName = 0;
    		}
    	}
    	free(pC->servers);
    	pC->servers = 0;
    	pC->serverCount = 0;
    }
}

#define MIN_SERVER_NAMES_SIZE 8
#define DELIM  ", \t"

static int parseAndSetServerNames(consistentImpl_t* pC, char* serverNames) {
	char*      serverNamesCopy = 0;
	server_t*  array           = 0;
	int        size            = MIN_SERVER_NAMES_SIZE;
    int        count           = 0;
    char*      token           = 0;
    int        returnValue     = 0;

    IfTrue(pC, ERR, "Null consistent pointer");
    IfTrue(serverNames, ERR, "Null server names");
    serverNamesCopy = strdup(serverNames);
    IfTrue(serverNamesCopy, ERR,  "Error copying server names");

    array = calloc(size, sizeof(char*));
    IfTrue(array, ERR,  "Error allocating memory");

    token = strtok(serverNamesCopy, DELIM);

    while (token) {
    	array[count].serverName = strdup(token);
    	IfTrue(array[count].serverName, ERR,  "Error allocating memory");
    	array[count].available = 1;
    	count++;
    	if (count == size) {
    		server_t* newArray = realloc(array, sizeof(server_t) * size * 2);
    		IfTrue(newArray, ERR,  "Error allocating memory");
    		array = newArray;
    		size  = size * 2;
    	}
    	token = strtok(NULL, DELIM);
    }
    freeServers(pC);
    pC->servers     = array;
    pC->serverCount = count;
    goto OnSuccess;
OnError:
	if (array) {
		for (int i = 0; i < count; i++) {
			if (array[i].serverName) {
				free(array[i].serverName);
				array[i].serverName = 0;
			}
		}
		free(array);
		array = 0;
	}
	returnValue = -1;
OnSuccess:
	if (serverNamesCopy) {
		free(serverNamesCopy);
		serverNamesCopy = 0;
	}
	return returnValue;
}

#define SPREAD  160
/* choosing a default because otherwise everyone has to
 * choose and ensure that it is same all over, which
 * could be a pain.
 * Bigger todo is to have some way to specify weightage
 * with the servers.
 */
consistent_t consistentCreate(char* serverNames) {
	consistentImpl_t* pC = ALLOCATE_1(consistentImpl_t);

	IfTrue(pC, ERR, "Error allocating memory");

	pC->spread    = SPREAD;

	IfTrue( 0 == parseAndSetServerNames(pC, serverNames), ERR, "Error setting server names");
	IfTrue( 0 == calculatePoints(pC), ERR, "Error calculating points");

	goto OnSuccess;
OnError:
	consistentDelete(pC);
	pC = 0;
OnSuccess:
    return pC;
}

void  consistentDelete(consistent_t consistent) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	if (pC) {
		freeServers(pC);
		if (pC->points) {
			free(pC->points);
			pC->points = 0;
		}
		free(pC);
	}
}


const char* consistentFindServer(consistent_t consistent, char* key) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	u_int32_t hashValue  = hashcode(pC, key, strlen(key));

	int highp = pC->pointsCount, lowp  = 0, midp = 0;
	u_int32_t midval, midval1;
	char* server      = 0;
	int   finalIndex  = 0;

	// divide and conquer array search to find server with next biggest
	// point after what this key hashes to
	while (1) {
		midp = (int)( ( lowp+highp ) / 2 );
		if (midp == pC->pointsCount) {
			// if at the end, roll back to zeroth
			server = pC->servers[pC->points[0].serverIndex].serverName;
			finalIndex = 0;
			break;
		}

		midval = pC->points[midp].hashPoint;
		midval1 = midp == 0 ? 0 : pC->points[midp-1].hashPoint;

		if (hashValue <= midval && hashValue > midval1) {
			server = pC->servers[pC->points[midp].serverIndex].serverName;
			finalIndex = midp;
			break;
		}

		if (midval < hashValue) {
			lowp = midp + 1;
		}else {
			highp = midp - 1;
		}

		if (lowp > highp) {
			server = pC->servers[pC->points[0].serverIndex].serverName;
			finalIndex = 0;
			break;
		}
	}

	//we found the server check if it is available
	if (pC->servers[finalIndex].available) {
		return server;
	}
	// the current server is not available..find the next available
	// and return that one
	int next = finalIndex + 1;
	while (next != finalIndex) {
		if (next < pC->pointsCount) {
			if (pC->servers[pC->points[next].serverIndex].available) {
				return pC->servers[pC->points[next].serverIndex].serverName;
			}
			next++;
		}else {
			next = 0;
		}
	}
	return 0;
}

int consistentGetServerCount(consistent_t consistent) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	if (pC) {
		return pC->serverCount;
	}
	return 0;
}

static int consistentGetServerIndex(consistent_t consistent, char* serverName) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	if (pC) {
		for (int i = 0; i < pC->serverCount; i++) {
			if (0 == strcmp(serverName, pC->servers[i].serverName)) {
				return i;
			}
		}
	}
	return -1;
}

int consistentSetServerAvailable(consistent_t consistent, char* serverName, int available) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	int index = consistentGetServerIndex(consistent, serverName);
	if (pC && (index <= pC->serverCount) && (index >= 0)) {
		pC->servers[index].available = available;
		return 0;
	}
	return -1;
}

int consistentIsServerAvailable(consistent_t consistent, char* serverName) {
	consistentImpl_t* pC = CONSISTENT(consistent);
	int index = consistentGetServerIndex(consistent, serverName);
	if (pC && (index <= pC->serverCount) && (index >= 0)) {
		return pC->servers[index].available;
	}
	return 0;
}
