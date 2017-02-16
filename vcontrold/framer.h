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
    class Framer
    {
    public:
        Framer(const std::string dev) : _device(dev) {};
        virtual ~Framer() {};

        int send(char* s_buf, size_t len);

        int waitfor(char* w_buf, int w_len);

        int receive(char* r_buf, int r_len, unsigned long* petime);

        int openDevice(char pid);

        void closeDevice();

        bool isOpen()
        {
            return _device.IsOpen();
        };

        Optolink& device()
        {
            return _device;
        };
    private:
        int close_p300();
        int open_p300();
        void set_actaddr(void* pdu);
        void reset_actaddr();
        int check_actaddr(void* pdu);
        void set_result(char result);
        int preset_result(char* r_buf, int r_len, unsigned long* petime);

        Optolink _device;
        uint16_t current_addr = FRAMER_NO_ADDR; // stored value depends on Endianess
        char pid = 0;	  // current active protocol
    };
}

#endif /* FRAMER_H */