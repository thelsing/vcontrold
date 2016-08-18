/* socket.c */
/* $Id: socket.c 13 2008-03-02 13:13:41Z marcust $ */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifndef __CYGWIN__
    /* I'm not sure about this cpp defines, can some check tht? -fn- */
    #ifdef __linux__
        #include <linux/tcp.h>	/*do we realy need this? Not sure for Linux -fn- */
    #endif
    #if defined (__FreeBSD__) || defined(__APPLE__)
        #include <netinet/in.h>
        #include <netinet/tcp.h>	/* TCP_NODELAY is defined there -fn- */
    #endif
#endif

#include "socket.h"
#include "common.h"

extern int inetversion;

const int LISTEN_QUEUE = 128;

int openSocket(int tcpport)
{
    int listenfd;
    int n;
    char* port;
    struct addrinfo hints, *res, *ressave;

    memset(&hints, 0, sizeof(struct addrinfo));

    switch (inetversion)
    {
        case 6:
            hints.ai_family = PF_INET6;
            break;

        case 4:		/* this is for backward compatibility. We can explictly
                 activate IPv6 with the -6 switch. Later we can use
                 PF_UNSPEC as default and let the OS decide */
        default:
            hints.ai_family = PF_INET;
    }

    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    asprintf(&port, "%d", tcpport);

    n = getaddrinfo(NULL, port, &hints, &res);

    free(port);

    if (n < 0)
    {
        logIT(LOG_ERR, "getaddrinfo error:: [%s]\n",
              gai_strerror(n));
        return -1;
    }

    ressave = res;

    /*
     Try open socket with each address getaddrinfo returned,
     until getting a valid listening socket.
     */

    listenfd = -1;

    while (res)
    {
        listenfd = socket(res->ai_family,
                          res->ai_socktype,
                          res->ai_protocol);
        int optval = 1;

        if (listenfd > 0 && setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
                                       &optval, sizeof optval) < 0)
        {
            logIT(LOG_ERR, "setsockopt gescheitert!");
            exit(1);
        }

        if (!(listenfd < 0))
        {
            if (bind(listenfd,
                     res->ai_addr,
                     res->ai_addrlen) == 0)
                break;

            close(listenfd);
            listenfd = -1;
        }

        res = res->ai_next;
    }

    if (listenfd < 0)
    {
        freeaddrinfo(ressave);
        fprintf(stderr,
                "socket error:: could not open socket\n");
        exit(1);
    }

    listen(listenfd, LISTEN_QUEUE);
    logIT(LOG_NOTICE, "TCP socket %d geoeffnet", tcpport);

    freeaddrinfo(ressave);

    return listenfd;
}


int listenToSocket(int listenfd, short(*checkIP)(char*))
{
    int connfd;
    struct sockaddr_storage cliaddr;
    socklen_t               cliaddrlen = sizeof(cliaddr);
    char clienthost[NI_MAXHOST];
    char clientservice[NI_MAXSERV];

    signal(SIGCHLD, SIG_IGN);

    for (;;)
    {
        connfd = accept(listenfd,
                        (struct sockaddr*) &cliaddr,
                        &cliaddrlen);

        getnameinfo((struct sockaddr*) &cliaddr, cliaddrlen,
                    clienthost, sizeof(clienthost),
                    clientservice, sizeof(clientservice),
                    NI_NUMERICHOST);

        if (!(*checkIP)(clienthost))
        {
            close(connfd);
            continue;
        }

        if (connfd < 0)
        {
            logIT(LOG_NOTICE, "accept auf host %s: port %s", clienthost, clientservice);
            close(connfd);
            continue;
        }

        logIT(LOG_NOTICE, "Client verbunden %s:%s (FD:%d)", clienthost, clientservice, connfd);
        return connfd;
    }
}

void closeSocket(int sockfd)
{
    logIT(LOG_INFO, "Verbindung beendet (fd:%d)", sockfd);
    close(sockfd);
}

/* Stuff aus Unix Network Programming Vol 1*/

/* include writen */

ssize_t						/* Write "n" bytes to a descriptor. */
writen(int fd, const void* vptr, size_t n)
{
    ssize_t		nleft;
    ssize_t		nwritten;
    const char*	ptr;

    ptr = (char*)vptr;
    nleft = n;

    while (nleft > 0)
    {
        if ((nwritten = write(fd, ptr, nleft)) <= 0)
        {
            if (errno == EINTR)
                nwritten = 0;		/* and call write() again */
            else
                return (-1);			/* error */
        }

        nleft -= nwritten;
        ptr += nwritten;
    }

    return (ssize_t)n;
}
/* end writen */

ssize_t
Writen(int fd, void* ptr, size_t nbytes)
{
    if (writen(fd, ptr, nbytes) != (ssize_t)nbytes)
    {
        logIT(LOG_ERR, "Fehler beim schreiben auf socket");
        return (0);
    }

    return (ssize_t)nbytes;
}


/* include readn */

ssize_t						/* Read "n" bytes from a descriptor. */
readn(int fd, void* vptr, size_t n)
{
    ssize_t	nleft;
    ssize_t	nread;
    char*	ptr;

    ptr = (char*) vptr;
    nleft = n;

    while (nleft > 0)
    {
        if ((nread = read(fd, ptr, nleft)) < 0)
        {
            if (errno == EINTR)
                nread = 0;		/* and call read() again */
            else
                return (-1);
        }
        else if (nread == 0)
            break;				/* EOF */

#ifdef __CYGWIN__

        if (nread > nleft) 				// This is a workaround for Cygwin.
            nleft = 0;					// Here cygwins read(fd,buff,count) is
        else							// reading more than count chars! this is bad!
            nleft -= nread;

#else
        nleft -= nread;
#endif
        ptr += nread;
    }

    return ((ssize_t)n) - nleft;		/* return >= 0 */
}
/* end readn */

ssize_t
Readn(int fd, void* ptr, size_t nbytes)
{
    ssize_t		n;

    if ((n = readn(fd, ptr, nbytes)) < 0)
    {
        logIT(LOG_ERR, "Fehler beim lesen von socket");
        return (0);
    }

    return (n);
}


/* include readline */

static ssize_t
my_read(int fd, char* ptr)
{
    static ssize_t read_cnt = 0;
    static char*	read_ptr;
    static char	read_buf[MAXLINE];

    if (read_cnt <= 0)
    {
again:

        if ((read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0)
        {
            if (errno == EINTR)
                goto again;

            return (-1);
        }
        else if (read_cnt == 0)
            return (0);

        read_ptr = read_buf;
    }

    read_cnt--;
    *ptr = *read_ptr++;
    return (1);
}

ssize_t
readline(int fd, void* vptr, size_t maxlen)
{
    ssize_t rc;
    char	c, *ptr;

    ptr = (char*)vptr;
    size_t n;

    for (n = 1; n < maxlen; n++)
    {
        if ((rc = my_read(fd, &c)) == 1)
        {
            *ptr++ = c;

            if (c == '\n')
                break;	/* newline is stored, like fgets() */
        }
        else if (rc == 0)
        {
            if (n == 1)
                return (0);	/* EOF, no data read */
            else
                break;		/* EOF, some data was read */
        }
        else
            return (-1);		/* error, errno set by read() */
    }

    *ptr = 0;	/* null terminate like fgets() */
    return (ssize_t)n;
}
/* end readline */

ssize_t
Readline(int fd, void* ptr, size_t maxlen)
{
    ssize_t		n;

    if ((n = readline(fd, ptr, maxlen)) < 0)
    {
        logIT(LOG_ERR, "Fehler beim lesen von socket");
        return (0);
    }

    return (n);
}
