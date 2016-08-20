#ifndef PARSER_H
#define PARSER_H

#include "xmlconfig.h"

void removeCompileList(compilePtr ptr);
int execByteCode(commandPtr cmdPtr, int fd, char* recvBuf, size_t recvLen, char* sendBuf, size_t sendLen, short supressUnit);
compilePtr buildByteCode(commandPtr cPtr);
int expand(commandPtr cPtr, protocolPtr pPtr);

/* Token Definition */

#define WAIT    1
#define RECV    2
#define SEND    3
#define PAUSE   4
#define BYTES   5

#ifndef MAXBUF
    #define MAXBUF 4096
#endif

#endif /* PARSER_H */
