/*
 * Testprogramm fuer Vito- Abfrage
 *  */
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>

#include"common.h"

int syslogger = 0;
int debug = 0;
FILE* logFD;
char errMsg[2000];
int errClass = 99;
int dbgFD = -1;

int initLog(int useSyslog, char* logfile, int debugSwitch)
{
    /* oeffnet bei Bedarf syslog oder log-Datei */
    if (useSyslog)
    {
        syslogger = 1;
        openlog("vito", LOG_PID, LOG_LOCAL0);
        syslog(LOG_LOCAL0, "vito gestartet");
    }

    if (logfile)   /* Logfile ist nicht NULL und nicht leer */
    {
        if (strcmp(logfile, "") == 0)
            return (0);

        logFD = fopen(logfile, "a");

        if (!logFD)
        {
            printf("Konnte %s nicht oeffnen %s", logfile, strerror(errno));
            return (0);
        }
    }

    debug = debugSwitch;
    bzero(errMsg, sizeof(errMsg));
    return (1);

}

void logIT(int logclass, const char* string, ...)
{

    va_list arguments;
    time_t t;
    char* tPtr;
    char* cPtr;
    time(&t);
    tPtr = ctime(&t);
    char* print_buffer;
    int pid;
    size_t avail;

    va_start(arguments, string);
    vasprintf(&print_buffer, string, arguments);
    va_end(arguments);

    if (logclass <= LOG_ERR)
    {
        avail = sizeof(errMsg) - strlen(errMsg) - 2;

        if (avail > 0)
        {
            strncat(errMsg, print_buffer, avail);
            strcat(errMsg, "\n");
        }
        else
        {
            strcpy(&errMsg[sizeof(errMsg) - 12], "OVERFLOW\n"); /* sollte den semop Fehler loesen */
        }
    }

    errClass = logclass;
    /* Steuerzeichen verdampfen */
    cPtr = tPtr;

    while (*cPtr)
    {
        if (iscntrl(*cPtr))
            *cPtr = ' ';

        cPtr++;
    }

    if (dbgFD >= 0)
    {
        /* der Debug FD ist gesetzt und wir senden die Infos zuerst dort hin */
        dprintf(dbgFD, "DEBUG:%s: %s\n", tPtr, print_buffer);
    }

    if (!debug && (logclass > LOG_NOTICE))
    {
        free(print_buffer);
        return;
    }

    pid = getpid();

    if (syslogger)
        syslog(logclass, "%s", print_buffer);

    if (logFD)
    {
        fprintf(logFD, "[%d] %s: %s\n", pid, tPtr, print_buffer);
        fflush(logFD);
    }

    /* Ausgabe nur, wenn 2 als STDERR geoeffnet ist */
    if (isatty(2))
        fprintf(stderr, "[%d] %s: %s\n", pid, tPtr, print_buffer);

    free(print_buffer);
}

void sendErrMsg(int fd)
{
    char string[256];

    if ((fd >= 0) && (errClass <= 3))
    {
        snprintf(string, sizeof(string), "ERR: %s", errMsg);
        write(fd, string, strlen(string));
        errClass = 99; /* damit wird sie nur ein mal angezeigt */
    }

    *errMsg = '\0';
}

void setDebugFD(int fd)
{
    dbgFD = fd;
}


char hex2chr(char* hex)
{
    char buffer[16];
    int hex_value = -1;

    snprintf(buffer, sizeof(buffer), "0x%s", hex);

    if (sscanf(hex, "%x", &hex_value) != 1)
        logIT(LOG_WARNING, "Ungültige Hex Zeichen in %s", hex);

    return (char)hex_value;
}

size_t char2hex(char* outString, const char* charPtr, size_t len)
{
    size_t n;
    char string[MAXBUF];
    bzero(string, sizeof(string));

    for (n = 0; n < len; n++)
    {
        unsigned char byte = *charPtr++ & 255;
        snprintf(string, sizeof(string), "%02X ", byte);
        strcat(outString, string);
    }

    /* letztes Leerzeichen verdampfen */
    outString[strlen(outString) - 1] = '\0';
    return len;
}

size_t string2chr(char* line, char* buf, size_t bufsize)
{
    char* sptr;
    size_t count;

    count = 0;

    sptr = strtok(line, " ");

    do
    {
        if (*sptr == ' ')
            continue;

        buf[count++] = hex2chr(sptr);
    }
    while ((sptr = strtok(NULL, " ")) && (count < bufsize));

    return count;
}
