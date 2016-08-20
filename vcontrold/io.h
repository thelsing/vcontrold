#ifndef IO_H
#define IO_H

#include <string>
#include <cstdlib>

int my_send(int fd, char* s_buf, size_t len);
size_t receive_nb(int fd, char* r_buf, size_t r_len, unsigned long* etime);
int waitfor(int fd, char* w_buf, int w_len);
int openDevice(const char* device);
void closeDevice(int fd);

ssize_t	writen(int fd, const void* vptr, size_t n);
ssize_t Writen(int fd, void* ptr, size_t nbytes);

bool ReadLine(int fd, std::string* line);

#ifndef MAXBUF
    #define MAXBUF 4096
#endif
#define TIMEOUT 5

#endif /* IO_H */
