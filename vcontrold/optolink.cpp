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
#include <stdlib.h>
#include <sstream>
#include <string>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <linux/limits.h>
#include <linux/usbdevice_fs.h>

#include "common.h"
#include "io.h"
#include "optolink.h"

#define TIMEOUT 5

void Vcontrold::Optolink::FlushReadAndSend(const std::vector<uint8_t>& bytes)
{
    /* Buffer leeren */
    /* da tcflush nicht richtig funktioniert, verwenden wir nonblocking read */
    fcntl(_fd, F_SETFL, O_NONBLOCK);

    char string[256];

    while (read(_fd, string, sizeof(string)) > 0)
        ;

    fcntl(_fd, F_SETFL, !O_NONBLOCK);

    tcflush(_fd, TCIFLUSH);

    WriteBytes(bytes);

    logIT(LOG_INFO, std::string(">SEND: "), bytes);
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


std::vector<uint8_t> Vcontrold::Optolink::ReadNBytes(size_t bytesToRead)
{
    size_t i;
    ssize_t len;

    fd_set rfds;
    struct timeval tv;
    int retval;

    struct tms tms_t;
    clock_t start, end;
    double clktck = (double)sysconf(_SC_CLK_TCK);
    start = times(&tms_t);

    i = 0;
    std::vector<uint8_t> result(bytesToRead);

    while (i < bytesToRead)
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
            logIT(LOG_INFO, std::string("<RECV: received "), result);
            throw OptoLinkException("Read Timeout");
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
                std::stringstream msgStream;
                msgStream << "<RECV: select error " << retval;
                std::string msg = msgStream.str();

                logIT(LOG_ERR, msg);
                setblock(_fd);
                logIT(LOG_INFO, std::string("<RECV: received "), result);
                throw OptoLinkException(msg);
            }
        }
        else if (FD_ISSET(_fd, &rfds))
        {
            len = read(_fd, &result[i], result.size() - i);

            if (len == 0)
            {
                std::string msg("<RECV: read eof");
                logIT(LOG_ERR, msg);
                setblock(_fd);
                logIT(LOG_INFO, std::string("<RECV: received "), result);
                throw OptoLinkException(msg);
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
                    std::stringstream msgStream;
                    msgStream << "<RECV: read error " << errno;
                    std::string msg = msgStream.str();
                    logIT(LOG_ERR, msg);
                    setblock(_fd);
                    logIT(LOG_INFO, std::string("<RECV: received "), result);
                    throw OptoLinkException(msg);
                }
            }
            else
                i += (size_t)len;
        }
        else
            continue;
    }

    end = times(&tms_t);
    double etime = ((double)(end - start) / clktck) * 1000;
    setblock(_fd);

    std::stringstream msgStream;
    msgStream << "<RECV: received (" << etime << " ms ) ";
    logIT(LOG_INFO, msgStream.str(), result);

    return result;
}

void Vcontrold::Optolink::WaitFor(const std::vector<uint8_t>& bytes)
{
    logIT(LOG_INFO, std::string("Warte auf "), bytes);

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

void Vcontrold::Optolink::ResetDevice()
{
    CloseConnection();

    int fd;
    int rc;
    char buff[PATH_MAX];
    ssize_t retVal;

    std::stringstream pathBuff;
    pathBuff << "/sys/dev/";

    struct stat fs;
    stat(_devicePath.c_str(), &fs);

    if (S_ISCHR(fs.st_mode))
        pathBuff << "char/";
    else
        pathBuff << "block/";

    pathBuff << major(fs.st_rdev);
    pathBuff << ":";
    pathBuff << minor(fs.st_rdev);

    std::string path = pathBuff.str();
    realpath(path.c_str(), buff);

    pathBuff.str("");
    pathBuff.clear();
    pathBuff << buff << "/../../../..";
    path = pathBuff.str();
    realpath(path.c_str(), buff);
    path = buff;

    pathBuff.str("");
    pathBuff.clear();
    pathBuff << path << "/busnum";


    std::string tmp = pathBuff.str();

    fd = open(tmp.c_str(), O_RDONLY);

    if (fd < 0)
    {
        std::string msg("error reading file busnum");
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }

    retVal = read(fd, buff, 3);
    buff[retVal] = 0;
    close(fd);
    int busNum = atoi(buff);

    pathBuff.str("");
    pathBuff.clear();
    pathBuff << path << "/devnum";

    tmp = pathBuff.str();

    fd = open(tmp.c_str(), O_RDONLY);

    if (fd < 0)
    {
        std::string msg("error reading file devnum");
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }

    retVal = read(fd, buff, 3);
    buff[retVal] = 0;
    close(fd);
    int devNum = atoi(buff);


    pathBuff.str("");
    pathBuff.clear();
    pathBuff << "/dev/bus/usb/";
    sprintf(buff, "%03d", busNum);
    pathBuff << buff << "/";
    sprintf(buff, "%03d", devNum);
    pathBuff << buff;

    path = pathBuff.str();
    const char* filename = path.c_str();

    fd = open(filename, O_WRONLY);

    if (fd < 0)
    {
        std::string msg("Error opening output file");
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }

    logIT(LOG_INFO, "Resetting USB device %s", filename);
    rc = ioctl(fd, USBDEVFS_RESET, 0);

    if (rc < 0)
    {
        std::string msg("Error in ioctl");
        logIT(LOG_ERR, msg);
        throw OptoLinkException(msg);
    }

    logIT(LOG_INFO, "Reset successful");

    close(fd);
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

void Vcontrold::Optolink::WriteBytes(const std::vector<uint8_t>& bytes)
{
    Write(_fd, bytes);
}



