#ifndef PARSER_H
#define PARSER_H

#include "xmlconfig.h"

int parseLine(char* lineo, char* hex, int* hexlen, char* uSPtr, ssize_t uSPtrLen);
int execCmd(char* cmd, int fd, char* result, int resultLen);
void removeCompileList(compilePtr ptr);
int execByteCode(commandPtr cmdPtr, int fd, char* recvBuf, size_t recvLen, char* sendBuf, size_t sendLen, short supressUnit, int retry, char* pRecvPtr, unsigned short recvTimeout);
void compileCommand(devicePtr dPtr);

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
