#include "parser.h"
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>

enum parseState {
	parse_first,
	parse_data
};

typedef struct {
	fallocator_t    fallocator;
	enum parseState state;
	u_int32_t       endOfLine;
	u_int32_t       requestSize;
	command_t*      pCommand;
} requestParserImpl_t;

#define PARSER(x) (requestParserImpl_t*)(x)

#define xisspace(c) isspace((unsigned char)c)

static bool safe_strtoull(const char *str, uint64_t *out) {
	assert(out != NULL);
	errno = 0;
	*out = 0;
	char *endptr;
	unsigned long long ull = strtoull(str, &endptr, 10);
	if (errno == ERANGE)
		return false;
	if (xisspace(*endptr) || (*endptr == '\0' && endptr != str)) {
		if ((long long) ull < 0) {
			/* only check for negative signs in the uncommon case when
			 * the unsigned number is so big that it's negative as a
			 * signed number. */
			if (strchr(str, '-') != NULL) {
				return false;
			}
		}
		*out = ull;
		return true;
	}
	return false;
}

static bool safe_strtoul(const char *str, uint32_t *out) {
	char *endptr = NULL;
	unsigned long l = 0;
	assert(out);
	assert(str);
	*out = 0;
	errno = 0;

	l = strtoul(str, &endptr, 10);
	if (errno == ERANGE) {
		return false;
	}

	if (xisspace(*endptr) || (*endptr == '\0' && endptr != str)) {
		if ((long) l < 0) {
			/* only check for negative signs in the uncommon case when
			 * the unsigned number is so big that it's negative as a
			 * signed number. */
			if (strchr(str, '-') != NULL) {
				return false;
			}
		}
		*out = l;
		return true;
	}

	return false;
}

static void cleanupTokens(fallocator_t fallocator, char** tokens, int ntokens) {
	if (tokens) {
		for (int i = 0; i < ntokens; i++) {
			if (tokens[i]) {
				fallocatorFree(fallocator, tokens[i]);
				tokens[i] = 0;
			}
		}
		fallocatorFree(fallocator, tokens);
	}
}

static char** tokenizeFirstLine(fallocator_t fallocator , dataStream_t dataStream, u_int32_t endOfLine, int *tokenCount) {
	int start = 0, end = 0, size  = 16, bufferCount = 0;
	dataStreamIterator_t iter = 0;
	char **tokens = 0;

	*tokenCount = 0;
	iter = dataStreamIteratorCreate(fallocator, dataStream, 0, endOfLine);
	IfTrue(iter, INFO, "Error creating iterator");
	bufferCount = dataStreamIteratorGetBufferCount(iter);
	IfTrue(bufferCount > 0, INFO, "No data in iterator");
	tokens = fallocatorMalloc(fallocator, size * sizeof(char*));
	IfTrue(tokens, WARN, "Error allocating memory for tokens")

	u_int32_t seen = 0;
	for (int i = 0; i < bufferCount; i++) {
		u_int32_t offset = 0;
		u_int32_t length = 0;
		char*     buffer = dataStreamIteratorGetBufferAtIndex(iter, i, &offset, &length);

		for (end = seen; end < (seen+length); end++) {
			u_int8_t current = *(buffer+offset+(end -seen));
			if (current == ' ') {
				if (start != end) {
					if (start >= seen) {
						char* out = fallocatorMalloc(fallocator, end - start + 1);
						IfTrue(out, WARN,  "Error allocating memory");
						memcpy(out, buffer+offset+ (start - seen), end - start);
						out[end - start]= 0;
						tokens[(*tokenCount)++] = out;
					}else {
						tokens[(*tokenCount)++] = dataStreamIteratorGetString(fallocator, iter, start, end - start);
						IfTrue(tokens[(*tokenCount) - 1], INFO, "Error getting string from iterator");
					}
				}
				start = end + 1;
			} else {
				if (end == (endOfLine - 1)) {
					if (start >= seen) {
						char* out = fallocatorMalloc(fallocator, end - start + 2);
						IfTrue(out, WARN, "Error allocating memory");
						memcpy(out, buffer+offset+ (start - seen), end - start + 1);
						out[end - start + 1]= 0;
						tokens[(*tokenCount)++] = out;
					}else {
						tokens[(*tokenCount)++] = dataStreamIteratorGetString(fallocator, iter, start, end - start+1);
						IfTrue(tokens[(*tokenCount) - 1], INFO, "Error getting string from iterator");
					}
				}
			}
			if (*tokenCount == size) {
				char** newTokens = fallocatorMalloc(fallocator, size * 2 * sizeof(char*));
				IfTrue(newTokens, INFO, "Error allocating memory")
				memcpy(newTokens, tokens, size * sizeof(char*));
				fallocatorFree(fallocator, tokens);
				tokens = newTokens;
				size   = size * 2;
			}
		}
		seen += length;
	}
	goto OnSuccess;
OnError:
	if (tokens) {
		cleanupTokens(fallocator, tokens, size);
		tokens      = 0;
		*tokenCount = 0;
	}
OnSuccess:
	if (iter) {
		dataStreamIteratorDelete(fallocator, iter);
	}
	return tokens;
}

requestParser_t requestParserCreate(fallocator_t fallocator) {
	requestParserImpl_t* pParser = ALLOCATE_1(requestParserImpl_t);
	if (pParser) {
		pParser->state = parse_first;
		pParser->pCommand   = fallocatorMalloc(fallocator, sizeof(command_t));
		pParser->fallocator = fallocator;
	}
	return pParser;
}

void requestParserDelete(requestParser_t parser) {
	requestParserImpl_t* pParser = PARSER(parser);
	if (pParser) {
		if (pParser->pCommand) {
			commandDelete(pParser->fallocator, pParser->pCommand);
		}
		FREE(pParser);
	}
}

static int parseFirstLineRequest(fallocator_t fallocator, requestParserImpl_t* pParser, char** tokens, int ntokens) {
	int returnValue = 0;

	if (ntokens >= 2 && (((strcmp(tokens[0], "get") == 0) && (pParser->pCommand->command
			= COMMAND_GET)) || ((strcmp(tokens[0], "bget") == 0)
			&& (pParser->pCommand->command = COMMAND_BGET)))) {

		if (ntokens > 2) {
			//copy the keys ...
			pParser->pCommand->multiGetKeys = fallocatorMalloc(fallocator, (ntokens-1) * sizeof(char*));
			IfTrue(pParser->pCommand->multiGetKeys, WARN, "Error allocating memory");
			pParser->pCommand->multiGetKeysCount = ntokens -1 ;
			for (int i = 1; i < ntokens; i++) {
				pParser->pCommand->multiGetKeys[i-1] = tokens[i];
				tokens[i] = 0;
			}
		}else {
			pParser->pCommand->key = tokens[1];
			pParser->pCommand->keySize = strlen(tokens[1]);
			tokens[1] = 0;
		}
	} else if ((ntokens == 5 || ntokens == 6) && ((strcmp(tokens[0], "add")
			== 0 && (pParser->pCommand->command = COMMAND_ADD)) || (strcmp(tokens[0],
			"set") == 0 && (pParser->pCommand->command = COMMAND_SET)) || (strcmp(
			tokens[0], "replace") == 0 && (pParser->pCommand->command = COMMAND_REPLACE))
			|| (strcmp(tokens[0], "prepend") == 0 && (pParser->pCommand->command
					= COMMAND_PREPEND)) || (strcmp(tokens[0], "append") == 0
			&& (pParser->pCommand->command = COMMAND_APPEND)))) {
		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;
		IfTrue(safe_strtoul(tokens[2], &pParser->pCommand->flags), INFO, "Error parsing flags");
		IfTrue(safe_strtoul(tokens[3], &pParser->pCommand->expiryTime), INFO, "Error parsing expiry time ");
		IfTrue(safe_strtoul(tokens[4], &pParser->pCommand->dataLength), INFO, "Error parsing data length");

		if (tokens[5] != NULL) {
			if (strcmp(tokens[5], "noreply") == 0) {
				pParser->pCommand->noreply = 1;
			}
		}

	} else if ((ntokens == 6 || ntokens == 7)
			&& (strcmp(tokens[0], "cas") == 0)) {
		pParser->pCommand->command = COMMAND_CAS;

		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;
		IfTrue(safe_strtoul(tokens[2], &pParser->pCommand->flags), INFO, "Error parsing flags");
		IfTrue(safe_strtoul(tokens[3], &pParser->pCommand->expiryTime), INFO, "Error parsing expiry time ");
		IfTrue(safe_strtoul(tokens[4], &pParser->pCommand->dataLength), INFO, "Error parsing data length");
		IfTrue(safe_strtoull(tokens[5], &pParser->pCommand->cas), INFO, "Error parsing cas id");

		if (tokens[6] != NULL) {
			if (strcmp(tokens[6], "noreply") == 0) {
				pParser->pCommand->noreply = 1;
			}
		}

	} else if ((ntokens == 3 || ntokens == 4) && (strcmp(tokens[0], "incr")
			== 0)) {
		pParser->pCommand->command = COMMAND_INCR;
		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;

		IfTrue(safe_strtoull(tokens[2], &pParser->pCommand->delta), INFO, "Error parsing delta");
		if (tokens[3] != NULL) {
			if (strcmp(tokens[3], "noreply") == 0) {
				pParser->pCommand->noreply = 1;
			}
		}
	} else if (ntokens >= 2 && (strcmp(tokens[0], "gets") == 0)) {
		pParser->pCommand->command = COMMAND_GETS;
		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;
		//TODO - only suport one key per get for now

	} else if ((ntokens == 4 || ntokens == 5) && (strcmp(tokens[0], "decr")
			== 0)) {
		pParser->pCommand->command = COMMAND_DECR;
		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;

		IfTrue(safe_strtoull(tokens[2], &pParser->pCommand->delta), INFO, "Error parsing delta");
		if (tokens[3] != NULL) {
			if (strcmp(tokens[3], "noreply") == 0) {
				pParser->pCommand->noreply = 1;
			}
		}
	} else if (ntokens >= 2 && ntokens <= 4 && (strcmp(tokens[0], "delete")
			== 0)) {
		pParser->pCommand->command = COMMAND_DELETE;
		pParser->pCommand->key = tokens[1];
		pParser->pCommand->keySize = strlen(tokens[1]);
		tokens[1] = 0;
		if (tokens[2] != NULL) {
			if (strcmp(tokens[2], "noreply") == 0) {
				pParser->pCommand->noreply = 1;
			}
		}
	} else if (ntokens >= 2 && (strcmp(tokens[0], "stats") == 0)) {
		pParser->pCommand->command = COMMAND_STATS;
		//TODO - later
	} else if (ntokens >= 1 && ntokens <= 2 && (strcmp(tokens[0], "flush_all")
			== 0)) {
		pParser->pCommand->command = COMMAND_FLUSH_ALL;
		//TODO - later
	} else if (ntokens == 1 && (strcmp(tokens[0], "version") == 0)) {
		pParser->pCommand->command = COMMAND_VERSION;
		//TODO - later
	} else if (ntokens == 1 && (strcmp(tokens[0], "quit") == 0)) {
		pParser->pCommand->command = COMMAND_QUIT;
		//TODO - later
	} else if ((ntokens == 2 || ntokens == 3)
			&& (strcmp(tokens[0], "verbosity") == 0)) {
		pParser->pCommand->command = COMMAND_VERBOSITY;
		IfTrue(safe_strtoul(tokens[2], &pParser->pCommand->flags), INFO,
				"Error parsing verosity level");
	} else {
		//this is error
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	return returnValue;
}

static int isDataExpected(requestParserImpl_t* pParser) {
	switch (pParser->pCommand->command) {
	case COMMAND_ADD:
	case COMMAND_SET:
	case COMMAND_REPLACE:
	case COMMAND_PREPEND:
	case COMMAND_APPEND:
	case COMMAND_CAS:
		 return 1;
	default:
		return 0;
	}
}

/* returns -1 on error, 1 if more input is required, 0 when parse is complete */
int requestParserParse(requestParser_t parser, dataStream_t dataStream) {
	requestParserImpl_t* pParser = PARSER(parser);
	char** tokens = 0;
	int    ntokens = 0, endOfLine = 0, parseResult = 0, returnValue = 0;

	IfTrue(pParser, ERR, "Null Parser Object");
	IfTrue(pParser->pCommand, ERR, "Null command object");

	if (pParser->state == parse_first) {
		endOfLine = dataStreamFindEndOfLine(dataStream);
		if (endOfLine <= 0) {
			returnValue = 1;
			goto OnSuccess;
		}
		pParser->endOfLine = endOfLine;
		tokens = tokenizeFirstLine(pParser->fallocator, dataStream, endOfLine, &ntokens);
		IfTrue(tokens, DEBUG, "Error getting tokens");
		parseResult = parseFirstLineRequest(pParser->fallocator, pParser, tokens, ntokens);
		if (parseResult < 0) {
			LOG(INFO, "parsing error %d\n", parseResult);
			goto OnError;
		}
		if (!isDataExpected(pParser)) {
			pParser->requestSize = endOfLine + 2;
			goto OnSuccess;
		}
		pParser->state = parse_data;
		pParser->requestSize = endOfLine + 2 + pParser->pCommand->dataLength + 2;
	}

	if (pParser->state == parse_data) {
		if (dataStreamGetSize(dataStream) < pParser->requestSize) {
			returnValue = 1;
			goto OnSuccess;
		}
		pParser->pCommand->dataStream = dataStreamSubStream(pParser->fallocator, dataStream, (pParser->endOfLine+2),
				                                            pParser->pCommand->dataLength);
		IfTrue(pParser->pCommand->dataStream, INFO, "Error creating data stream");
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	if (pParser) {
		cleanupTokens(pParser->fallocator, tokens, ntokens);
		tokens = 0;
	}
	return returnValue;
}

/*
 * This is called when parserParse returns 0 indicating success;
 * Using the data captured by the parser we will use the
 * commands.h  interface to construct appropriate command to be
 * executed by the server.
 *
 */
command_t* requestParserGetCommandAndReset(requestParser_t parser, dataStream_t dataStream) {
	requestParserImpl_t* pParser = PARSER(parser);
	command_t* pCommand   = pParser->pCommand;
	u_int32_t newSize = dataStreamGetSize(dataStream) - pParser->requestSize;

	pParser->pCommand    = fallocatorMalloc(pParser->fallocator, sizeof(command_t));
	pParser->endOfLine   = 0;
	pParser->requestSize = 0;
	pParser->state       = parse_first;

	dataStreamTruncateFromStart(dataStream, newSize);
	return pCommand;
}

typedef struct {
	fallocator_t    fallocator;
	enum parseState state;
	u_int32_t       endOfLine;
	u_int32_t       valueLength;
	u_int32_t       flags;
	u_int32_t       endOfResponse;
	u_int32_t       endOfValue;
	char*           key;
	dataStream_t    value;
} responseParserImpl_t;

#define RESPONSE_PARSER(x) (responseParserImpl_t*)(x)


responseParser_t responseParserCreate(fallocator_t fallocator) {
	responseParserImpl_t* pParser = ALLOCATE_1(responseParserImpl_t);
	if (pParser) {
		pParser->state         = parse_first;
		pParser->fallocator    = fallocator;
		pParser->endOfResponse = 0;
	}
	return pParser;
}

void responseParserDelete(responseParser_t parser) {
	responseParserImpl_t* pParser = RESPONSE_PARSER(parser);
	if (pParser) {
		if (pParser->key) {
			fallocatorFree(pParser->fallocator, pParser->key);
		}
		if (pParser->value) {
			dataStreamDelete(pParser->value);
		}
		FREE(pParser);
	}
}

/**
 * VALUE key flags datalength\r\n
 * <DATA>\r\n
 * END\r\n
 */
static int parseFirstLineResponse(fallocator_t fallocator, responseParserImpl_t* pParser, char** tokens,
		int ntokens) {
	int returnValue = 0;

	if (0 == strcmp(tokens[0], "END")) {
		pParser->endOfResponse = 1;
		goto OnSuccess;
	}
	if (0 == strcmp(tokens[0], "VALUE")) {
		pParser->key = tokens[1];
		tokens[1] = 0;

		IfTrue(safe_strtoul(tokens[2], &pParser->flags), INFO, "Error parsing flags");
		IfTrue(safe_strtoul(tokens[3], &pParser->valueLength), INFO, "Error parsing value length");
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	LOG(DEBUG, "endOfResponse %d", pParser->endOfResponse);
	return returnValue;
}

/* returns -1 on error, 1 if more input is required, 0 when parse is complete */
int responseParserParse(responseParser_t parser, dataStream_t dataStream) {
	responseParserImpl_t* pParser = RESPONSE_PARSER(parser);
	char** tokens = 0;
	int    ntokens = 0, endOfLine = 0, parseResult = 0, returnValue = 0;

	IfTrue(pParser, ERR, "Null Parser Object");

	if (pParser->state == parse_first) {
		endOfLine = dataStreamFindEndOfLine(dataStream);
		if (endOfLine <= 0) {
			returnValue = 1;
			goto OnSuccess;
		}
		pParser->endOfLine = endOfLine;
		tokens = tokenizeFirstLine(pParser->fallocator, dataStream, endOfLine, &ntokens);
		IfTrue(tokens, DEBUG, "Error getting tokens");
		parseResult = parseFirstLineResponse(pParser->fallocator, pParser, tokens, ntokens);
		if (parseResult < 0) {
			LOG(INFO, "parsing error %d\n", parseResult);
			goto OnError;
		}
		if (pParser->endOfResponse) {
			LOG(DEBUG, "endOfResponse %d", pParser->endOfResponse);
			pParser->endOfValue = endOfLine+2;
			goto OnSuccess;
		}

		pParser->state      = parse_data;
		pParser->endOfValue = endOfLine + 2 + pParser->valueLength + 2;
	}

	if (pParser->state == parse_data) {
		if (dataStreamGetSize(dataStream) < pParser->endOfValue) {
			returnValue = 1;
			goto OnSuccess;
		}
		pParser->value = dataStreamSubStream(pParser->fallocator, dataStream,
								(pParser->endOfLine+2), pParser->valueLength);
		IfTrue(pParser->value, INFO, "Error creating data stream");
	}
	goto OnSuccess;
OnError:
	returnValue = -1;
OnSuccess:
	if (pParser) {
		cleanupTokens(pParser->fallocator, tokens, ntokens);
		tokens = 0;
	}
	return returnValue;
}

int responseParserGetResponse(responseParser_t parser, dataStream_t dataStream,
		char** pKey, dataStream_t* pValue, u_int32_t* pFlags) {

	responseParserImpl_t* pParser = RESPONSE_PARSER(parser);
	u_int32_t newSize     = dataStreamGetSize(dataStream) - pParser->endOfValue;
	int       returnValue = pParser->endOfResponse;
	fallocator_t fallocator = pParser->fallocator;

	LOG(DEBUG, "returnValue %d", returnValue);

	dataStreamTruncateFromStart(dataStream, newSize);
	*pKey = pParser->key;
	*pValue = pParser->value;
    *pFlags = pParser->flags;
    memset(pParser, 0, sizeof(responseParserImpl_t));
    pParser->state      = parse_first;
    pParser->fallocator = fallocator;
	return returnValue;
}

