#ifndef CONVERSION_H
#define CONVERSION_H

#include "xmlconfig.h"

void convertToString(commandPtr cmdPtr, char* recvBuf, size_t len, char* result);
void convertBack(commandPtr cmdPtr, char* sendBuf, size_t* sendLen);

#endif /* CONVERSION_H */

