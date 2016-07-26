/* unit.h */
/* $Id$ */

int procGetUnit(unitPtr uPtr, char *recvBuf, size_t len, char *result, char bitpos, char *pRecvPtr);
int procSetUnit(unitPtr uPtr, char *sendBuf, size_t *sendLen, char bitpos, char *pRecvPtr);

