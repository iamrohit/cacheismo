#ifndef PARSER_H_
#define PARSER_H_

#include "../common/common.h"
#include "../io/datastream.h"
#include "../driver/commands.h"
#include "../fallocator/fallocator.h"

typedef void* parser_t;

parser_t   parserCreate(fallocator_t fallocator);
void       parserDelete(parser_t parser);
/* returns -1 on error, 1 if more input is required, 0 when parse is complete */
int        parserParse(parser_t parser, dataStream_t dataStream);
command_t* parserGetCommandAndReset(parser_t parser, dataStream_t dataStream);

#endif /* PARSER_H_ */
