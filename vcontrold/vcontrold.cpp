/*
 * Vito-Control Daemon fuer Vito- Abfrage
 *  */
/* $Id: vcontrold.c 34 2008-04-06 19:39:29Z marcust $ */
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <stdexcept>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>


#include"io.h"
#include "common.h"
#include "xmlconfig.h"
#include "parser.h"
#include "socket.h"
#include "semaphore.h"
#include "framer.h"

const char* VERSION_DAEMON = "0.98 TK";

/* Globale Variablen */
#ifdef __CYGWIN__
    const char* xmlfile = "vcontrold.xml";
    const char* INIOUTFILE = "sim-%s.ini";
#else
    const char* xmlfile = "/etc/vcontrold/vcontrold.xml";
    const char* INIOUTFILE = "/tmp/sim-%s.ini";
#endif
FILE* iniFD = NULL;
int makeDaemon = 1;
int inetversion = 0;
pthread_mutex_t device_mutex;
pthread_mutex_t config_mutex;

/* in xmlconfig.c definiert */
extern protocolPtr protoPtr;
extern unitPtr uPtr;
extern devicePtr devPtr;
extern configPtr cfgPtr;

typedef struct _thread_args
{
    int sock_fd;
    char device[20];
} thread_args;

void usage()
{
    printf("usage: vcontrold [-x|--xmlfile xml-file] [-d|--device <device>] [-l|--logfile <logfile>] [-p|--port port] [-s|--syslog] [-n|--nodaemon] [-i|--vsim] [-g|--debug] [-4|--inet4] [-6|--inet6]\n");
    exit(1);
}

short checkIP(char* ip)
{
    allowPtr aPtr;

    if ((aPtr = getAllowNode(cfgPtr->aPtr, inet_addr(ip))))
    {
        logIT(LOG_INFO, "%s in allowList (%s)", ip, aPtr->text);
        return (1);
    }
    else
    {
        logIT(LOG_INFO, "%s nicht in allowList", ip);
        return (0);
    }
}

int reloadConfig()
{
    pthread_mutex_lock(&config_mutex);

    if (parseXMLFile(xmlfile))
    {
        compileCommand(devPtr, uPtr);
        logIT(LOG_NOTICE, "XMLFile %s neu geladen", xmlfile);
        pthread_mutex_unlock(&config_mutex);
        return (1);
    }
    else
    {
        logIT(LOG_ERR, "Laden von XMLFile %s gescheitert", xmlfile);
        pthread_mutex_unlock(&config_mutex);
        return (0);
    }
}


int readCmdFile(char* filename, char* result, int* resultLen, char* device)
{
    FILE* cmdPtr;
    char line[MAXBUF];
    char string[256];
    char recvBuf[MAXBUF];
    char* resultPtr = result;
    int fd;
    int count = 0;
    //void *uPtr;
    //int maxResLen=*resultLen;
    *resultLen = 0; /* noch keine Zeichen empfangen :-) */

    /* das Device wird erst geoeffnet, wenn wir was zu tun haben */
    if ((fd = framer_openDevice(device, cfgPtr->devPtr->protoPtr->id)) == -1)
    {
        logIT(LOG_ERR, "Fehler beim oeffnen %s", device);
        result[0] = 0;
        *resultLen = 0;
        return (0);
    }

    cmdPtr = fopen(filename, "r");

    if (!cmdPtr)
    {
        logIT(LOG_ERR, "Kann Cmd File %s nicht oeffnen", filename);
        result[0] = 0;
        *resultLen = 0;
        framer_closeDevice(fd);
        return (0);
    }

    logIT(LOG_INFO, "Lese Cmd File %s", filename);
    /* Queue leeren */
    tcflush(fd, TCIOFLUSH);

    while (fgets(line, MAXBUF - 1, cmdPtr))
    {
        /* \n verdampfen */
        line[strlen(line) - 1] = '\0';
        bzero(recvBuf, sizeof(recvBuf));
        count = execCmd(line, fd, recvBuf, sizeof(recvBuf));
        int n;
        char* ptr;
        ptr = recvBuf;
        char buffer[MAXBUF];
        bzero(buffer, sizeof(buffer));

        for (n = 0; n < count; n++)   /* wir haben Zeichen empfangen */
        {
            *resultPtr++ = *ptr;
            (*resultLen)++;
            bzero(string, sizeof(string));
            unsigned char byte = *ptr++ & 255;
            snprintf(string, sizeof(string), "%02X ", byte);
            strcat(buffer, string);

            if (n >= MAXBUF - 3)
                break;
        }

        if (count - 1)
        {
            /* timeout */
        }

        if (count)
            logIT(LOG_INFO, "Empfangen: %s", buffer);

    }

    framer_closeDevice(fd);
    fclose(cmdPtr);
    return (1);
}
void printHelp(int socketfd)
{
    char string[] = " \
close: schliesst Verbindung zum Device\n \
commands: Listet alle in der XML definierten Kommandos fuer das Protokoll\n \
debug on|off: Debug Informationen an/aus\n \
detail <command>: Zeigt detaillierte Informationen zum <command> an\n \
device: In der XML gewaehltes Device\n \
protocol: Aktives Protokoll\n \
raw: Raw Modus, Befehle WAIT,SEND,RECV,PAUSE Abschluss mit END\n \
reload: Neu-Laden der XML Konfiguration\n \
unit on|off: Wandlung des Ergebnisses lsut definierter Unit an/aus\n \
version: Zeigt die Versionsnummer an\n \
quit: beendet die Verbindung\n";
    Writen(socketfd, string, strlen(string));
}

int rawModus(int socketfd, char* device)
{
    /* hier schreiben wir alle ankommenden Befehle in eine temp. Datei */
    /* diese Datei wird dann an readCmdFile uerbegeben */
    char readBuf[MAXBUF];
    char string[256];

#ifdef __CYGWIN__
    char tmpfile[] = "vitotmp-XXXXXX";
#else
    char tmpfile[] = "/tmp/vitotmp-XXXXXX";
#endif

    FILE* filePtr;
    char result[MAXBUF];
    int resultLen;

    if (!mkstemp(tmpfile))
    {
        /* noch ein Versuch */
        if (!mkstemp(tmpfile))
        {
            logIT(LOG_ERR, "Fehler Erzeugung mkstemp");
            return (0);
        }
    }

    filePtr = fopen(tmpfile, "w+");

    if (!filePtr)
    {
        logIT(LOG_ERR, "Kann Tmp File %s nicht anlegen", tmpfile);
        return (0);
    }

    logIT(LOG_INFO, "Raw Modus: Temp Datei: %s", tmpfile);

    while (Readline(socketfd, readBuf, sizeof(readBuf)))
    {
        /* hier werden die einzelnen Befehle geparst */
        if (strstr(readBuf, "END") == readBuf)
        {
            fclose(filePtr);
            resultLen = sizeof(result);
            readCmdFile(tmpfile, result, &resultLen, device);

            if (resultLen)   /* es wurden Zeichen empfangen */
            {
                char buffer[MAXBUF];
                bzero(buffer, sizeof(buffer));
                char2hex(buffer, result, resultLen);
                snprintf(string, sizeof(string), "Result: %s\n", buffer);
                Writen(socketfd, string, strlen(string));
            }

            remove(tmpfile);
            return (1);
        }

        logIT(LOG_INFO, "Raw: Gelesen: %s", readBuf);

        /*
        int n;
        if ((n=fputs(readBuf,filePtr))== 0) {
        */
        if (fputs(readBuf, filePtr) == EOF)
            logIT(LOG_ERR, "Fehler beim schreiben tmp Datei");
        else
        {
            /* debug stuff */
        }

    }

    return 0;			// is this correct?
}

int interactive(int socketfd, char* device)
{

    char readBuf[1000];
    char* readPtr;
    char prompt[] = "vctrld>";
    char bye[] = "good bye!\n";
    char unknown[] = "ERR: command unknown\n";
    char string[256];
    commandPtr cPtr;
    commandPtr pcPtr;
    static int deviceFd = -1;
    int count = 0;
    ssize_t rcount = 0;
    short noUnit = 0;
    char recvBuf[MAXBUF];
    char pRecvBuf[MAXBUF];
    char sendBuf[MAXBUF];
    char cmd[MAXBUF];
    char para[MAXBUF];
    char* ptr;
    size_t sendLen;
    char buffer[MAXBUF];

    Writen(socketfd, prompt, strlen(prompt));
    bzero(readBuf, sizeof(readBuf));


    while ((rcount = Readline(socketfd, readBuf, sizeof(readBuf))))
    {
        try
        {
            sendErrMsg(socketfd);
            /* Steuerzeichen verdampfen */
            /*readPtr=readBuf+strlen(readBuf); **/
            readPtr = readBuf + rcount;

            while (iscntrl(*readPtr))
                *readPtr-- = '\0';

            logIT(LOG_INFO, "Befehl: %s", readBuf);

            /* wir trennen Kommando und evtl. Optionen am ersten Blank */
            bzero(cmd, sizeof(cmd));
            bzero(para, sizeof(para));

            if ((ptr = strchr(readBuf, ' ')))
            {
                strncpy(cmd, readBuf, (size_t)(ptr - readBuf));
                strcpy(para, ptr + 1);
            }
            else
                strcpy(cmd, readBuf);

            /* hier werden die einzelnen Befehle geparst */
            if (strstr(readBuf, "help") == readBuf)
                printHelp(socketfd);
            else if (strstr(readBuf, "quit") == readBuf)
            {
                Writen(socketfd, bye, strlen(bye));
                //framer_closeDevice(deviceFd);
                return 1;
            }
            else if (strstr(readBuf, "debug on") == readBuf)
                setDebugFD(socketfd);
            else if (strstr(readBuf, "debug off") == readBuf)
                setDebugFD(-1);
            else if (strstr(readBuf, "unit off") == readBuf)
                noUnit = 1;
            else if (strstr(readBuf, "unit on") == readBuf)
                noUnit = 0;
            else if (strstr(readBuf, "reload") == readBuf)
            {
                if (reloadConfig())
                {
                    bzero(string, sizeof(string));
                    snprintf(string, sizeof(string), "XMLFile %s neu geladen\n", xmlfile);
                    Writen(socketfd, string, strlen(string));
                }
                else
                {
                    bzero(string, sizeof(string));
                    snprintf(string, sizeof(string), "Laden von XMLFile %s gescheitert, nutze alte Konfig\n", xmlfile);
                    Writen(socketfd, string, strlen(string));
                }

            }
            else if (strstr(readBuf, "raw") == readBuf)
                rawModus(socketfd, device);

            //else if (strstr(readBuf, "close") == readBuf) {
            //    framer_closeDevice(deviceFd);
            //    snprintf(string, sizeof(string), "%s geschlossen\n", device);
            //    Writen(socketfd, string, strlen(string));
            //    deviceFd = -1;
            //}

            else if (strstr(readBuf, "commands") == readBuf)
            {
                pthread_mutex_lock(&config_mutex);
                cPtr = cfgPtr->devPtr->cmdPtr;

                while (cPtr)
                {
                    if (cPtr->addr)
                    {
                        bzero(string, sizeof(string));
                        snprintf(string, sizeof(string), "%s: %s\n", cPtr->name, cPtr->description);
                        Writen(socketfd, string, strlen(string));
                    }

                    cPtr = cPtr->next;
                }

                pthread_mutex_unlock(&config_mutex);
            }
            else if (strstr(readBuf, "protocol") == readBuf)
            {
                bzero(string, sizeof(string));
                snprintf(string, sizeof(string), "%s\n", cfgPtr->devPtr->protoPtr->name);
                Writen(socketfd, string, strlen(string));
            }
            else if (strstr(readBuf, "device") == readBuf)
            {
                bzero(string, sizeof(string));
                snprintf(string, sizeof(string), "%s (ID=%s) (Protocol=%s)\n", cfgPtr->devPtr->name,
                         cfgPtr->devPtr->id,
                         cfgPtr->devPtr->protoPtr->name);
                Writen(socketfd, string, strlen(string));
            }
            else if (strstr(readBuf, "version") == readBuf)
            {
                bzero(string, sizeof(string));
                snprintf(string, sizeof(string), "Version: %s\n", VERSION_DAEMON);
                Writen(socketfd, string, strlen(string));
            }
            /* Ist das Kommando in der XML definiert ? */
            else if ((cPtr = getCommandNode(cfgPtr->devPtr->cmdPtr, cmd)) && (cPtr->addr))
            {
                bzero(string, sizeof(string));
                bzero(recvBuf, sizeof(recvBuf));
                bzero(sendBuf, sizeof(sendBuf));
                bzero(pRecvBuf, sizeof(pRecvBuf));


                /* Falls Unit Off wandeln wir die uebergebenen Parameter in Hex */
                /* oder keine Unit definiert ist */
                bzero(sendBuf, sizeof(sendBuf));

                if ((noUnit | !cPtr->unit) && *para)
                {
                    if ((sendLen = string2chr(para, sendBuf, sizeof(sendBuf))) == -1)
                    {
                        logIT(LOG_ERR, "Kein Hex string: %s", para);
                        sendErrMsg(socketfd);

                        if (!Writen(socketfd, prompt, strlen(prompt)))
                        {
                            sendErrMsg(socketfd);
                            //framer_closeDevice(deviceFd);
                            return (0);
                        }

                        continue;
                    }

                    /* falls sendLen > als len der Befehls, nehmen wir len */
                    if (sendLen > cPtr->blockLength)
                    {
                        logIT(LOG_WARNING, "Laenge des Hex Strings > Sendelaenge des Befehls, sende nur %d Byte", cPtr->blockLength);
                        sendLen = cPtr->blockLength;
                    }
                }
                else if (*para)   /* wir kopieren die Parameter, darum kuemert sich execbyteCode selbst */
                {
                    strcpy(sendBuf, para);
                    sendLen = strlen(sendBuf);
                }

                if (iniFD)
                    fprintf(iniFD, ";%s\n", readBuf);


                /* das Device wird erst geoeffnet, wenn wir was zu tun haben */
                /* aber nur, falls es nicht schon offen ist */

                if (deviceFd < 0)
                {
                    /* As one vclient call opens the link once, all is seen a transaction
                     * This may cause trouble for telnet sessions, as the whole session is
                     * one link activity, even more commands are given within.
                     * This is related to a accept/close on a server socket
                     */

                    if ((deviceFd = framer_openDevice(device, cfgPtr->devPtr->protoPtr->id)) == -1)
                    {
                        logIT(LOG_ERR, "Fehler beim oeffnen %s", device);
                        sendErrMsg(socketfd);

                        if (!Writen(socketfd, prompt, strlen(prompt)))
                        {
                            sendErrMsg(socketfd);
                            framer_closeDevice(deviceFd);
                            return (0);
                        }

                        continue;
                    }
                }

                pthread_mutex_lock(&config_mutex);
                pthread_mutex_lock(&device_mutex);

                /* falls ein Pre-Kommando definiert wurde, fuehren wir dies zuerst aus */
                if (cPtr->precmd && (pcPtr = getCommandNode(cfgPtr->devPtr->cmdPtr, cPtr->precmd)))
                {
                    logIT(LOG_INFO, "Fuehre Pre Kommando %s aus", cPtr->precmd);

                    if (execByteCode(pcPtr, deviceFd, pRecvBuf, sizeof(pRecvBuf), sendBuf, sendLen, 1, pcPtr->bit, pcPtr->retry, pRecvBuf, pcPtr->recvTimeout) == -1)
                    {
                        logIT(LOG_ERR, "Fehler beim ausfuehren von %s", readBuf);
                        sendErrMsg(socketfd);
                        pthread_mutex_unlock(&device_mutex);
                        pthread_mutex_unlock(&config_mutex);
                        break;
                    }
                    else
                    {
                        bzero(buffer, sizeof(buffer));
                        char2hex(buffer, pRecvBuf, pcPtr->blockLength);
                        logIT(LOG_INFO, "Ergebnis Pre-Kommand: %s", buffer);
                    }


                }

                /* wir fuehren den Bytecode aus,
                	   -1 -> Fehler
                	0 -> Formaterierter String
                	n -> Bytes in Rohform */

                count = execByteCode(cPtr, deviceFd, recvBuf, sizeof(recvBuf), sendBuf, sendLen, noUnit, cPtr->bit, cPtr->retry, pRecvBuf, cPtr->recvTimeout);

                if (count == -1)
                {
                    logIT(LOG_ERR, "Fehler beim ausfuehren von %s", readBuf);
                    sendErrMsg(socketfd);
                }
                else if (*recvBuf && (count == 0))   /* Unit gewandelt */
                {

                    logIT(LOG_INFO, "%s", recvBuf);
                    snprintf(string, sizeof(string), "%s\n", recvBuf);
                    Writen(socketfd, string, strlen(string));
                }
                else
                {
                    int n;
                    char* ptr;
                    ptr = recvBuf;
                    char buffer[MAXBUF];
                    bzero(buffer, sizeof(buffer));

                    for (n = 0; n < count; n++)   /* wir haben Zeichen empfangen */
                    {
                        bzero(string, sizeof(string));
                        unsigned char byte = *ptr++ & 255;
                        snprintf(string, sizeof(string), "%02X ", byte);
                        strcat(buffer, string);

                        if (n >= MAXBUF - 3)
                            break;
                    }

                    if (count)
                    {
                        snprintf(string, sizeof(string), "%s\n", buffer);
                        Writen(socketfd, string, strlen(string));
                        logIT(LOG_INFO, "Empfangen: %s", buffer);
                    }
                }

                if (iniFD)
                    fflush(iniFD);

                pthread_mutex_unlock(&device_mutex);
                pthread_mutex_unlock(&config_mutex);
            }
            else if (strstr(readBuf, "detail") == readBuf)
            {
                readPtr = readBuf + strlen("detail");

                while (isspace(*readPtr))
                    readPtr++;

                pthread_mutex_lock(&config_mutex);

                /* Ist das Kommando in der XML definiert ? */
                if (readPtr && (cPtr = getCommandNode(cfgPtr->devPtr->cmdPtr, readPtr)))
                {
                    bzero(string, sizeof(string));
                    snprintf(string, sizeof(string), "%s: %s\n", cPtr->name, cPtr->send);
                    Writen(socketfd, string, strlen(string));
                    /* Error String definiert */
                    char buf[MAXBUF];
                    bzero(buf, sizeof(buf));

                    if (cPtr->errStr && char2hex(buf, cPtr->errStr, cPtr->blockLength))
                    {
                        snprintf(string, sizeof(string), "\tError bei (Hex): %s", buf);
                        Writen(socketfd, string, strlen(string));
                    }

                    /* recvTimeout ?*/
                    if (cPtr->recvTimeout)
                    {
                        snprintf(string, sizeof(string), "\tRECV Timeout: %d ms\n", cPtr->recvTimeout);
                        Writen(socketfd, string, strlen(string));
                    }

                    /* Retry definiert ? */
                    if (cPtr->retry)
                    {
                        snprintf(string, sizeof(string), "\tRetry: %d\n", cPtr->retry);
                        Writen(socketfd, string, strlen(string));
                    }

                    /* Ist Bit definiert ?*/
                    if (cPtr->bit > 0)
                    {
                        snprintf(string, sizeof(string), "\tBit (BP): %d\n", cPtr->bit);
                        Writen(socketfd, string, strlen(string));
                    }

                    /* Pre-Command definiert ?*/
                    if (cPtr->precmd)
                    {
                        snprintf(string, sizeof(string), "\tPre-Kommando (P0-P9): %s\n", cPtr->precmd);
                        Writen(socketfd, string, strlen(string));
                    }

                    /* Falls eine Unit verwendet wurde, geben wir das auch noch aus */
                    compilePtr cmpPtr;
                    cmpPtr = cPtr->cmpPtr;

                    while (cmpPtr)
                    {
                        if (cmpPtr && cmpPtr->uPtr)   /* Unit gefunden */
                        {
                            char* gcalc;
                            char* scalc;

                            /* wir unterscheiden die Rechnerei nach get und setaddr */
                            if (cmpPtr->uPtr->gCalc && *cmpPtr->uPtr->gCalc)
                                gcalc = cmpPtr->uPtr->gCalc;
                            else
                                gcalc = cmpPtr->uPtr->gICalc;

                            if (cmpPtr->uPtr->sCalc && *cmpPtr->uPtr->sCalc)
                                scalc = cmpPtr->uPtr->sCalc;
                            else
                                scalc = cmpPtr->uPtr->sICalc;

                            snprintf(string, sizeof(string), "\tUnit: %s (%s)\n\t  Type: %s\n\t  Get-Calc: %s\n\t  Set-Calc: %s\n\t Einheit: %s\n",
                                     cmpPtr->uPtr->name, cmpPtr->uPtr->abbrev,
                                     cmpPtr->uPtr->type,
                                     gcalc,
                                     scalc,
                                     cmpPtr->uPtr->entity);
                            Writen(socketfd, string, strlen(string));

                            /* falls es sich um ein enum handelt, gibts noch mehr */
                            /* oder sonstwo enums definiert sind */
                            if (cmpPtr->uPtr->ePtr)
                            {
                                enumPtr ePtr;
                                ePtr = cmpPtr->uPtr->ePtr;
                                char dummy[20];

                                while (ePtr)
                                {
                                    bzero(dummy, sizeof(dummy));

                                    if (!ePtr->bytes)
                                        strcpy(dummy, "<default>");
                                    else
                                        char2hex(dummy, ePtr->bytes, ePtr->len);

                                    snprintf(string, sizeof(string), "\t  Enum Bytes:%s Text:%s\n", dummy, ePtr->text);
                                    Writen(socketfd, string, strlen(string));
                                    ePtr = ePtr->next;
                                }
                            }
                        }

                        cmpPtr = cmpPtr->next;
                    }
                }
                else
                {
                    bzero(string, sizeof(string));
                    snprintf(string, sizeof(string), "ERR: command %s unbekannt\n", readPtr);
                    Writen(socketfd, string, strlen(string));
                }

                pthread_mutex_unlock(&config_mutex);
            }
            else if (*readBuf)
            {
                if (!Writen(socketfd, unknown, strlen(unknown)))
                {
                    sendErrMsg(socketfd);
                    //framer_closeDevice(deviceFd);
                    return (0);
                }

            }
        }
        catch (std::exception& e)
        {
            logIT(LOG_ERR, "%s", e.what());
        }

        bzero(string, sizeof(string));
        sendErrMsg(socketfd);

        if (!Writen(socketfd, prompt, strlen(prompt)))
        {
            sendErrMsg(socketfd);
            return 0;
        }

        bzero(readBuf, sizeof(readBuf));
    }

    sendErrMsg(socketfd);
    //framer_closeDevice(deviceFd);
    return 0;
}

static void sigHupHandler(int signo)
{
    logIT(LOG_NOTICE, "SIGHUP empfangen");
    reloadConfig();
}

static void sigTermHandler(int signo)
{
    logIT(LOG_NOTICE, "SIGTERM empfangen");
    exit(1);
}

void* connection_handler(void* voidArgs)
{
    thread_args* args = (thread_args*)voidArgs;
    logIT(LOG_DEBUG, "ch: device: %s", args->device);
    interactive(args->sock_fd, args->device);
    closeSocket(args->sock_fd);
    free(args);
    return 0;
}


/* hier gehts los */

int main(int argc, char* argv[])
{

    /* Auswertung der Kommandozeilenschalter */
    char device[20] = "";
    char* cmdfile = NULL;
    char* logfile = NULL;
    static int useSyslog = 0;
    static int debug = 0;
    static int verbose = 0;
    int tcpport = 0;
    static int simuOut = 0;
    int opt;

    while (1)
    {

        static struct option long_options[] =
        {
            {"commandfile",	required_argument,	0, 'c'},
            {"device",		required_argument,	0, 'd'},
            {"debug",		no_argument,		&debug, 1},
            {"vsim",		no_argument,		&simuOut, 1},
            {"logfile",		required_argument,	0, 'l'},
            {"nodaemon",	no_argument,		&makeDaemon, 0},
            {"port",		required_argument,	0, 'p'},
            {"syslog",		no_argument,		&useSyslog, 1},
            {"xmlfile",		required_argument,	0, 'x'},
            {"verbose",		no_argument,		&verbose, 1},
            {"inet4",		no_argument,		&inetversion, 4},
            {"inet6",		no_argument,		&inetversion, 6},
            {"help",		no_argument,		0,	0},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        opt = getopt_long(argc, argv, "c:d:gil:np:svx:46",
                          long_options, &option_index);

        /* Detect the end of the options. */
        if (opt == -1)
            break;

        switch (opt)
        {
            case 0:

                /* If this option sets a flag, we do nothing for now */
                if (long_options[option_index].flag != 0)
                    break;

                if (verbose)
                {
                    printf("option %s", long_options[option_index].name);

                    if (optarg)
                        printf(" with arg %s", optarg);

                    printf("\n");
                }

                if (strcmp("help", long_options[option_index].name) == 0)
                    usage();

                break;

            case '4':
                inetversion = 4;
                break;

            case '6':
                inetversion = 6;
                break;

            case 'c':
                cmdfile = optarg;
                break;

            case 'd':
                strncpy(device, optarg, 20);
                break;

            case 'g':
                debug = 1;
                break;

            case 'i':
                simuOut = 1;
                break;

            case 'l':
                logfile = optarg;
                break;

            case 'n':
                makeDaemon = 0;
                break;

            case 'p':
                tcpport = atoi(optarg);
                break;

            case 's':
                useSyslog = 1;
                break;

            case 'v':
                puts("option -v\n");
                verbose = 1;
                break;

            case 'x':
                xmlfile = optarg;
                break;

            case '?':
                /* getopt_long already printed an error message. */
                usage();
                break;

            default:
                abort();
        }
    }

    initLog(useSyslog, logfile, debug);

    if (!parseXMLFile(xmlfile))
    {
        fprintf(stderr, "Fehler beim Laden von %s, terminiere!\n", xmlfile);
        exit(1);
    }


    /* es wurden die beiden globalen Variablen cfgPtr und protoPtr gefuellt */
    if (cfgPtr)
    {
        if (!tcpport)
            tcpport = cfgPtr->port;

        if (!device[0])
            strncpy(device, cfgPtr->tty, 20);

        if (!logfile)
            logfile = cfgPtr->logfile;

        if (!useSyslog)
            useSyslog = cfgPtr->syslog;

        if (!debug)
            debug = cfgPtr->debug;
    }

    if (!initLog(useSyslog, logfile, debug))
        exit(1);

    pthread_mutex_init(&device_mutex, NULL);
    pthread_mutex_init(&config_mutex, NULL);

    if (signal(SIGHUP, sigHupHandler) == SIG_ERR)
    {
        logIT(LOG_ERR, "Fehler beim Signalhandling SIGHUP");
        exit(1);
    }

    if (signal(SIGQUIT, sigTermHandler) == SIG_ERR)
    {
        logIT(LOG_ERR, "Fehler beim Signalhandling SIGTERM: %s", strerror(errno));
        exit(1);
    }

    /* falls -i angegeben wurde, loggen wir die Befehle im Simulator INI Format */
    if (simuOut)
    {
        char file[100];
        snprintf(file, sizeof(file), INIOUTFILE, cfgPtr->devID);

        if (!(iniFD = fopen(file, "w")))
            logIT(LOG_ERR, "Konnte Simulator INI File %s nicht anlegen", file);

        fprintf(iniFD, "[DATA]\n");
    }

    /* die Macros werden ersetzt und die zu sendenden Strings in Bytecode gewandelt */
    compileCommand(devPtr, uPtr);

    int fd = 0;
    char result[MAXBUF];
    int resultLen = sizeof(result);
    int sid;
    int pidFD = 0;
    char str[10];
    const char* pidFile = "/var/run/vcontrold.pid";

    if (tcpport)
    {
        if (makeDaemon)
        {
            int pid;

            /* etwas Siganl Handling */
            if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
                logIT(LOG_ERR, "Fehler beim Signalhandling SIGCHLD");

            pid = fork();

            if (pid < 0)
            {
                logIT(LOG_ERR, "fork fehlgeschlagen (%d)", pid);
                exit(1);
            }

            if (pid > 0)
                exit(0); /* Vater wird beendet, Kind laueft weiter */

            /* ab hier laueft nur noch das Kind */

            /* FD schliessen */
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            umask(0);

            sid = setsid();

            if (sid < 0)
            {
                logIT(LOG_ERR, "setsid fehlgeschlagen");
                exit(1);
            }

            if (chdir("/") < 0)
            {
                logIT(LOG_ERR, "chdir / fehlgeschlagen");
                exit(1);
            }

            pidFD = open(pidFile, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

            if (pidFD == -1)
            {
                logIT(LOG_ERR, "Could not open PID lock file %s, exiting", pidFile);
                exit(1);
            }

            if (lockf(pidFD, F_TLOCK, 0) == -1)
            {
                logIT(LOG_ERR, "Could not lock PID lock file %s, exiting", pidFile);
                exit(1);
            }

            if (ftruncate(pidFD, 0) == -1)
            {
                logIT(LOG_ERR, "Could not truncate PID file '%s'", pidFile);
                exit(1);
            }

            sprintf(str, "%d\n", getpid());

            if (write(pidFD, str, strlen(str)) != (ssize_t)strlen(str))
            {
                logIT(LOG_ERR, "Writing to PID file '%s'", pidFile);
                exit(1);
            }
        }

        int sockfd = -1;
        int listenfd = -1;
        /* Zeiger auf die Funktion checkIP */

        short(*checkP)(char*);

        if (cfgPtr->aPtr)  /* wir haben eine allow Liste */
            checkP = checkIP;
        else
            checkP = NULL;

        listenfd = openSocket(tcpport);

        while (1)
        {
            sockfd = listenToSocket(listenfd, checkP);

            if (sockfd >= 0)
            {
                /* Socket hat fd zurueckgegeben, den Rest machen wir in interactive */
                pthread_t thread_id;

                thread_args* args = (thread_args*)malloc(sizeof(thread_args));
                logIT(LOG_DEBUG, "main: device: %s", device);
                strncpy(args->device, device, 20);
                args->sock_fd = sockfd;

                if (pthread_create(&thread_id, NULL, connection_handler, (void*)args) < 0)
                {
                    logIT(LOG_ERR, "Could not create thread.");
                    exit(1);
                }
            }
            else
                logIT(LOG_ERR, "Fehler bei Verbindungsaufbau");
        }
    }

    if (*cmdfile)
        readCmdFile(cmdfile, result, &resultLen, device);

    close(fd);
    close(pidFD);
    logIT(LOG_LOCAL0, "vcontrold beendet");

    return 0;
}