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
    socklen_t cliaddrlen = sizeof(cliaddr);
    char clienthost[NI_MAXHOST];
    char clientservice[NI_MAXSERV];

    signal(SIGCHLD, SIG_IGN);

    for (;;)
    {
        connfd = accept(listenfd,
                        (struct sockaddr*) &cliaddr,
                        &cliaddrlen);

        if (connfd < 0)
        {
            logIT(LOG_ERR, "accept failed");
            continue;
        }

        getnameinfo((struct sockaddr*) &cliaddr, cliaddrlen,
                    clienthost, sizeof(clienthost),
                    clientservice, sizeof(clientservice),
                    NI_NUMERICHOST);

        if (!(*checkIP)(clienthost))
        {
            logIT(LOG_WARNING, "denied connection from %s:%s (fd:%d)", clienthost, clientservice, connfd);
            closeSocket(connfd);
            continue;
        }

        logIT(LOG_NOTICE, "client connection established %s:%s (fd:%d)", clienthost, clientservice, connfd);
        return connfd;
    }
}

void closeSocket(int sockfd)
{
    logIT(LOG_INFO, "connection closed (fd:%d)", sockfd);
    close(sockfd);
}


