#ifndef FRAMER_H
#define FRAMER_H

/*
 * Framer interface
 *
 * For P300 framing is supported here
 *
 * Control is by a controlling byte defining the action
 *
 * 2013-01-31 vheat
 */

/*
 * Main indentification of P300 Protocol is about leadin 0x41
 */
#include "optolink.h"
#include <stdint.h>

#define FRAMER_ERROR	0
#define FRAMER_SUCCESS	1
#define FRAMER_NO_ADDR	( (uint16_t) (-1))

namespace Vcontrold
{
    class FramerException : public Exception
    {
    public:
        FramerException(const std::string msg) : Exception(msg) {};
        virtual ~FramerException() {};
    };

    class Framer
    {
    public:
        Framer(const std::string dev) : _device(dev) {};
        virtual ~Framer() {};

        void Send(std::vector<uint8_t>& bytes);

        void WaitFor(std::vector<uint8_t>& bytes);

        std::vector<uint8_t> receive(size_t r_len);

        int OpenDevice(char pid);

        void CloseDevice();

        void ResetDevice();

        bool IsOpen();

        Optolink& Device()
        {
            return _device;
        };
    private:
        int close_p300();
        int open_p300();
        void SetActiveAddress(std::vector<uint8_t>& pdu);
        void ResetActiveAddress();
        bool CheckActiveAddress(std::vector<uint8_t>& pdu);

        Optolink _device;
        uint16_t _currentAddress = FRAMER_NO_ADDR; // stored value depends on Endianess
        char _pid = 0;	  // current active protocol
        uint8_t _linkStatus = 0;
    };
}

#endif /* FRAMER_H */