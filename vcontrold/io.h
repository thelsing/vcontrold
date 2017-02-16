#ifndef IO_H
#define IO_H

#include <string>
#include <vector>
#include <cstdlib>
#include <stdexcept>

template <typename T>
void Write(int fd, T bytes)
{
    ssize_t	left = bytes.size();
    ssize_t	idx = 0;

    while (left > 0)
    {
        ssize_t written = write(fd, &bytes[idx], left);

        if (written <= 0)
        {
            if (errno == EINTR)
                continue;
            else
                throw std::runtime_error("error in Write<T>");
        }

        left -= written;
        idx += written;
    }
}

void WriteString(int fd, const std::string str);
bool ReadLine(int fd, std::string* line);
#endif /* IO_H */
