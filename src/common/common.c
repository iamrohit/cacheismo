#include "common.h"

char* levelToString(int level) {
	switch(level) {
	case DEBUG: return "DEBUG";
	case INFO:  return "INFO";
	case WARN:  return "WARN";
	case ERR:   return "ERR";
	}
	return "UNKNOWN";
}
