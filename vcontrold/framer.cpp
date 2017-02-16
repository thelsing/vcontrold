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

int Vcontrold::Framer::check_actaddr(void* pdu)
{
    char string[100];

    if (current_addr != *(uint16_t*)(((char*)pdu) + P300_ADDR_OFFSET))
    {
        snprintf(string, sizeof(string), ">FRAMER: addr corrupted stored %04X, now %04X",
                 current_addr,
                 *(uint16_t*)(((char*)pdu) + P300_ADDR_OFFSET));
        return -1;
    }

    return 0;
}

//TODO: could cause trouble on addr containing 0xFE
void Vcontrold::Framer::set_result(char result)
{
    current_addr = (uint16_t) FRAMER_LINK_STATUS(result);
}

int Vcontrold::Framer::preset_result(char* r_buf, int r_len, unsigned long* petime)
{
    char string[100];

    if ((pid == P300_LEADIN) && ((current_addr & FRAMER_LINK_STATUS(0)) == FRAMER_LINK_STATUS(0)))
    {
        r_buf[0] = (char)(current_addr ^ FRAMER_LINK_STATUS(0));
        snprintf(string, sizeof(string), ">FRAMER: preset result %02X", r_buf[0]);
        logIT(LOG_INFO, string);
        return FRAMER_SUCCESS;
    }

    snprintf(string, sizeof(string), ">FRAMER: no preset result");
    logIT(LOG_INFO, string);
    return FRAMER_ERROR;
}

/*
 * synchronisation for P300 + switch to P300, back to normal for close -> repeating 05
 */
int Vcontrold::Framer::close_p300()
{
    char string[100];
    int i;
    char wbuf = P300_RESET;
    char rbuf = 0;
    unsigned long etime;
    int rlen;

    for (i = 0; i < P300X_ATTEMPTS; i++)
    {
        if (!_device.my_send(&wbuf, 1))
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: reset not send");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;;
        }

        etime = 0;
        rlen = _device.receive_nb(&rbuf, 1, &etime);

        if (rlen < 0)
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: close read failure");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if (rlen == 0)
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: close read timeout for ack");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if ((rbuf == P300_INIT_OK) || (rbuf == P300_NOT_INIT))
        {
            set_result(P300_NOT_INIT);
            snprintf(string, sizeof(string), ">FRAMER: closed");
            logIT(LOG_INFO, string);
            return FRAMER_SUCCESS;
        }
        else
        {
            snprintf(string, sizeof(string), ">FRAMER: unexpected data %02Xk", rbuf);
            logIT(LOG_ERR, string);
            // continue anyway
        }
    }

    set_result(P300_ERROR);
    snprintf(string, sizeof(string), ">FRAMER: could not close (%d attempts)", P300X_ATTEMPTS);
    logIT(LOG_ERR, string);
    return FRAMER_ERROR;
}

int Vcontrold::Framer::open_p300()
{
    char string[100];
    int i;
    char rbuf = 0;
    char enable[] = P300_ENABLE;
    unsigned long etime;
    int rlen;

    if (!close_p300())
    {
        snprintf(string, sizeof(string), ">FRAMER: could not set start cond");
        logIT(LOG_ERR, string);
        return FRAMER_ERROR;
    }

    for (i = 0; i < P300X_ATTEMPTS; i++)
    {
        if (!_device.my_send(enable, sizeof(enable)))
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: enable not send");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;;
        }

        etime = 0;
        rlen = _device.receive_nb(&rbuf, 1, &etime);

        if (rlen < 0)
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: enable read failure");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if (rlen == 0)
        {
            set_result(P300_ERROR);
            snprintf(string, sizeof(string), ">FRAMER: enable read timeout for ack");
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if (rbuf == P300_INIT_OK)
        {
            set_result(P300_INIT_OK);
            snprintf(string, sizeof(string), ">FRAMER: opened");
            logIT(LOG_INFO, string);
            return FRAMER_SUCCESS;
        }
    }

    set_result(P300_ERROR);
    snprintf(string, sizeof(string), ">FRAMER: could not close (%d attempts)", P300X_ATTEMPTS);
    logIT(LOG_ERR, string);
    return FRAMER_ERROR;
}

/*
 * calculation check sum for P300,
 * assuming buffer is frame and starts by P300_LEADIN
 */
static char framer_chksum(char* buf, int len)
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
    return (char)sum % 256;
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
    char string[256];

    if ((len < 1) || (!s_buf))
    {
        snprintf(string, sizeof(string), ">FRAMER: invalid buffer %d %p", len, s_buf);
        logIT(LOG_ERR, string);
        return FRAMER_ERROR;
    }

    if (pid != P300_LEADIN)
        return _device.my_send(s_buf, len);
    else if (len < 3)
    {
        snprintf(string, sizeof(string), ">FRAMER: too few for P300");
        logIT(LOG_ERR, string);
        return FRAMER_ERROR;
    }
    else
    {
        size_t pos = 0;
        char l_buf[256];
        unsigned long etime;
        int rlen;

        l_buf[0] = P300_LEADIN;
        l_buf[1] = (char)len; // only payload but len contains other bytes
        l_buf[2] = s_buf[0]; // type
        l_buf[3] = s_buf[1]; // function

        for (pos = 0; pos < (len - 2); pos++)
            l_buf[P300_ADDR_OFFSET + pos] = s_buf[pos + 2];

        l_buf[P300_ADDR_OFFSET + pos] = framer_chksum(l_buf + 1,
                                        len + P300_EXTRA_BYTES - 2);

        if (!_device.my_send(l_buf, len + P300_EXTRA_BYTES))
        {
            snprintf(string, sizeof(string), ">FRAMER: write failure %d",
                     len + P300_EXTRA_BYTES);
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }

        etime = 0;
        rlen = _device.receive_nb(l_buf, 1, &etime);

        if (rlen < 0)
        {
            snprintf(string, sizeof(string), ">FRAMER: read failure %d", pos + 1);
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if (rlen == 0)
        {
            snprintf(string, sizeof(string), ">FRAMER: timeout for ack %d", pos + 1);
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }
        else if (*l_buf != P300_INIT_OK)
        {
            snprintf(string, sizeof(string), ">FRAMER: Error %02X", *l_buf);
            logIT(LOG_ERR, string);
            return FRAMER_ERROR;
        }

        set_actaddr(l_buf);
        snprintf(string, sizeof(string), ">FRAMER: Command send");
        logIT(LOG_DEBUG, string);
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

int Vcontrold::Framer::receive(char* r_buf, int r_len, unsigned long* petime)
{
    char string[256];
    int rlen;
    int total;
    int rtmp;
    char l_buf[256];
    unsigned long etime;
    char chk;

    l_buf[4] = 0;
    l_buf[5] = 0; // to identify TimerWWMi bug

    if ((r_len < 1) || (!r_buf))
    {
        snprintf(string, sizeof(string), ">FRAMER: invalid read buffer %d %p", r_len, r_buf);
        logIT(LOG_ERR, string);
        return FRAMER_ERROR;
    }

    if (preset_result(r_buf, r_len, petime))
    {
        reset_actaddr();
        return FRAMER_SUCCESS;
    }

    *petime = 0;
    rtmp = _device.receive_nb(l_buf, r_len, petime);

    if (rtmp < 0)
    {
        snprintf(string, sizeof(string), ">FRAMER: read failure");
        logIT(LOG_ERR, string);
        reset_actaddr();
        return FRAMER_READ_ERROR;
    }
    else if (rtmp == 0)
    {
        snprintf(string, sizeof(string), ">FRAMER: read timeout");
        logIT(LOG_ERR, string);
        reset_actaddr();
        return FRAMER_READ_TIMEOUT;
    }
    else if (pid != P300_LEADIN)
    {
        // no P300 frame, just forward
        for (rlen = 0; rlen < r_len; rlen++)
            r_buf[rlen] = l_buf[rlen];

        return rtmp;
    }

    // this is not GWG / KW we know now
    etime = 0;

    // read at least the length info
    if (rtmp < 2)
    {
        rlen = _device.receive_nb(l_buf + 1, 1, &etime);
        *petime += etime;

        if (rlen < 0)
        {
            reset_actaddr();
            snprintf(string, sizeof(string), ">FRAMER: read failure");
            logIT(LOG_ERR, string);
            return FRAMER_READ_ERROR;
        }
        else if (rlen == 0)
        {
            reset_actaddr();
            snprintf(string, sizeof(string), ">FRAMER: read timeout");
            logIT(LOG_ERR, string);
            return FRAMER_READ_TIMEOUT;
        }

        rtmp += rlen;
    }

    total = l_buf[1] + P300_EXTRA_BYTES;
    rlen = total - rtmp;

    if (rlen <= 0)
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: strange read %d", rlen);
        logIT(LOG_ERR, string);
        return rtmp; // strange should not happen here
    }

    // now read what is extra
    rtmp = _device.receive_nb(l_buf + rtmp, rlen, &etime);
    *petime += etime;

    // bug in Vitotronic getTimerWWMi, we got it , but complete
    if ((l_buf[4] == 0x21) && (l_buf[5] == 0x10) && (rtmp == -1))
    {
        snprintf(string, sizeof(string), ">FRAMER: bug of getTimerWWMi - omit checksum");
        logIT(LOG_ERR, string);
        goto except;
    }

    if (rtmp < 0)
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: read final failure");
        logIT(LOG_ERR, string);
        return FRAMER_READ_ERROR;
    }
    else if (rtmp == 0)
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: read final timeout");
        logIT(LOG_ERR, string);
        return FRAMER_READ_TIMEOUT;
    }

    chk = framer_chksum(l_buf + 1, total - 2);

    if (l_buf[total - 1] != chk)
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: read chksum error received 0x%02X calc 0x%02X", (unsigned char)l_buf[total - 1], (unsigned char)chk);
        logIT(LOG_ERR, string);
        return FRAMER_READ_ERROR;
    }

except:

    if (l_buf[P300_TYPE_OFFSET] == P300_ERROR_REPORT)
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: ERROR address %02X%02X code %d",
                 l_buf[P300_ADDR_OFFSET], l_buf[P300_ADDR_OFFSET + 1],
                 l_buf[P300_BUFFER_OFFSET]);
        logIT(LOG_ERR, string);
        return FRAMER_READ_ERROR;
    }
    else
    {
        if (r_len != l_buf[P300_RESP_LEN_OFFSET])
        {
            reset_actaddr();
            snprintf(string, sizeof(string), ">FRAMER: unexpected length %d %02X",
                     l_buf[P300_RESP_LEN_OFFSET], l_buf[P300_RESP_LEN_OFFSET]);
            logIT(LOG_ERR, string);
            return FRAMER_READ_ERROR;
        }
    }

    // TODO: could add check for address receive matching address send before
    if (check_actaddr(l_buf))
    {
        reset_actaddr();
        snprintf(string, sizeof(string), ">FRAMER: not matching response addr");
        logIT(LOG_ERR, string);
        return FRAMER_READ_ERROR;
    }

    if ((l_buf[P300_FCT_OFFSET] == P300_WRITE_DATA) && (r_len == 1))
    {
        // if we have a P300 setaddr we do not get data back ...
        if (l_buf[P300_TYPE_OFFSET] == P300_RESPONSE)
        {
            // OK
            r_buf[rtmp] = 0x00;
        }
        else
        {
            // NOT OK
            r_buf[rtmp] = 0x01;
        }
    }
    else
    {
        for (rtmp = 0; rtmp < r_len; rtmp++)
            r_buf[rtmp] = l_buf[P300_BUFFER_OFFSET + rtmp];
    }

    reset_actaddr();
    return r_len;
}

int Vcontrold::Framer::waitfor(char* w_buf, int w_len)
{

    unsigned long etime;

    if (preset_result(w_buf, w_len, &etime))
    {
        reset_actaddr();
        return FRAMER_SUCCESS;
    }

    std::vector<uint8_t> bytes(w_buf, w_buf + w_len);
    _device.WaitFor(bytes);
    return FRAMER_SUCCESS;
}

/*
 * Device handling,
 * with open and close the mode is also switched to P300/back
 */
int Vcontrold::Framer::openDevice(char pid)
{
    char string[100];
    int fd;

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
    return fd;
}

void Vcontrold::Framer::closeDevice()
{
    if (pid == P300_LEADIN)
        close_p300();

    pid = 0;
    _device.CloseConnection();
}

