#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <vector>
#include <cstring>
#include <syslog.h>
#include <time.h>
#include <stdexcept>
#include <setjmp.h>
#include <signal.h>
#include <sys/times.h>
#include <sys/ioctl.h>
#include <sstream>

#include "common.h"
#include "io.h"
#include "optolink.h"

#define TIMEOUT 5

static char* dump(char* dest, const char* title, char* buf, size_t len)
{
    int pos = 0;
    size_t i;

    pos = sprintf(dest, "%s", title);

    for (i = 0; i < len; i++)
        pos += sprintf(dest + pos, " %02X", buf[i] & 0xff);

    return dest;
}

int Vcontrold::Optolink::my_send(char* s_buf, size_t len)
{
    char string[256];

    /* Buffer leeren */
    /* da tcflush nicht richtig funktioniert, verwenden wir nonblocking read */
    fcntl(_fd, F_SETFL, O_NONBLOCK);

    while (read(_fd, string, sizeof(string)) > 0);

    fcntl(_fd, F_SETFL, !O_NONBLOCK);

    tcflush(_fd, TCIFLUSH);

    std::vector<uint8_t> bytes(s_buf, s_buf + len);
    WriteBytes(bytes);

    for (size_t i = 0; i < len; i++)
    {

        unsigned char byte = s_buf[i] & 255;
        logIT(LOG_INFO, ">SEND: %02X", (int)byte);
    }

    return 1;
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


size_t Vcontrold::Optolink::receive_nb(char* r_buf, size_t r_len, unsigned long* etime)
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
        setnonblock(_fd);
        FD_ZERO(&rfds);
        FD_SET(_fd, &rfds);
        tv.tv_sec = TIMEOUT;
        tv.tv_usec = 0;
        retval = select(_fd + 1, &rfds, NULL, NULL, &tv);

        if (retval == 0)
        {
            logIT(LOG_ERR, "<RECV: read timeout");
            setblock(_fd);
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
                setblock(_fd);
                logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
                return (-1);
            }
        }
        else if (FD_ISSET(_fd, &rfds))
        {
            len = read(_fd, &r_buf[i], r_len - i);

            if (len == 0)
            {
                logIT(LOG_ERR, "<RECV: read eof");
                setblock(_fd);
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
                    setblock(_fd);
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
    setblock(_fd);
    logIT(LOG_INFO, dump(string, "<RECV: received", r_buf, i));
    return i;
}

void Vcontrold::Optolink::WaitFor(const std::vector<uint8_t>& bytes)
{
    char hexString[128] = "\0";
    char dummy[3];

    for (size_t i = 0; i < bytes.size(); i++)
    {
        sprintf(dummy, "%02X", bytes[i]);
        strncat(hexString, dummy, strlen(dummy));
    }

    logIT(LOG_INFO, "Warte auf %s", hexString);

    /* wir warten auf das erste Zeichen, danach muss es passen */
    std::vector<uint8_t> readBytes;

    do
    {
        readBytes = ReadBytes(1);
    }
    while (readBytes[0] != bytes[0]);

    for (size_t i = 1; i < bytes.size(); i++)
    {
        readBytes = ReadBytes(1);

        if (readBytes[0] != bytes[i])
        {
            logIT(LOG_ERR, "Synchronisation verloren");
            throw OptoLinkException("lost syncronisation");
        }
    }
}

void Vcontrold::Optolink::OpenConnection()
{
    logIT(LOG_INFO, std::string("konfiguriere serielle Schnittstelle ") + _devicePath);

    if ((_fd = open(_devicePath.c_str(), O_RDWR)) < 0)
    {
        std::string msg("cannot open ");
        msg += _devicePath;
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }

    int s;
    struct termios oldsb, newsb;
    s = tcgetattr(_fd, &oldsb);

    if (s < 0)
    {
        std::string msg("error tcgetattr ");
        msg += _devicePath;
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
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

    tcsetattr(_fd, TCSADRAIN, &newsb);

    /* DTR High fuer Spannungsversorgung */
    int modemctl = 0;
    ioctl(_fd, TIOCMGET, &modemctl);
    modemctl |= TIOCM_DTR;
    s = ioctl(_fd, TIOCMSET, &modemctl);

    if (s < 0)
    {
        std::string msg("error ioctl TIOCMSET ");
        msg += _devicePath;
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }
}

void Vcontrold::Optolink::CloseConnection()
{
    close(_fd);
    _fd = 0;
}

static jmp_buf	env_alrm;

static void sig_alrm(int signo)
{
    longjmp(env_alrm, 1);
}

std::vector<uint8_t> Vcontrold::Optolink::ReadBytes(int count)
{
    if (signal(SIGALRM, sig_alrm) == SIG_ERR)
        logIT(LOG_ERR, "SIGALRM error");

    if (setjmp(env_alrm) != 0)
        throw OptoLinkException("read timeout");

    std::vector<uint8_t> result(count);

    alarm(TIMEOUT);

    ssize_t	nleft;
    ssize_t	nread;
    int idx = 0;

    while (nleft > 0)
    {
        if ((nread = read(_fd, &result[idx], nleft)) < 0)
        {
            if (errno == EINTR)
                continue;
            else
            {
                alarm(0);
                throw OptoLinkException("read error");
            }
        }

        nleft -= nread;
        idx += nread;
    }

    alarm(0);
    return result;
}

void Vcontrold::Optolink::WriteBytes(const std::vector<uint8_t> bytes)
{
    Write(_fd, bytes);
}



