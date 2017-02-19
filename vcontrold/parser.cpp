/*
 * parser.c, Routinen fuer das Lesen von Kommandos
 *  */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <ctype.h>
#include <memory>

#include "xmlconfig.h"
#include "parser.h"
#include "conversion.h"
#include "common.h"
#include "io.h"
#include "framer.h"


static int parseLine(char* line, char* hex, int* hexlen, char* uSPtr, ssize_t uSPtrLen)
{
    int token = 0;

    if (strstr(line, "WAIT") == line)
        token = WAIT;
    else if (strstr(line, "SEND BYTES") == line)
        token = BYTES;
    else if (strstr(line, "SEND") == line)
        token = SEND;
    else if (strstr(line, "RECV") == line)
        token = RECV;
    else if (strstr(line, "PAUSE") == line)
        token = PAUSE;

    if (token)
    {
        /* wir parsen den Hex String und liefern ihn gewandelt zur�ck */
        /* erste Leerstelle suchen */
        char* ptr;
        ptr = strchr(line, ' ');

        if (!ptr)
            logIT(LOG_ERR, "Parse error, kein Leerzeichen gefunden: %s", line);
        else
        {
            /* wir nudeln den Rest der Zeile durch */
            char pString[MAXBUF];
            char* pPtr;

            while (*ptr != '\0')
            {
                int i;
                pPtr = pString;

                for (i = 0; i < MAXBUF; i++)
                    pPtr[i] = '\0';

                pPtr = pString;
                /* Leerstellen verdampfen */
                int endFound = 0;

                while (*++ptr == ' ')
                    if (*ptr == '\0')
                    {
                        endFound = 1;
                        break;
                    }

                if (endFound)
                    break;

                /* wir lesen nun die Zeichen ein bis zum naechsten Blank oder Ende */
                while ((*ptr != ' ') && (*ptr != '\0') && (*ptr != '\n') && (*ptr != '\r'))
                    *pPtr++ = *ptr++;

                if (!*pString)
                    break;

                *pPtr = '\0';
                ++*hexlen;

                /* wenn es sich um Pause oder Receive handelt, behandeln wir den Wert als Integer */
                if (token == PAUSE || token == RECV)
                {
                    *hexlen = atoi(pString);

                    /* Bei RECV kann man noch die Unit angeben, in die umgerechnet werden soll */
                    if (token == RECV)
                    {
                        pPtr = pString;

                        while (*ptr && (isdigit(*ptr) || isspace(*ptr)))
                            ptr++;

                        strncpy(uSPtr, ptr, uSPtrLen);
                    }

                    return (token);
                }
                else if (token == BYTES)   /* hier folgt noch die Unit */
                {
                    pPtr = pString;

                    while (*ptr && (isdigit(*ptr) || isspace(*ptr)))
                        ptr++;

                    strncpy(uSPtr, ptr, uSPtrLen);
                    return (token);
                }
                else
                    *hex++ = hex2chr(pString);

                *pString = '\0';

            }
        }



    }

    return (token);
}

int execByteCode(commandPtr cmdPtr, Vcontrold::Framer& framer, char* recvBuf, size_t recvLen,
                 char* sendBuf, size_t sendLen, short supressUnit)
{

    compilePtr cmpPtr = cmdPtr->cmpPtr;
    char string[256];
    char result[MAXBUF];
    size_t len;
    compilePtr cPtr = cmpPtr;
    unsigned long etime;
    char out_buff[1024];
    int _len = 0;
    int retry = cmdPtr->retry;

    /* wir wandeln zuerst die zu sendenen Bytes, nicht daas wir mittendrin abbrechen muessen */
    if (!supressUnit)
        while (cPtr)
        {
            if (cPtr->token != BYTES)
            {
                cPtr = cPtr->next;
                continue;
            }


            convertBack(cmdPtr, sendBuf, &len);


            if (cPtr->send)   /* wir haben das schon mal gesendet, der Speicher war noch alloziert */
            {
                free(cPtr->send);
                cPtr->send = NULL;
            }

            cPtr->send = (char*)calloc(len, sizeof(char));
            cPtr->len = len;
            sendLen = 0; /* wir senden nicht den ungewandelten sendBuf */
            memcpy(cPtr->send, sendBuf, len);

            cPtr = cPtr->next;
        }

    do
    {
        cPtr = cmpPtr; /* fuer die naechste Runde brauchen wir den Anfang */

        while (cmpPtr)
        {
            switch (cmpPtr->token)
            {
                case WAIT:
                {
                    std::vector<uint8_t> bytes(cmpPtr->send, cmpPtr->send + cmpPtr->len);
                    framer.WaitFor(bytes);

                    bzero(string, sizeof(string));
                    char2hex(string, cmpPtr->send, cmpPtr->len);

                    break;
                }


                case SEND:
                    _len = 0;

                    while (1)
                    {
                        memcpy(out_buff + _len, cmpPtr->send, cmpPtr->len);
                        _len += cmpPtr->len;

                        if (!(cmpPtr->next && cmpPtr->next->token == BYTES))
                            break;

                        cmpPtr = cmpPtr->next;
                    }

                    if (!framer.send(out_buff, _len))
                    {
                        logIT(LOG_ERR, "Fehler send, Abbruch");
                        return (-1);
                    }

                    bzero(string, sizeof(string));
                    char2hex(string, out_buff, _len);
                    break;

                case RECV:
                {
                    if (cmpPtr->len > recvLen)
                    {
                        logIT(LOG_ERR, "Recv Buffer zu klein. Ist: %d Soll %d", recvLen, cmpPtr->len);
                        cmpPtr->len = recvLen; /* hoffentlich kommen wir hier nicht hin */
                    }

                    etime = 0;
                    bzero(recvBuf, recvLen);

                    std::vector<uint8_t> resultVector = framer.receive(cmpPtr->len);
                    memcpy(recvBuf, &resultVector.front(), resultVector.size());

                    /* falls wir beim empfangen laenger als der Timeout gebraucht haben gehts in die
                    naechste Rune */
                    if (cmdPtr->recvTimeout && (etime > cmdPtr->recvTimeout))
                    {
                        logIT(LOG_NOTICE, "Recv Timeout: %ld ms  > %d ms (Retry: %d)", etime, cmdPtr->recvTimeout, (int)(retry - 1));

                        if (retry <= 1)
                        {
                            logIT(LOG_ERR, "Recv Timeout, Abbruch");
                            return (-1);
                        }

                        goto RETRY;
                    }

                    /*  falls ein errStr definiert ist, schauen wir mal ob das Ergebnis richtig ist */
                    if (cmpPtr->errStr && *cmpPtr->errStr)
                    {
                        if (memcmp(recvBuf, cmpPtr->errStr, cmpPtr->len) == 0)   /* falsche Antwort */
                        {
                            logIT(LOG_NOTICE, "Errstr matched, Ergebnis falsch (Retry:%d)", retry - 1);

                            if (retry <= 1)
                            {
                                logIT(LOG_ERR, "Ergebnis falsch, Abbruch");
                                return (-1);
                            }

                            goto RETRY;
                        }
                    }

                    bzero(string, sizeof(string));
                    char2hex(string, recvBuf, cmpPtr->len);
                    /* falls wir eine Unit haben (==uPtr) rechnen wir den
                     * empfangenen Wert um, und geben den umgerechneten Wert auch in uPtr zurueck */
                    bzero(result, sizeof(result));

                    if (!supressUnit)
                    {
                        std::shared_ptr<char> dataBuf((char*)malloc(cmdPtr->byteLength), free);
                        memcpy(dataBuf.get(), recvBuf + cmdPtr->bytePosition, cmdPtr->byteLength);

                        if (cmdPtr->bitLength)
                        {
                            char mask = 0;

                            for (char i = cmdPtr->bitPosition; i < (cmdPtr->bitPosition + cmdPtr->bitLength); i++)
                                mask |= (char)(1 << (7 - i % 8));

                            *dataBuf &= mask;
                            char shiftLength = (char)(8 - cmdPtr->bitPosition - cmdPtr->bitLength);
                            *dataBuf = (char)(*dataBuf >> shiftLength);
                        }

                        convertToString(cmdPtr, dataBuf.get(), cmdPtr->byteLength, result);

                        strncpy(recvBuf, result, recvLen);

                        return (0); /* 0==geawandelt nach unit */
                    }

                    return (cmpPtr->len);
                    break;
                }

                case PAUSE:
                    logIT(LOG_INFO, "Warte %i ms", cmpPtr->len);
                    usleep(cmpPtr->len * 1000L);
                    /* t_sleep.tv_sec=(time_t) cmpPtr->len / 1000;
                    t_sleep.tv_nsec=(long) cmpPtr->len * 1000000;
                    if (nanosleep(&t_sleep,&t_sleep_rem)==-1)
                    	nanosleep(&t_sleep_rem,NULL);
                    */
                    break;

                case BYTES:

                    /* wir senden den sendBuffer, der uebergeben wurde */
                    /* es fand keine Wandlung statt */
                    if (sendLen)
                    {
                        std::vector<uint8_t> bytes(sendBuf, sendBuf + sendLen);
                        framer.device().FlushReadAndSend(bytes);

                        char2hex(string, sendBuf, sendLen);
                    }
                    /* es ist eine Einheit definiert soll benutzt werden und wir haben das oben schon gewandelt */
                    else if (cmpPtr->len)
                    {
                        std::vector<uint8_t> bytes(cmpPtr->send, cmpPtr->send + cmpPtr->len);
                        framer.device().FlushReadAndSend(bytes);

                        bzero(string, sizeof(string));
                        char2hex(string, cmpPtr->send, cmpPtr->len);
                    }

                    break;

                default:
                    logIT(LOG_ERR, "unbekanntes Token: %d", cmpPtr->token);
                    return (-1);
            }

            cmpPtr = cmpPtr->next;
        }

RETRY:
        retry--;
        cmpPtr = cPtr; /* von vorne bitte */
    }
    while ((cmpPtr->errStr || cmdPtr->recvTimeout) && (retry > 0));

    return (0);
}

compilePtr newCompileNode(compilePtr ptr)
{
    compilePtr nptr;

    if (ptr && ptr->next)
        return (newCompileNode(ptr->next));

    nptr = (compilePtr)malloc(sizeof(Compile));
    memset(nptr, 0, sizeof(Compile));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    nptr->token = 0;
    nptr->len = 0;
    nptr->next = NULL;
    return (nptr);
}

void removeCompileList(compilePtr ptr)
{
    if (ptr && ptr->next)
        removeCompileList(ptr->next);

    if (ptr)
    {
        free(ptr->send);
        free(ptr);
    }
}

int expand(commandPtr cPtr, protocolPtr pPtr)
{

    /* Rekursion */
    if (!cPtr)
        return (0);

    if (cPtr->next)
        expand(cPtr->next, pPtr);


    /* falls keine Adresse gesetzt ist machen wir nichts */
    if (!cPtr->addrStr)
        return 0;


    char eString[2000];
    char* ePtr = eString;;
    char var[100];
    char name[100];
    char* ptr, *bptr;
    macroPtr mFPtr;
    char string[256];
    char* sendPtr;
    char* tmpPtr;

    icmdPtr iPtr;

    /* send des Kommand Pointers muss gebaut werden
    1. Suche Kommando pcmd bei den Kommandos des Protokolls */
    if (!(iPtr = (icmdPtr)getIcmdNode(pPtr->icPtr, cPtr->pcmd)))
    {
        logIT(LOG_ERR, "Protokoll Kommando %s (bei %s) nicht definiert", cPtr->pcmd, cPtr->name);
        exit(3);
    }

    /*	2. Parse die zeile und ersetze die Variablen durch Werte in cPtr */
    sendPtr = iPtr->send;

    if (!sendPtr)
        return (0);

    logIT(LOG_INFO, "protocmd Zeile: %s", sendPtr);
    bzero(eString, sizeof(eString));
    cPtr->retry = iPtr->retry; /* wir uebernehmen den Retry Wert aus des Protokoll Kommandos */
    cPtr->recvTimeout = iPtr->recvTimeout; /* dito fuer den Receive Timeout */

    do
    {
        ptr = sendPtr;

        while (*ptr++)
        {
            if ((*ptr == '$') || (*ptr == '\0'))
                break;
        }

        bptr = ptr;

        while (*bptr++)
        {
            if ((*bptr == ' ') || (*bptr == ';') || (*bptr == '\0'))
                break;
        }

        /* kopiere nicht umgewandelten String*/
        strncpy(ePtr, sendPtr, ptr - sendPtr);
        ePtr += ptr - sendPtr;
        bzero(var, sizeof(var));
        strncpy(var, ptr + 1, bptr - ptr - 1);

        /*		snprintf(string, sizeof(string),"   Var: %s",var);
        		logIT(LOG_INFO,string); */
        if (*var)   /* Haben wir uerbhaupt Variablen zu expandieren */
        {
            if (strstr(var, "addr") == var)
            {
                /* immer zwei Byte zusammen */
                bzero(string, sizeof(string));

                for (size_t i = 0; i < strlen(cPtr->addrStr) - 1; i += 2)
                {
                    strncpy(ePtr, cPtr->addrStr + i, 2);
                    ePtr += 2;
                    *ePtr++ = ' ';
                }

                ePtr--;
            }
            else if (strstr(var, "len") == var)
            {
                snprintf(string, sizeof(string), "%d", cPtr->blockLength);
                strncpy(ePtr, string, strlen(string));
                ePtr += strlen(string);
            }
            else if (strstr(var, "hexlen") == var)
            {
                snprintf(string, sizeof(string), "%02X", cPtr->blockLength);
                strncpy(ePtr, string, strlen(string));
                ePtr += strlen(string);
            }
            else
            {
                logIT(LOG_ERR, "Variable %s unbekannt", var);
                exit(3);
            }
        }

        sendPtr = bptr;
    }
    while (*sendPtr);

    logIT(LOG_INFO, "  Nach Ersetzung: %s", eString);
    tmpPtr = (char*)calloc(strlen(eString) + 1, sizeof(char));
    strcpy(tmpPtr, eString);

    /*	3. Expandiere die Zeile dann wie gehabt */

    sendPtr = tmpPtr;

    /* wir suchen nach woertern und schauen, ob es die als Macros gibt */

    bzero(eString, sizeof(eString));
    ePtr = eString;

    do
    {
        ptr = sendPtr;

        while (*ptr++)
        {
            if ((*ptr == ' ') || (*ptr == ';') || (*ptr == '\0'))
                break;
        }

        bzero(name, sizeof(name));
        strncpy(name, sendPtr, ptr - sendPtr);

        if ((mFPtr = getMacroNode(pPtr->mPtr, name)))
        {
            strncpy(ePtr, mFPtr->command, strlen(mFPtr->command));
            ePtr += strlen(mFPtr->command);
            *ePtr++ = *ptr;
        }
        else
        {
            strncpy(ePtr, sendPtr, ptr - sendPtr + 1);
            ePtr += ptr - sendPtr + 1;
        }

        sendPtr = ptr + 1;
    }
    while (*sendPtr);

    free(tmpPtr);
    logIT(LOG_INFO, "   nach EXPAND:%s", eString);

    if (!(cPtr->send = (char*)calloc(strlen(eString) + 1, sizeof(char))))
    {
        logIT(LOG_ERR, "calloc gescheitert");
        exit(1);
    }

    strcpy(cPtr->send, eString);
    return (1);
}

compilePtr buildByteCode(commandPtr cPtr)
{
    /* Rekursion */
    if (!cPtr)
        return (0);

    if (cPtr->next)
        buildByteCode(cPtr->next);

    /* falls keine Adresse gesetzt ist machen wir nichts */
    if (!cPtr->addrStr)
        return (0);

    char eString[2000];
    char cmd[200];
    char* ptr;

    char hex[MAXBUF];
    int hexlen;
    int token;
    char uString[100];
    char* uSPtr = uString;

    compilePtr cmpPtr, cmpStartPtr;

    cmpStartPtr = NULL;


    char* sendPtr;
    sendPtr = cPtr->send;

    if (!sendPtr) /* hier gibt es nichts zu tun */
        return (0);

    logIT(LOG_INFO, "BuildByteCode:%s", sendPtr);
    bzero(eString, sizeof(eString));

    do
    {
        ptr = sendPtr;

        while (*ptr++)
        {
            if ((*ptr == '\0') || (*ptr == ';'))
                break;
        }

        bzero(cmd, sizeof(cmd));
        strncpy(cmd, sendPtr, ptr - sendPtr);
        hexlen = 0;
        bzero(uSPtr, sizeof(uString));
        token = parseLine(cmd, hex, &hexlen, uString, sizeof(uString));
        logIT(LOG_INFO, "\t\tToken: %d Hexlen:%d, Unit: %s", token, hexlen, uSPtr);
        cmpPtr = newCompileNode(cmpStartPtr);

        if (!cmpStartPtr)
            cmpStartPtr = cmpPtr;

        cmpPtr->token = token;
        cmpPtr->len = hexlen;
        cmpPtr->errStr = cPtr->errStr;
        cmpPtr->send = (char*)calloc(hexlen, sizeof(char));
        memcpy(cmpPtr->send, hex, hexlen);
        sendPtr += strlen(cmd) + 1;
    }
    while (*sendPtr);

    cPtr->cmpPtr = cmpStartPtr;
    return (cmpStartPtr);
}
