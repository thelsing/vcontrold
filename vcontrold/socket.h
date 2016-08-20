#ifndef SOCKET_H
    #define SOCKET_H

    int openSocket(int tcpport);
    int listenToSocket(int listenfd, short(*checkP)(char*));
    void closeSocket(int sockfd);

#endif /* SOCKET_H */