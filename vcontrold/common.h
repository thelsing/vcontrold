#ifndef COMMON_H
#define COMMON_H

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace Vcontrold
{
    class Exception : public std::exception
    {
    public:
        Exception(const std::string& msg) : _msg(msg) {};
        virtual ~Exception() {};
        virtual const char* what() const _GLIBCXX_USE_NOEXCEPT
        {
            return _msg.c_str();
        }
    private:
        std::string _msg;
    };
}

int initLog(int useSyslog, const char* logfile, int debugSwitch);
void logIT(int logclass, const char* string, ...);
void logIT(int logclass, const std::string& msg);
char hex2chr(char* hex);
size_t char2hex(char* outString, const char* charPtr, size_t len);
size_t string2chr(char* line, char* buf, size_t bufsize);
void sendErrMsg(int fd);
void setDebugFD(int fd);

#ifndef MAXBUF
    #define MAXBUF 4096
#endif

#endif /* COMMON_H */