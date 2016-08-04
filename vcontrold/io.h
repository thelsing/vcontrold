#ifndef IO_H
#define IO_H

#include <cstdlib>

int my_send(int fd, char* s_buf, size_t len);
int receive(int fd, char* r_buf, int r_len, unsigned long* etime);
size_t receive_nb(int fd, char* r_buf, size_t r_len, unsigned long* etime);
int waitfor(int fd, char* w_buf, int w_len);
int opentty(char* device);
int openDevice(char* device);
void closeDevice(int fd);


/* Schnittstellenparameter */

#define TTY    "/dev/usb/tts/0"

#ifndef MAXBUF
    #define MAXBUF 4096
#endif
#define TIMEOUT 5

#endif /* IO_H */
