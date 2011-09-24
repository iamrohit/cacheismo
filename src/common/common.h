#ifndef COMMON_H_
#define COMMON_H_

#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#define DEBUG 0
#define INFO  1
#define WARN  2
#define ERR   3


static char* levelToString(int level) {
	switch(level) {
	case DEBUG: return "DEBUG";
	case INFO:  return "INFO";
	case WARN:  return "WARN";
	case ERR:  return "ERR";
	}
	return "UNKNOWN";
}

#define ALLOCATE_N(n, x)  calloc(n, (sizeof(x)))
#define ALLOCATE_1(x)     ALLOCATE_N(1,x)
#define FREE(x)           free((x))

int logLevel;

#define LOG(level, format, ...)                                                                \
  if (level >= logLevel) {                                                                     \
	  printf("[%s][%s:%s:%d] " format "\n", levelToString(level),   __FILE__, __FUNCTION__, __LINE__, ##  __VA_ARGS__ ); \
  }

#define IfTrue(x, level, format, ... )          \
		if (!(x)) {                             \
			LOG(level, format, ##__VA_ARGS__)   \
            goto OnError;                       \
        }

#define bool char
#define false 0
#define true 1

#endif /* COMMON_H_ */
