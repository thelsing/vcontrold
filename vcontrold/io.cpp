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

extern int debug;

















void WriteString(int fd, const std::string str)
{
    Write(fd, str);
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
    if (*(pos - 1) == '\r')
        *line = std::string(buffer.begin(), pos - 1);
    else
        *line = std::string(buffer.begin(), pos);

    buffer = std::string(pos + 1, buffer.end());
    return true;
}
