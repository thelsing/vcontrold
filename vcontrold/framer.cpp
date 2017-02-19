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
#include <sstream>
#include <iomanip>

#include "common.h"
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

#define P300_LEADIN_OFFSET    0
#define P300_LEN_OFFSET       1
#define P300_TYPE_OFFSET      2
#define P300_FCT_OFFSET       3
#define P300_ADDR_OFFSET      4
#define P300_RESP_LEN_OFFSET  6
#define P300_BUFFER_OFFSET    7

#define P300_EXTRA_BYTES      3 // LEADIN + LEN + CHECKSUM
#define P300_RESP_EXTRA_BYTES 5 // TYPE + FCT + ADDR + RESP_LEN
#define P300_NOCRC_BYTES      2 // LEADIN + CHECKSUM

/*
 *  status handling of current command
 */
void  Vcontrold::Framer::SetActiveAddress(std::vector<uint8_t>& pdu)
{
    if (_currentAddress != FRAMER_NO_ADDR)
    {
        char string[100];
        snprintf(string, sizeof(string), ">FRAMER: addr was still active %04X", _currentAddress);
        logIT(LOG_ERR, string);
    }

    _currentAddress = *(uint16_t*)&pdu[P300_ADDR_OFFSET];
}

void Vcontrold::Framer::ResetActiveAddress()
{
    _currentAddress = FRAMER_NO_ADDR;
}

bool Vcontrold::Framer::CheckActiveAddress(std::vector<uint8_t>& pdu)
{
    return _currentAddress != *(uint16_t*)&pdu[P300_ADDR_OFFSET];
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
            _linkStatus = P300_NOT_INIT;
            logIT(LOG_INFO, ">FRAMER: closed");
            return FRAMER_SUCCESS;
        }
        else
        {
            logIT(LOG_ERR, ">FRAMER: unexpected data %02Xk", result[0]);
            // continue anyway
        }
    }

    _linkStatus = P300_ERROR;
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
            _linkStatus = P300_INIT_OK;
            logIT(LOG_INFO, ">FRAMER: opened");
            return FRAMER_SUCCESS;
        }
    }

    _linkStatus = P300_ERROR;
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

void Vcontrold::Framer::Send(std::vector<uint8_t>& payload)
{
    if (payload.size() < 1)
    {
        std::string msg(">FRAMER: invalid payload");
        logIT(LOG_ERR, msg);
        throw FramerException(msg);
    }

    if (_pid != P300_LEADIN)
    {
        _device.FlushReadAndSend(payload);
        return;
    }

    if (payload.size() < 3)
    {
        std::string msg(">FRAMER: too few for P300");
        logIT(LOG_ERR, msg);
        throw FramerException(msg);
    }

    std::vector<uint8_t> bytes;

    bytes.push_back(P300_LEADIN);
    bytes.push_back((uint8_t)payload.size());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    bytes.push_back(framer_chksum(&bytes[P300_LEN_OFFSET], payload.size() + 1));

    _device.FlushReadAndSend(bytes);

    std::vector<uint8_t> result = _device.ReadNBytes(1);

    if (result[0] != P300_INIT_OK)
    {
        std::stringstream msgStream;
        msgStream << ">FRAMER: Error " << std::hex << std::uppercase << std::setfill('0');
        msgStream << std::setw(2) << static_cast<int>(result[0]);
        std::string msg = msgStream.str();
        logIT(LOG_ERR, msg);
        throw FramerException(msg);
    }

    SetActiveAddress(bytes);
    logIT(LOG_DEBUG, ">FRAMER: Command send");
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

    if (_pid != P300_LEADIN)
    {
        result = _device.ReadNBytes(r_len);
        return result;
    }

    result = _device.ReadNBytes(r_len + P300_EXTRA_BYTES + P300_RESP_EXTRA_BYTES);
    int total = result[1] + P300_EXTRA_BYTES;

    char chk = framer_chksum(&result[P300_LEN_OFFSET], total - P300_NOCRC_BYTES);

    if (result.back() != chk)
    {
        ResetActiveAddress();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: read chksum error received 0x%02X calc 0x%02X", result.back(), chk);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }

    if (result[P300_TYPE_OFFSET] == P300_ERROR_REPORT)
    {
        ResetActiveAddress();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: ERROR address %02X%02X code %d",
                 result[P300_ADDR_OFFSET], result[P300_ADDR_OFFSET + 1],
                 result[P300_BUFFER_OFFSET]);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }

    if (r_len != result[P300_RESP_LEN_OFFSET])
    {
        ResetActiveAddress();
        char string[256];
        snprintf(string, sizeof(string), ">FRAMER: unexpected length %d %02X",
                 result[P300_RESP_LEN_OFFSET], result[P300_RESP_LEN_OFFSET]);
        logIT(LOG_ERR, string);
        throw FramerException(string);
    }


    // TODO: could add check for address receive matching address send before
    if (CheckActiveAddress(result))
    {
        ResetActiveAddress();
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
    ResetActiveAddress();
    return ret;
}

void Vcontrold::Framer::WaitFor(std::vector<uint8_t>& bytes)
{
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

    this->_pid = pid;
    return 1;
}

void Vcontrold::Framer::CloseDevice()
{
    if (_pid == P300_LEADIN)
        close_p300();

    _pid = 0;
    _device.CloseConnection();
}


void Vcontrold::Framer::ResetDevice()
{
    _device.ResetDevice();
    _linkStatus = P300_NOT_INIT;
    usleep(500000);
    OpenDevice(_pid);
}

bool Vcontrold::Framer::IsOpen()
{
    return _device.IsOpen() && _linkStatus == P300_INIT_OK;
}

