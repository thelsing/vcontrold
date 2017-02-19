#ifndef OPTOLINK
#define OPTOLINK

#include <string>
#include <vector>
#include "common.h"

namespace Vcontrold
{
    class OptoLinkException : public Exception
    {
    public:
        OptoLinkException(const std::string msg) : Exception(msg) {};
        virtual ~OptoLinkException() {};
    };

    class Optolink
    {
    public:
        Optolink(const std::string& devicePath) : _devicePath(devicePath) {};
        virtual ~Optolink() {};

        void FlushReadAndSend(const std::vector<uint8_t>& bytes);
        std::vector<uint8_t> ReadNBytes(size_t bytesToRead);

        void OpenConnection();
        void CloseConnection();

        std::vector<uint8_t> ReadBytes(int count);
        void WriteBytes(const std::vector<uint8_t>& bytes);
        void WaitFor(const std::vector<uint8_t>& bytes);

        const std::string& DevicePath()
        {
            return _devicePath;
        }

        bool IsOpen()
        {
            return _fd > 0;
        }
    private:
        int _fd = 0;
        const std::string _devicePath;
    };
}

#endif // OPTOLINK
