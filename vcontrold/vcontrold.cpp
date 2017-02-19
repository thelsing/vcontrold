/*
 * Vito-Control Daemon fuer Vito- Abfrage
 *  */
/* $Id: vcontrold.c 34 2008-04-06 19:39:29Z marcust $ */
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string>
#include <memory>
#include <stdexcept>
#include <cstring>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

#include <yaml-cpp/yaml.h>

#include "io.h"
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
#else
    const char* xmlfile = "/etc/vcontrold/vcontrold.xml";
#endif

int makeDaemon = 1;
int inetversion = 0;
pthread_mutex_t device_mutex;
pthread_mutex_t config_mutex;
std::string device;
Vcontrold::Framer* framerPtr;

/* in xmlconfig.c definiert */
extern configPtr cfgPtr;
extern commandPtr cmdPtr;

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

bool reloadConfig()
{
    pthread_mutex_lock(&config_mutex);

    if (parseXMLFile(xmlfile))
    {
        expand(cmdPtr, cfgPtr->protoPtr);
        buildByteCode(cmdPtr);
        logIT(LOG_NOTICE, "XMLFile %s neu geladen", xmlfile);
        pthread_mutex_unlock(&config_mutex);
        return true;
    }
    else
    {
        logIT(LOG_ERR, "Laden von XMLFile %s gescheitert", xmlfile);
        pthread_mutex_unlock(&config_mutex);
        return false;
    }
}

void printHelp(int socketfd)
{
    char string[] = " \
commands: Listet alle in der XML definierten Kommandos fuer das Protokoll\n \
debug on|off: Debug Informationen an/aus\n \
detail <command>: Zeigt detaillierte Informationen zum <command> an\n \
protocol: Aktives Protokoll\n \
reload: Neu-Laden der XML Konfiguration\n \
unit on|off: Wandlung des Ergebnisses lsut definierter Unit an/aus\n \
version: Zeigt die Versionsnummer an\n \
quit: beendet die Verbindung\n";
    WriteString(socketfd, string);
}

void printCommands(int socketfd)
{
    char string[256] = "";
    pthread_mutex_lock(&config_mutex);
    commandPtr cPtr = cmdPtr;

    while (cPtr)
    {
        if (cPtr->addrStr)
        {
            snprintf(string, sizeof(string), "%s: %s\n", cPtr->name, cPtr->description);
            WriteString(socketfd, string);
        }

        cPtr = cPtr->next;
    }

    pthread_mutex_unlock(&config_mutex);
}

void printCommandDetails(char* cmdName, int socketfd)
{
    char string[256];

    while (isspace(*cmdName))
        cmdName++;

    try
    {
        pthread_mutex_lock(&config_mutex);
        commandPtr cPtr;

        /* Ist das Kommando in der XML definiert ? */
        if (cmdName && (cPtr = getCommandNode(cmdPtr, cmdName)))
        {
            bzero(string, sizeof(string));
            snprintf(string, sizeof(string), "%s: %s\n", cPtr->name, cPtr->send);
            WriteString(socketfd, string);
            /* Error String definiert */
            char buf[MAXBUF];
            bzero(buf, sizeof(buf));

            if (cPtr->errStr && char2hex(buf, cPtr->errStr, cPtr->blockLength))
            {
                snprintf(string, sizeof(string), "\tError bei (Hex): %s", buf);
                WriteString(socketfd, string);
            }

            /* recvTimeout ?*/
            if (cPtr->recvTimeout)
            {
                snprintf(string, sizeof(string), "\tRECV Timeout: %d ms\n", cPtr->recvTimeout);
                WriteString(socketfd, string);
            }

            /* Retry definiert ? */
            if (cPtr->retry)
            {
                snprintf(string, sizeof(string), "\tRetry: %d\n", cPtr->retry);
                WriteString(socketfd, string);
            }
        }
        else
        {
            bzero(string, sizeof(string));
            snprintf(string, sizeof(string), "ERR: command %s unbekannt\n", cmdName);
            WriteString(socketfd, string);
        }
    }
    catch (std::exception& e)
    {
        logIT(LOG_ERR, "%s", e.what());
    }

    pthread_mutex_unlock(&config_mutex);
}

commandPtr findCommand(const char* cmd)
{
    commandPtr cPtr = getCommandNode(cmdPtr, cmd);

    if (cPtr && cPtr->addrStr)
        return cPtr;

    return 0;
}

std::string runCommand(commandPtr cPtr, const char* para, short noUnit)
{
    struct tms tms_t;
    clock_t start, end;
    double clktck = (double)sysconf(_SC_CLK_TCK);
    start = times(&tms_t);

    std::string result("");
    size_t sendLen = 0;

    char recvBuf[MAXBUF];
    char pRecvBuf[MAXBUF];
    char sendBuf[MAXBUF];
    int count = 0;

    char string[256] = "";

    bzero(string, sizeof(string));
    bzero(recvBuf, sizeof(recvBuf));
    bzero(sendBuf, sizeof(sendBuf));
    bzero(pRecvBuf, sizeof(pRecvBuf));


    /* Falls Unit Off wandeln wir die uebergebenen Parameter in Hex */
    /* oder keine Unit definiert ist */
    bzero(sendBuf, sizeof(sendBuf));

    if (noUnit && para && *para)
    {
        if ((sendLen = string2chr((char*)para, sendBuf, sizeof(sendBuf))) == 0)
        {
            logIT(LOG_ERR, "Kein Hex string: %s", para);

            return result;
        }

        /* falls sendLen > als len der Befehls, nehmen wir len */
        if (sendLen > cPtr->blockLength)
        {
            logIT(LOG_WARNING, "Laenge des Hex Strings > Sendelaenge des Befehls, sende nur %d Byte", cPtr->blockLength);
            sendLen = cPtr->blockLength;
        }
    }
    else if (para && *para)   /* wir kopieren die Parameter, darum kuemert sich execbyteCode selbst */
    {
        strcpy(sendBuf, para);
        sendLen = strlen(sendBuf);
    }

    /* das Device wird erst geoeffnet, wenn wir was zu tun haben */
    /* aber nur, falls es nicht schon offen ist */

    if (!framerPtr->IsOpen())
    {

        if (framerPtr->OpenDevice(cfgPtr->protoPtr->id) == -1)
        {
            logIT(LOG_ERR, "Fehler beim oeffnen %s", device.c_str());
            throw std::logic_error("could not open serial device");
        }
    }

    pthread_mutex_lock(&config_mutex);
    pthread_mutex_lock(&device_mutex);

    try
    {
        /* wir fuehren den Bytecode aus,
        	   -1 -> Fehler
        	0 -> Formaterierter String
        	n -> Bytes in Rohform */
        count = execByteCode(cPtr, *framerPtr, recvBuf, sizeof(recvBuf), sendBuf, sendLen, noUnit);

    }
    catch (std::exception& e)
    {
        logIT(LOG_ERR, "Exception: %s", e.what());
        logIT(LOG_ERR, "Resetting device and retrying");

        try
        {
            framerPtr->ResetDevice();
            count = execByteCode(cPtr, *framerPtr, recvBuf, sizeof(recvBuf), sendBuf, sendLen, noUnit);
        }
        catch (std::exception& e)
        {
            logIT(LOG_ERR, "Exception on retry: %s", e.what());
        }
    }

    if (count == -1)
        logIT(LOG_ERR, "Fehler beim ausfuehren von %s", cPtr->name);
    else if (*recvBuf && (count == 0))   /* Unit gewandelt */
    {
        logIT(LOG_INFO, "%s", recvBuf);
        result = std::string(recvBuf);
    }
    else
    {
        char buffer[MAXBUF];

        count = char2hex(buffer, recvBuf, count);

        if (count)
        {
            result = std::string(buffer);
            logIT(LOG_INFO, "Empfangen: %s", buffer);
        }
    }


    pthread_mutex_unlock(&device_mutex);
    pthread_mutex_unlock(&config_mutex);
    end = times(&tms_t);
    logIT(LOG_INFO, "runcommand took (%0.1f ms)", ((double)(end - start) / clktck) * 1000);
    return result;
}

std::string bulkExec(char* para, short noUnit)
{
    YAML::Node commands = YAML::Load(para);

    for (YAML::const_iterator it = commands.begin(); it != commands.end(); ++it)
    {
        std::string cmd = it->first.as<std::string>();
        std::string para = it->second.as<std::string>();

        commandPtr cptr = findCommand(cmd.c_str());

        if (!cptr)
            continue;

        commands[cmd] = runCommand(cptr, para.c_str(), noUnit);
    }

    YAML::Emitter out;
    out << YAML::Flow << YAML::DoubleQuoted << commands;
    std::string result(out.c_str());
    return result;
}

void interactive(int socketfd)
{
    short noUnit = 0;

    std::string line;

    while (ReadLine(socketfd, &line))
    {
        std::stringstream ss;

        try
        {
            logIT(LOG_INFO, "Befehl: %s", line.c_str());

            /* wir trennen Kommando und evtl. Optionen am ersten Blank */
            char* cmd = (char*)line.c_str();
            char* para = strchr(cmd, ' ');

            if (para)
            {
                *para = 0;
                para++;
            }

            /* hier werden die einzelnen Befehle geparst */
            if (strcmp(cmd, "help") == 0)
                printHelp(socketfd);
            else if (strcmp(cmd, "quit") == 0)
            {
                WriteString(socketfd, "good bye!\n");
                return;
            }
            else if (strcmp(cmd, "debug") == 0)
            {
                if (strcmp(para, "on") == 0)
                    setDebugFD(socketfd);
                else
                    setDebugFD(-1);
            }
            else if (strcmp(cmd, "unit") == 0)
            {
                if (strcmp(para, "off") == 0)
                    noUnit = 1;
                else
                    noUnit = 0;
            }
            else if (strcmp(cmd, "reload") == 0)
            {
                if (reloadConfig())
                    ss << "XMLFile " << xmlfile << " neu geladen\n";
                else
                    ss << "Laden von XMLFile " << xmlfile << " gescheitert, nutze alte Konfig\n";

            }
            else if (strcmp(cmd, "commands") == 0)
                printCommands(socketfd);

            else if (strcmp(cmd, "protocol") == 0)
                ss << cfgPtr->protoPtr->name << std::endl;

            else if (strcmp(cmd, "version") == 0)
                ss << "Version: " << VERSION_DAEMON << std::endl;

            else if (strcmp(cmd, "bulkexec") == 0)
                ss << bulkExec(para, noUnit) << std::endl;

            else if (commandPtr cptr = findCommand(cmd))
                ss << runCommand(cptr, para, noUnit) << std::endl;

            else if (line == "detail")
                printCommandDetails(para, socketfd);

            else if (!line.empty())
                ss << "ERR: command unknown\n";
        }
        catch (std::exception& e)
        {
            logIT(LOG_ERR, "%s", e.what());
        }

        std::string result = ss.str();

        if (!result.empty())
            WriteString(socketfd, result);

        sendErrMsg(socketfd);
    }
}

void* connection_handler(void* voidArgs)
{
    std::shared_ptr<int> fdPtr((int*)voidArgs);

    try
    {
        interactive(*fdPtr);
    }
    catch (std::exception& e)
    {
        logIT(LOG_ERR, "%s", e.what());
    }

    closeSocket(*fdPtr);
    return 0;
}

int main(int argc, char* argv[])
{

    /* Auswertung der Kommandozeilenschalter */
    std::string logfile;
    int useSyslog = 0;
    int debug = 0;
    int tcpport = 0;
    int opt;

    while (1)
    {

        static struct option long_options[] =
        {
            {"debug",		no_argument,		&debug, 1},
            {"logfile",		required_argument,	0, 'l'},
            {"nodaemon",	no_argument,		&makeDaemon, 0},
            {"port",		required_argument,	0, 'p'},
            {"syslog",		no_argument,		&useSyslog, 1},
            {"xmlfile",		required_argument,	0, 'x'},
            {"inet4",		no_argument,		&inetversion, 4},
            {"inet6",		no_argument,		&inetversion, 6},
            {"help",		no_argument,		0,	0},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;
        opt = getopt_long(argc, argv, "gl:np:sx:46",
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

                if (strcmp("help", long_options[option_index].name) == 0)
                    usage();

                break;

            case '4':
                inetversion = 4;
                break;

            case '6':
                inetversion = 6;
                break;

            case 'g':
                debug = 1;
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

    initLog(useSyslog, logfile.c_str(), debug);

    if (!parseXMLFile(xmlfile))
    {
        fprintf(stderr, "Fehler beim Laden von %s, terminiere!\n", xmlfile);
        exit(1);
    }


    /* es wurden die beiden globalen Variablen cfgPtr und protoPtr gefuellt */
    if (!cfgPtr)
    {
        fprintf(stderr, "Fehler beim Laden von %s, terminiere!\n", xmlfile);
        exit(1);
    }

    if (!tcpport)
        tcpport = cfgPtr->port;

    device = cfgPtr->tty;

    if (logfile.empty())
        logfile = cfgPtr->logfile;

    if (!useSyslog)
        useSyslog = cfgPtr->syslog;

    if (!debug)
        debug = cfgPtr->debug;

    if (!initLog(useSyslog, logfile.c_str(), debug))
        exit(1);

    pthread_mutex_init(&device_mutex, NULL);
    pthread_mutex_init(&config_mutex, NULL);

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
    {
        logIT(LOG_ERR, "Fehler beim Signalhandling SIGPIPE: %s", strerror(errno));
        exit(1);
    }

    /* die Macros werden ersetzt und die zu sendenden Strings in Bytecode gewandelt */
    expand(cmdPtr, cfgPtr->protoPtr);
    buildByteCode(cmdPtr);

    int sid;
    int pidFD = 0;
    char str[10];
    const char* pidFile = "/tmp/vcontrold.pid";

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

        Vcontrold::Framer framer(cfgPtr->tty);
        framerPtr = &framer;

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

                int* fdPtr = new int(sockfd);
                logIT(LOG_DEBUG, "main: device: %s", cfgPtr->tty.c_str());

                if (pthread_create(&thread_id, NULL, connection_handler, (void*)fdPtr) < 0)
                {
                    logIT(LOG_ERR, "Could not create thread.");
                    exit(1);
                }
            }
            else
                logIT(LOG_ERR, "Fehler bei Verbindungsaufbau");
        }
    }

    close(pidFD);
    logIT(LOG_LOCAL0, "vcontrold beendet");

    return 0;
}