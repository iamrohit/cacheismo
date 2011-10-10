#ifndef PARSER_H_
#define PARSER_H_

#include "../common/common.h"
#include "../datastream/datastream.h"
#include "../common/commands.h"
#include "../fallocator/fallocator.h"

typedef void* requestParser_t;
typedef void* responseParser_t;

requestParser_t    requestParserCreate(fallocator_t fallocator);
void               requestParserDelete(requestParser_t parser);
/* returns -1 on error, 1 if more input is required, 0 when parse is complete */
int                requestParserParse(requestParser_t parser, dataStream_t dataStream);
command_t*         requestParserGetCommandAndReset(requestParser_t parser, dataStream_t dataStream);

responseParser_t   responseParserCreate(fallocator_t fallocator);
void               responseParserDelete(responseParser_t parser);
/* returns -1 on error, 1 if more input is required, 0 when parse is complete */
int                responseParserParse(responseParser_t parser, dataStream_t dataStream);
/* 0 on valid response, 1 on end */
int                responseParserGetResponse(responseParser_t parser,
		              dataStream_t dataStream, char** pKey, dataStream_t* pValue,
		              u_int32_t* pFlags);

#endif /* PARSER_H_ */
