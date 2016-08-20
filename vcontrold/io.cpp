#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <iostream>
#include <algorithm>
#include <stdexcept>

#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include "io.h"
#include "common.h"

#ifdef __CYGWIN__
    /* NCC is not defined under cygwin */
    #define NCC NCCS
#endif

extern int debug;

static void sig_alrm(int);
static jmp_buf	env_alrm;

void closeDevice(int fd)
{
    close(fd);
}

int openDevice(const char* device)
{
    int fd;

    logIT(LOG_INFO, "konfiguriere serielle Schnittstelle %s", device);

    if ((fd = open(device, O_RDWR)) < 0)
    {
        logIT(LOG_ERR, "cannot open %s:%m", device);
        exit(1);
    }

    int s;
    struct termios oldsb, newsb;
    s = tcgetattr(fd, &oldsb);

    if (s < 0)
    {
        logIT(LOG_ERR, "error tcgetattr %s:%m", device);
        exit(1);
    }

    newsb = oldsb;

#ifdef NCC

    /* NCC is not always defined */
    for (s = 0; s < NCC; s++)
        newsb.c_cc[s] = 0;

#else
    bzero(&newsb, sizeof(newsb));
#endif
    newsb.c_iflag = IGNBRK | IGNPAR;
    newsb.c_oflag = 0;
    newsb.c_lflag = 0;   /* removed ISIG for susp=control-z problem; */
    newsb.c_cflag = (CLOCAL | B4800 | CS8 | CREAD | PARENB | CSTOPB);
    newsb.c_cc[VMIN] = 1;
    newsb.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSADRAIN, &newsb);

    /* DTR High fuer Spannungsversorgung */
    int modemctl = 0;
    ioctl(fd, TIOCMGET, &modemctl);
    modemctl |= TIOCM_DTR;
    s = ioctl(fd, TIOCMSET, &modemctl);

    if (s < 0)
    {
        logIT(LOG_ERR, "error ioctl TIOCMSET %s:%m", device);
        exit(1);
    }

    return (fd);
}

/* Read "n" bytes from a descriptor. */
static ssize_t	readn(int fd, void* vptr, size_t n)
{
    ssize_t	nleft;
    ssize_t	nread;
    char*	ptr;

    ptr = (char*)vptr;
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


int my_send(int fd, char* s_buf, size_t len)
{
    char string[256];

    /* Buffer leeren */
    /* da tcflush nicht richtig funktioniert, verwenden wir nonblocking read */
    fcntl(fd, F_SETFL, O_NONBLOCK);

    while (readn(fd, string, sizeof(string)) > 0);

    fcntl(fd, F_SETFL, !O_NONBLOCK);

    tcflush(fd, TCIFLUSH);

    /* wir benutzen die Socket feste Vairante aus socket.c */
    writen(fd, s_buf, len);

    for (size_t i = 0; i < len; i++)
    {

        unsigned char byte = s_buf[i] & 255;
        logIT(LOG_INFO, ">SEND: %02X", (int)byte);
    }

    return 1;
}

static int receive(int fd, char* r_buf, int r_len, unsigned long* etime)
{
    int i;

    struct tms tms_t;
    clock_t start, end, mid, mid1;
    double clktck = (double)sysconf(_SC_CLK_TCK);
    start = times(&tms_t);
    mid1 = start;

    for (i = 0; i < r_len; i++)
    {
        if (signal(SIGALRM, sig_alrm) == SIG_ERR)
            logIT(LOG_ERR, "SIGALRM error");

        if (setjmp(env_alrm) != 0)
        {
            logIT(LOG_ERR, "read timeout");
            return (-1);
        }

        alarm(TIMEOUT);

        /* wir benutzen die Socket feste Vairante aus socket.c */
        if (readn(fd, &r_buf[i], 1) <= 0)
        {
            logIT(LOG_ERR, "error read tty");;
            alarm(0);
            return (-1);
        }

        alarm(0);
        unsigned char byte = r_buf[i] & 255;
        mid = times(&tms_t);
        logIT(LOG_INFO, "<RECV: %02X (%0.1f ms)", byte, ((double)(mid - mid1) / clktck) * 1000);
        mid1 = mid;
    }

    end = times(&tms_t);
    *etime = (unsigned long)((double)(end - start) / clktck) * 1000;
    return i;
}

static int setnonblock(int fd)
{
    int flags;
    /* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)

    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    /* Otherwise, use the old way of doing it */
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

static int setblock(int fd)
{
    int flags;
    /* If they have O_BLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)

    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;

    return fcntl(fd, F_SETFL, flags & (~O_NONBLOCK));
#else
    /* Otherwise, use the old way of doing it */
    flags = 0;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

static char* dump(char* dest, const char* title, char* buf, size_t len)
{
    int pos = 0;
    size_t i;

    pos = sprintf(dest, "%s", title);

    for (i = 0; i < len; i++)
        pos += sprintf(dest + pos, " %02X", buf[i] & 0xff);

    return dest;
}

size_t receive_nb(int fd, char* r_buf, size_t r_len, unsigned long* etime)
{
    size_t i;
    ssize_t len;
    char string[100];

    fd_set rfds;
    struct timeval tv;
    int retval;

    struct tms tms_t;
    clock_t start, end, mid, mid1;
    double clktck = (double)sysconf(_SC_CLK_TCK);
    start = times(&tms_t);
    mid1 = start;

    i = 0;

    while (i < r_len)
    {
        setnonblock(fd);
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        retval = select(fd + 1, &rfds, NULL, NULL, &tv);

        if (retval == 0)
        {
            logIT(LOG_ERR, "<RECV: read timeout");
            setblock(fd);
            logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
            return (-1);
        }
        else if (retval < 0)
        {
            if (errno == EINTR)
            {
                logIT(LOG_INFO, "<RECV: select interrupted - redo");
                continue;
            }
            else
            {
                logIT(LOG_ERR, "<RECV: select error %d", retval);
                setblock(fd);
                logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
                return (-1);
            }
        }
        else if (FD_ISSET(fd, &rfds))
        {
            len = read(fd, &r_buf[i], r_len - i);

            if (len == 0)
            {
                logIT(LOG_ERR, "<RECV: read eof");
                setblock(fd);
                logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
                return (-1);
            }
            else if (len < 0)
            {
                if (errno == EINTR)
                {
                    logIT(LOG_INFO, "<RECV: read interrupted - redo");
                    continue;
                }
                else
                {
                    logIT(LOG_ERR, "<RECV: read error %d", errno);
                    setblock(fd);
                    logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
                    return (-1);
                }
            }
            else
            {
                unsigned char byte = r_buf[i] & 255;
                mid = times(&tms_t);
                logIT(LOG_INFO, "<RECV: len=%zd %02X (%0.1f ms)", len, byte, ((double)(mid - mid1) / clktck) * 1000);
                mid1 = mid;
                i += (size_t)len;
            }
        }
        else
            continue;
    }

    end = times(&tms_t);
    *etime = (unsigned long)((double)(end - start) / clktck) * 1000;
    setblock(fd);
    logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
    return i;
}

static void sig_alrm(int signo)
{
    longjmp(env_alrm, 1);
}

int waitfor(int fd, char* w_buf, int w_len)
{
    int i;
    time_t start;
    char r_buf[MAXBUF];
    char hexString[128] = "\0";
    char dummy[3];
    unsigned long etime;

    for (i = 0; i < w_len; i++)
    {
        sprintf(dummy, "%02X", w_buf[i]);
        strncat(hexString, dummy, strlen(dummy));
    }

    logIT(LOG_INFO, "Warte auf %s", hexString);
    start = time(NULL);

    /* wir warten auf das erste Zeichen, danach muss es passen */
    do
    {
        etime = 0;

        if (receive(fd, r_buf, 1, &etime) < 0)
            return (0);

        if (time(NULL) - start > TIMEOUT)
        {
            logIT(LOG_WARNING, "Timeout wait");
            return (0);
        }
    }
    while (r_buf[0] != w_buf[0]);

    for (i = 1; i < w_len; i++)
    {
        etime = 0;

        if (receive(fd, r_buf, 1, &etime) < 0)
            return (0);

        if (time(NULL) - start > TIMEOUT)
        {
            logIT(LOG_WARNING, "Timeout wait");
            return (0);
        }

        if (r_buf[0] != w_buf[i])
        {
            logIT(LOG_ERR, "Synchronisation verloren");
            exit(1);
        }
    }

    /*	logIT(LOG_INFO,"Zeichenkette erkannt"); */
    return (1);
}

/* Write "n" bytes to a descriptor. */
ssize_t	writen(int fd, const void* vptr, size_t n)
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

ssize_t Writen(int fd, void* ptr, size_t nbytes)
{
    if (writen(fd, ptr, nbytes) != (ssize_t)nbytes)
    {
        logIT(LOG_ERR, "Fehler beim schreiben auf socket");
        return (0);
    }

    return (ssize_t)nbytes;
}

bool ReadLine(int fd, std::string* line)
{
    // We read-ahead, so we store in static buffer
    // what we already read, but not yet returned by ReadLine.
    static std::string buffer;

    // Do the real reading from fd until buffer has '\n'.
    std::string::iterator pos = buffer.begin();

    while ((pos = find(pos, buffer.end(), '\n')) == buffer.end())
    {
        char buf[3];
        int n = read(fd, buf, sizeof(buf) - 1);

        if (n == 0)
            return false;

        if (n == -1)
            throw std::runtime_error("ReadLine error");

        buf[n] = 0;

        n = buffer.size();
        buffer += buf;
        pos = buffer.begin() + n;
    }

    // Split the buffer around '\n' found and return first part.
    *line = std::string(buffer.begin(), pos);
    buffer = std::string(pos + 1, buffer.end());
    return true;
}
