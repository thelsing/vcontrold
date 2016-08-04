#ifndef XMLCONFIG_H
#define XMLCONFIG_H

#include <arpa/inet.h>


typedef struct config* configPtr;
typedef struct protocol* protocolPtr;
typedef struct unit* unitPtr;
typedef struct macro* macroPtr;
typedef struct command* commandPtr;
typedef struct compile* compilePtr;
typedef struct device* devicePtr;
typedef struct icmd* icmdPtr;
typedef struct allow* allowPtr;
typedef struct enumerate* enumPtr;


int parseXMLFile(char* filename);
macroPtr getMacroNode(macroPtr ptr, const char* name);
unitPtr getUnitNode(unitPtr ptr, const char* name);
commandPtr getCommandNode(commandPtr ptr, const char* name);
allowPtr getAllowNode(allowPtr ptr, in_addr_t testIP);
enumPtr getEnumNode(enumPtr prt, char* search, size_t len);
enumPtr getDefaultEnumNode(enumPtr ptr, char* search);
icmdPtr getIcmdNode(icmdPtr ptr, const char* name);


typedef struct allow
{
    char* text;
    in_addr_t ip;
    in_addr_t mask;
    allowPtr next;
} Allow;

typedef struct compile
{
    int token;
    char* send;
    int len;
    unitPtr uPtr;
    char* errStr;
    compilePtr next;
} Compile;

typedef struct config
{
    char* tty;
    int port;
    char* logfile;
    char* devID;
    devicePtr devPtr;
    allowPtr aPtr;
    int syslog;
    int debug;
} Config;

typedef struct protocol
{
    char* name;
    char id;
    macroPtr mPtr;
    icmdPtr icPtr;
    protocolPtr next;
} Protocol;

typedef struct device
{
    char* name;
    char* id;
    commandPtr cmdPtr;
    protocolPtr protoPtr;
    devicePtr next;
} Device;

typedef struct unit
{
    char* name;
    char* abbrev;
    char* gCalc;
    char* sCalc;
    char* gICalc;
    char* sICalc;
    char* entity;
    char* type;
    enumPtr ePtr;
    unitPtr next;
} Unit;

typedef struct macro
{
    char* name;
    char* command;
    macroPtr next;
} Macro;

typedef struct command
{
    char* name;
    char* shortDescription;
    char* pcmd;
    char* send;
    char* addr;
    char* unit;
    char* errStr;
    char* precmd;
    size_t blockLength;
    size_t bytePosition;
    size_t byteLength;
    size_t bitPosition;
    size_t bitLength;
    int retry;
    unsigned short recvTimeout;
    char bit;
    char nodeType; /* 0==alles kopiert 1==alles Orig. 2== nur Adresse, unit len orig. */
    compilePtr cmpPtr;
    char* description;
    commandPtr next;
} Command;

typedef struct icmd
{
    char* name;
    char* send;
    unsigned char retry;
    unsigned short recvTimeout;
    icmdPtr next;
} iCmd;

typedef struct enumerate
{
    char* bytes;
    size_t len;
    char* text;
    enumPtr next;
} Enumerate;

#endif /* XMLCONFIG_H */