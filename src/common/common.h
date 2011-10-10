#ifndef COMMON_COMMON_H_
#define COMMON_COMMON_H_


#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../config.h"

//#include <pthread.h>

#define DEBUG 0
#define INFO  1
#define WARN  2
#define ERR   3

#define ALLOCATE_N(n, x)  calloc(n, (sizeof(x)))
#define ALLOCATE_1(x)     ALLOCATE_N(1,x)
#define FREE(x)           free((x))

int logLevel;

char* levelToString(int level);

#define LOG(level, format, ...)                                                                \
  if (level >= logLevel) {                                                                     \
	  printf("[%s][%s:%s:%d] " format "\n", levelToString(level),   __FILE__, __FUNCTION__, __LINE__, ##  __VA_ARGS__ ); \
  }

#define IfTrue(x, level, format, ... )          \
		if (!(x)) {                             \
			LOG(level, format, ##__VA_ARGS__)   \
            goto OnError;                       \
        }

#endif /* COMMON_COMMON_H_ */
