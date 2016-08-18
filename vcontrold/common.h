#ifndef COMMON_H
#define COMMON_H

#include <cstdlib>

/* Deklarationen */
int initLog(int useSyslog, const char* logfile, int debugSwitch);
void logIT(int logclass, const char* string, ...);
char hex2chr(char* hex);
size_t char2hex(char* outString, const char* charPtr, size_t len);
size_t string2chr(char* line, char* buf, size_t bufsize);
void sendErrMsg(int fd);
void setDebugFD(int fd);
ssize_t readn(int fd, void* vptr, size_t n);

#ifndef MAXBUF
    #define MAXBUF 4096
#endif

#endif /* COMMON_H */