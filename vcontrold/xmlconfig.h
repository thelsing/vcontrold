#ifndef XMLCONFIG_H
#define XMLCONFIG_H

#include <string>
#include <arpa/inet.h>


typedef struct config* configPtr;
typedef struct protocol* protocolPtr;
typedef struct macro* macroPtr;
typedef struct command* commandPtr;
typedef struct compile* compilePtr;
typedef struct icmd* icmdPtr;
typedef struct allow* allowPtr;


int parseXMLFile(const char* filename);
macroPtr getMacroNode(macroPtr ptr, const char* name);
commandPtr getCommandNode(commandPtr ptr, const char* name);
allowPtr getAllowNode(allowPtr ptr, in_addr_t testIP);
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
    size_t len;
    char* errStr;
    compilePtr next;
} Compile;

typedef struct config
{
    std::string tty;
    int port;
    std::string logfile;
    allowPtr aPtr;
    std::string protocolId;
    protocolPtr protoPtr;
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

typedef struct macro
{
    char* name;
    char* command;
    macroPtr next;
} Macro;

enum Parameter
{
    Byte,
    SByte,
    Int,
    SInt,
    Int4,
    SInt4,
    Array,
};

enum Conversion
{
    NoConversion,
    Div10,
    Sec2Hour,
    DateBCD,
    HexBytes2AsciiByte,
    HeyByte2UTF16Byte,
    Mult100,
    LastBurnerCheck,
    LastCheckInterval,
    RotateBytes,
    Mult10,
    Mult2,
    Div100,
    Div2,
    DateTimeBCD,
    Mult5,
    MultOffset,
};

typedef struct command
{
    char* name;
    char* shortDescription;
    char* pcmd;
    char* send;
    uint16_t addr;
    char* addrStr;
    char* errStr;
    Parameter parameter;
    Conversion conversion;
    double conversionFactor;
    double conversionOffset;
    size_t blockLength;
    size_t bytePosition;
    size_t byteLength;
    char bitPosition;
    char bitLength;
    int retry;
    unsigned short recvTimeout;
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

#endif /* XMLCONFIG_H */
