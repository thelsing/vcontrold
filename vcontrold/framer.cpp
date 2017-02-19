/*
 * Framer interface
 *
 * For P300 framing is supported here
 *
 * Control is by a controlling byte P300_LEADING = 0x41
 *
 * with open and close P300 Mode is switched on, there is new xml-tag <pid> with protocol
 * definition which controls the switching of vitotronic to P300 mode.
 * additional assuming PDUs start by P300_LEADIN, else transferred as send by client
 * todo: when PID is set, there is no need for defining a controlling x41 in getaddr etc.
 *
 * semaphore handling in vcontrol.c is changed to cover all from open until close to avoid
 * disturbance by other client trying to do uncoordinated open/close
 *
 * 2013-01-31 vheat
 */

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "common.h"
#include "io.h"
#include "framer.h"

// general marker of P300 protocol
#define P300_LEADIN 	0x41
#define P300_RESET      0x04
#define P300_ENABLE     { 0x16, 0x00, 0x00 };

// message type
#define P300_REQUEST 		0x00
#define P300_RESPONSE 		0x01
#define P300_ERROR_REPORT	0x03
#define P300X_LINK_MNT      0x0f

// function
#define P300_READ_DATA 		0x01
#define P300_WRITE_DATA		0x02
#define P300_FUNCT_CALL 	0x07

#define P300X_OPEN          0x01
#define P300X_CLOSE         0x00
#define P300X_ATTEMPTS		3
// response
#define P300_ERROR 			0x15
#define P300_NOT_INIT		0x05
#define P300_INIT_OK		0x06

#define P300_TYPE_OFFSET      2
#define P300_FCT_OFFSET       3
#define P300_ADDR_OFFSET      4
#define P300_RESP_LEN_OFFSET  6
#define P300_BUFFER_OFFSET    7
#define P300_EXTRA_BYTES      3

#define FRAMER_READ_ERROR	    (-1)
#define FRAMER_LINK_STATUS(st)  (0xFE00 + st)
#define FRAMER_READ_TIMEOUT     0

#define FRAMER_ADDR(pdu)	( *(uint16_t *)(((char *)pdu) + 6 ))  // cast [6] +[7] to uint8, even if no char buf
#define FRAMER_SET_ADDR(pdu) { framer_current_addr =

// current active command


/*
 *  status handling of current command
 */
void  Vcontrold::Framer::set_actaddr(void* pdu)
{
    char string[100];

    if (current_addr != FRAMER_NO_ADDR)
    {
        snprintf(string, sizeof(string), ">FRAMER: addr was still active %04X", current_addr);
        logIT(LOG_ERR, string);
    }


    current_addr = *(uint16_t*)(((char*)pdu) + P300_ADDR_OFFSET);
}

void Vcontrold::Framer::reset_actaddr()
{
    current_addr = FRAMER_NO_ADDR;
}

bool Vcontrold::Framer::CheckActAddr(std::vector<uint8_t> pdu)
{
    return current_addr != *(uint16_t*)&pdu[P300_ADDR_OFFSET];
}

//TODO: could cause trouble on addr containing 0xFE
void Vcontrold::Framer::set_result(char result)
{
    current_addr = (uint16_t)FRAMER_LINK_STATUS(result);
}

bool Vcontrold::Framer::PresetResult(std::vector<uint8_t> result)
{
    if (pid != P300_LEADIN || ((current_addr & FRAMER_LINK_STATUS(0)) != FRAMER_LINK_STATUS(0)))
    {
        std::string msg(">FRAMER: no preset result");
        logIT(LOG_INFO, msg);
        return false;
    }

    result[0] = (uint8_t)(current_addr ^ FRAMER_LINK_STATUS(0));
    logIT(LOG_INFO, ">FRAMER: preset result %02X", result[0]);
    return true;
}

/*
 * synchronisation for P300 + switch to P300, back to normal for close -> repeating 05
 */
int Vcontrold::Framer::close_p300()
{
    char wbuf = P300_RESET;

    for (int i = 0; i < P300X_ATTEMPTS; i++)
    {
        std::vector<uint8_t> bytes(&wbuf, &wbuf + 1);
        _device.FlushReadAndSend(bytes);

        std::vector<uint8_t> result = _device.ReadNBytes(1);

        if ((result[0] == P300_INIT_OK) || (result[0] == P300_NOT_INIT))
        {
            set_result(P300_NOT_INIT);
            logIT(LOG_INFO, ">FRAMER: closed");
            return FRAMER_SUCCESS;
        }
        else
        {
            logIT(LOG_ERR, ">FRAMER: unexpected data %02Xk", result[0]);
            // continue anyway
        }
    }

    set_result(P300_ERROR);
    logIT(LOG_ERR, ">FRAMER: could not close (%d attempts)", P300X_ATTEMPTS);
    return FRAMER_ERROR;
}

int Vcontrold::Framer::open_p300()
{
    char enable[] = P300_ENABLE;

    if (!close_p300())
    {
        logIT(LOG_ERR, ">FRAMER: could not set start cond");
        return FRAMER_ERROR;
    }

    for (int i = 0; i < P300X_ATTEMPTS; i++)
    {
        std::vector<uint8_t> bytes(enable, enable + sizeof(enable));
        _device.FlushReadAndSend(bytes);

        std::vector<uint8_t> result = _device.ReadNBytes(1);

        if (result[0] == P300_INIT_OK)
        {
            set_result(P300_INIT_OK);
            logIT(LOG_INFO, ">FRAMER: opened");
            return FRAMER_SUCCESS;
        }
    }

    set_result(P300_ERROR);
    logIT(LOG_ERR, ">FRAMER: could not close (%d attempts)", P300X_ATTEMPTS);
    return FRAMER_ERROR;
}

/*
 * calculation check sum for P300,
 * assuming buffer is frame and starts by P300_LEADIN
 */
static uint8_t framer_chksum(uint8_t* buf, int len)
{
    int sum = 0;

    while (len)
    {
        sum += *buf;
        //printf("framer chksum %d %02X %02X\n", len, *buf, sum);
        buf++;
        len--;
    }

    //printf("framer chksum %02x\n", sum);
    return (uint8_t)sum % 256;
}
/*
 * Frame a message in case P300 protocol is indicated
 *
 * Return 0: Error, else length of written bytes
 * This routine reads the first response byte for success status
 *
 * Format
 * Downlink
 * to framer
 * | LEADIN | type | function | addr | exp len |
 * to Vitotronic
 * | LEADIN | payload len | type | function | addr | exp len | chk |
 */

int Vcontrold::Framer::send(char* s_buf, size_t len)
{
    if ((len < 1) || (!s_buf))
    {
        logIT(LOG_ERR, ">FRAMER: invalid buffer %d %p", len, s_buf);
        return FRAMER_ERROR;
    }

    if (pid != P300_LEADIN)
    {
        std::vector<uint8_t> bytes(s_buf, s_buf + len);
        _device.FlushReadAndSend(bytes);
        return len;
    }
    else if (len < 3)
    {
        logIT(LOG_ERR, ">FRAMER: too few for P300");
        return FRAMER_ERROR;
    }
    else
    {
        size_t pos = 0;
        uint8_t l_buf[256];

        l_buf[0] = P300_LEADIN;
        l_buf[1] = (uint8_t)len; // only payload but len contains other bytes
        l_buf[2] = s_buf[0]; // type
        l_buf[3] = s_buf[1]; // function

        for (pos = 0; pos < (len - 2); pos++)
            l_buf[P300_ADDR_OFFSET + pos] = s_buf[pos + 2];

        l_buf[P300_ADDR_OFFSET + pos] = framer_chksum(l_buf + 1,
                                        len + P300_EXTRA_BYTES - 2);

        std::vector<uint8_t> bytes(l_buf, l_buf + len + P300_EXTRA_BYTES);
        _device.FlushReadAndSend(bytes);

        std::vector<uint8_t> result = _device.ReadNBytes(1);

        if (result[0] != P300_INIT_OK)
        {
            logIT(LOG_ERR, ">FRAMER: Error %02X", result[0]);
            return FRAMER_ERROR;
        }

        set_actaddr(l_buf);
        logIT(LOG_DEBUG, ">FRAMER: Command send");
        return FRAMER_SUCCESS;
    }
}

/*
 * Read a framed framed message in case P300 protocol is indicated
 *
 * Return 0: Error, else length of written bytes
 * This routine reads the first response byte for success status
 *
 * Format
 * from Vitotronic
 * | LEADIN | payload len | type | function | addr | exp len | chk |
 * Uplink
 * from framer
 * | data |
 * This simulates KW return, respective checking of the frame is done in this function
 *
 * etime is forwarded
 * return is FRAMER_ERROR, FRAMER_TIMEOUT or read len
 *
 * WEAKNESS:
 * If any other protocol gets an aswer beginning x41, then this will return errorneous
 * KW may have values returned starting 0x41
 */

std::vector<uint8_t> Vcontrold::Framer::receive(size_t r_len)
{
    std::vector<uint8_t> result(r_len);

    if (r_len < 1)
    {
        std::string msg(">FRAMER: invalid read length");
        logIT(LOG_ERR, msg);
        throw FramerException(msg);
    }

    if (PresetResult(result))
    {
        reset_actaddr();
        return result;
    }

    if (pid != P300_LEADIN)
    {
        result = _device.ReadNBytes(r_len);
        return result;
    }

    result = _device.ReadNBytes(r_len + 8);
    int total = result[1] + P300_EXTRA_BYTES;

    char chk = framer_chksum(&result[1], total - 2);

    if (result[total - 1] != chk)
    {
        reset_actaddr();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: read chksum error received 0x%02X calc 0x%02X", result[total - 1], chk);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }

    if (result[P300_TYPE_OFFSET] == P300_ERROR_REPORT)
    {
        reset_actaddr();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: ERROR address %02X%02X code %d",
                 result[P300_ADDR_OFFSET], result[P300_ADDR_OFFSET + 1],
                 result[P300_BUFFER_OFFSET]);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }

    if (r_len != result[P300_RESP_LEN_OFFSET])
    {
        reset_actaddr();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: unexpected length %d %02X",
                 result[P300_RESP_LEN_OFFSET], result[P300_RESP_LEN_OFFSET]);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }


    // TODO: could add check for address receive matching address send before
    if (CheckActAddr(result))
    {
        reset_actaddr();
        std::string msg(">FRAMER: not matching response addr");
        logIT(LOG_ERR, msg);
        throw FramerException(msg);
    }

    if ((result[P300_FCT_OFFSET] == P300_WRITE_DATA) && (r_len == 1))
    {
        // if we have a P300 setaddr we do not get data back ...
        if (result[P300_TYPE_OFFSET] == P300_RESPONSE)
        {
            // OK
            result[r_len] = 0x00;
        }
        else
        {
            // NOT OK
            result[r_len] = 0x01;
        }

        return result;
    }

    std::vector<uint8_t> ret(&result[P300_BUFFER_OFFSET], &result[P300_BUFFER_OFFSET + r_len]);
    reset_actaddr();
    return ret;
}

void Vcontrold::Framer::WaitFor(std::vector<uint8_t> bytes)
{
    if (PresetResult(bytes))
    {
        reset_actaddr();
        return;
    }

    _device.WaitFor(bytes);
}

/*
 * Device handling,
 * with open and close the mode is also switched to P300/back
 */
int Vcontrold::Framer::OpenDevice(char pid)
{
    char string[100];

    snprintf(string, sizeof(string), ">FRAMER: open device %s ProtocolID %02X", _device.DevicePath().c_str(), pid);
    logIT(LOG_INFO, string);

    _device.OpenConnection();

    if (pid == P300_LEADIN)
    {
        if (!open_p300())
        {
            _device.CloseConnection();
            return -1;
        }
    }

    this->pid = pid;
    return 1;
}

void Vcontrold::Framer::CloseDevice()
{
    if (pid == P300_LEADIN)
        close_p300();

    pid = 0;
    _device.CloseConnection();
}

