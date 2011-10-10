#include "commands.h"

#define COMMAND(x) (command_t*)(x)

void commandDelete(fallocator_t fallocator, command_t* pCommand) {
	if (pCommand) {
		if (pCommand->key) {
			fallocatorFree(fallocator, pCommand->key);
			pCommand->key = 0;
		}
		if (pCommand->dataStream) {
			dataStreamDelete(pCommand->dataStream);
			pCommand->dataStream = 0;
		}
		if (pCommand->multiGetKeys) {
			for (int i = 0; i < pCommand->multiGetKeysCount; i++) {
				if (pCommand->multiGetKeys[i]) {
					fallocatorFree(fallocator,pCommand->multiGetKeys[i]);
				}
			}
			fallocatorFree(fallocator, pCommand->multiGetKeys);
		}
		fallocatorFree(fallocator, pCommand);
	}
}

