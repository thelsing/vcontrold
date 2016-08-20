/* Routinen zum lesen von XML Dateien */

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string>
#include <syslog.h>
#include <libxml/parser.h>
#include <libxml/xinclude.h>
#include <arpa/inet.h>

#include "xmlconfig.h"
#include "common.h"
#include "parser.h"

#if defined(__FreeBSD__)
    #include <netinet/in.h>
#endif

/* globale Variablen */
protocolPtr protoPtr = NULL;
configPtr cfgPtr = NULL;
commandPtr cmdPtr = NULL;

void freeAllLists();

protocolPtr newProtocolNode(protocolPtr ptr)
{
    protocolPtr nptr;

    if (ptr && ptr->next)
        return (newProtocolNode(ptr->next));

    nptr = (protocolPtr) malloc(sizeof(Protocol));
    memset(nptr, 0, sizeof(Protocol));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    nptr->next = NULL;
    nptr->icPtr = NULL;
    nptr->id = 0;
    return nptr;
}

protocolPtr getProtocolNode(protocolPtr ptr, const char* name)
{
    if (!ptr)
        return (NULL);

    if (strcmp(ptr->name, name) != 0)
        return (getProtocolNode(ptr->next, name));

    return (ptr);
}

void removeMacroList(macroPtr ptr)
{
    if (ptr && ptr->next)
        removeMacroList(ptr->next);

    if (ptr)
    {
        free(ptr->name);
        free(ptr->command);
        free(ptr);
    }
}

void removeIcmdList(icmdPtr ptr)
{
    if (ptr && ptr->next)
        removeIcmdList(ptr->next);

    if (ptr)
    {
        free(ptr->name);
        free(ptr->send);
        free(ptr);
    }
}

void removeProtocolList(protocolPtr ptr)
{
    if (ptr && ptr->next)
        removeProtocolList(ptr->next);

    if (ptr)
    {
        removeMacroList(ptr->mPtr);
        removeIcmdList(ptr->icPtr);
        free(ptr->name);
        free(ptr);
    }
}

macroPtr newMacroNode(macroPtr ptr)
{
    macroPtr nptr;

    if (ptr && ptr->next)
        return (newMacroNode(ptr->next));

    nptr = (macroPtr)malloc(sizeof(Macro));
    memset(nptr, 0, sizeof(Macro));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    nptr->next = NULL;
    return (nptr);
}

macroPtr getMacroNode(macroPtr ptr, const char* name)
{
    if (!ptr)
        return (NULL);

    if (ptr->name && strcmp(ptr->name, name) != 0)
        /* if (*name && !(strstr(ptr->name,name)==ptr->name))  */
        return (getMacroNode(ptr->next, name));

    return (ptr);
}

commandPtr newCommandNode(commandPtr ptr)
{
    commandPtr nptr;

    if (ptr && ptr->next)
        return (newCommandNode(ptr->next));

    nptr = (commandPtr) malloc(sizeof(Command));
    memset(nptr, 0, sizeof(Command));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    return nptr;
}

void addCommandNode(commandPtr ptr, commandPtr nptr)
{
    if (!ptr)
        return;
    else if (ptr->next)
        addCommandNode(ptr->next, nptr);
    else
        ptr->next = nptr;
}

commandPtr getCommandNode(commandPtr ptr, const char* name)
{
    if (!ptr)
        return (NULL);

    if (ptr->name && name && (strcmp(ptr->name, name) != 0))
        return (getCommandNode(ptr->next, name));

    return (ptr);
}

void removeCommandList(commandPtr ptr)
{
    if (ptr && ptr->next)
        removeCommandList(ptr->next);

    if (ptr)
    {
        removeCompileList(ptr->cmpPtr);
        free(ptr->send);

        if (ptr->addr)
            free(ptr->addr);

        if (ptr->pcmd)
            free(ptr->pcmd);

        if (ptr->errStr)
            free(ptr->errStr);

        if (ptr->name)
            free(ptr->name);

        if (ptr->description)
            free(ptr->description);

        free(ptr);
    }
}

icmdPtr newIcmdNode(icmdPtr ptr)
{
    icmdPtr nptr;

    if (ptr && ptr->next)
        return (newIcmdNode(ptr->next));

    nptr = (icmdPtr)malloc(sizeof(iCmd));
    memset(nptr, 0, sizeof(iCmd));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    nptr->next = NULL;
    return (nptr);
}

icmdPtr getIcmdNode(icmdPtr ptr, const char* name)
{
    if (!ptr)
        return (NULL);

    if (ptr->name && (strcmp(ptr->name, name) != 0))
        return (getIcmdNode(ptr->next, name));

    return (ptr);
}

allowPtr getAllowNode(allowPtr ptr, const in_addr_t testIP)
{
    if (!ptr)
        return (NULL);

    if ((ntohl(ptr->ip) & ptr->mask) == (ntohl(testIP) & ptr->mask))
        return (ptr);

    return (getAllowNode(ptr->next, testIP));
}

allowPtr newAllowNode(allowPtr ptr)
{
    allowPtr nptr;

    if (ptr && ptr->next)
        return (newAllowNode(ptr->next));

    nptr = (allowPtr)malloc(sizeof(Allow));
    memset(nptr, 0, sizeof(Allow));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    return (nptr);
}

void removeAllowList(allowPtr ptr)
{
    if (ptr && ptr->next)
        removeAllowList(ptr->next);

    if (ptr)
    {
        free(ptr->text);
        free(ptr);
    }
}

void printNode(xmlNodePtr ptr)
{
    static int blanks = 0;
    int n;

    if (!ptr)
        return;

    for (n = 0; n <= blanks; n++)
        printf(" ");

    if ((ptr->type == XML_ELEMENT_NODE) || (ptr->type == XML_TEXT_NODE))
        printf("(%d) Node::Name=%s Type:%d Content=%s\n", ptr->line, ptr->name, ptr->type, ptr->content);
    else
        printf("Node::Name=%s\n", ptr->name);

    if ((ptr->type == XML_ELEMENT_NODE) && ptr->properties)
    {
        blanks++;
        printNode((xmlNodePtr)ptr->properties);
        blanks--;
    }

    if (ptr->children)
    {
        blanks++;
        printNode(ptr->children);
        blanks--;
    }

    if (ptr->next)
        printNode(ptr->next);
}

char* getTextNode(xmlNodePtr cur)
{
    if ((cur->children) && (cur->children->type == XML_TEXT_NODE))
        return ((char*)cur->children->content);
    else
        return (NULL);
}

char* getPropertyNode(xmlAttrPtr cur, xmlChar* name)
{
    if ((cur) &&
        (cur->type == XML_ATTRIBUTE_NODE) &&
        strstr((char*)cur->name, (char*)name))
        return ((char*)getTextNode((xmlNodePtr)cur));
    else if (cur && cur->next)
        return ((char*)getPropertyNode(cur->next, name));
    else
        return (NULL);
}

void nullIT(char** ptr)
{
    *ptr = (char*)malloc(sizeof(char));
    ** ptr = '\0';
}

configPtr parseConfig(xmlNodePtr cur)
{
    int serialFound = 0;
    int netFound = 0;
    int logFound = 0;
    configPtr cfgPtr = 0;
    char* chrPtr = 0;
    xmlNodePtr prevPtr = 0;
    allowPtr aPtr = 0;
    char ip[16];

    cfgPtr = new Config();

    while (cur)
    {
        logIT(LOG_INFO, "CONFIG:(%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (strstr((char*)cur->name, "serial"))
        {
            serialFound = 1;
            prevPtr = cur;
            cur = cur->children;
            continue;
        }
        else if (strstr((char*)cur->name, "net"))
        {
            netFound = 1;
            prevPtr = cur;
            cur = cur->children;
            continue;
        }
        else if (strstr((char*)cur->name, "logging"))
        {
            logFound = 1;
            prevPtr = cur;
            cur = cur->children;
            continue;
        }
        else if (strcmp((char*)cur->name, "protocol") == 0)
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cfgPtr->protocolId = chrPtr;
            else
                cfgPtr->protocolId = "";

        }

        else if (serialFound && strstr((char*)cur->name, "tty"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cfgPtr->tty = chrPtr;
            else
                cfgPtr->tty = "";

        }
        else if (netFound && strstr((char*)cur->name, "port"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cfgPtr->port = atoi(chrPtr);

        }
        else if (netFound && strstr((char*)cur->name, "allow"))
        {
            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"ip");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            /* wir zerlegen nun chrPtr in ip/size
            ist keine Maske angegeben dann nehmen wir /32 an
            danach wird eine inverser Maske gebaut und in mask gespeichert
            ip==text Inhalt IP-Adresse mask==Bitmaske */

            char* ptr;
            short count;
            int size;
            in_addr_t mask;

            bzero(ip, sizeof(ip));

            //bzero(string,sizeof(string));
            if ((ptr = strchr(chrPtr, '/')))
            {
                size = atoi(ptr + 1);
                strncpy(ip, chrPtr, (size_t)(ptr - chrPtr));
            }
            else
            {
                strncpy(ip, chrPtr, sizeof(ip) - 1);
                size = 32;
            }

            if (inet_addr(ip) != INADDR_NONE)
            {
                aPtr = newAllowNode(cfgPtr->aPtr);
                aPtr->text = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(aPtr->text, chrPtr);
                mask = 0;

                /* wir basteln die Bitmaske */
                if (size)
                {
                    mask = 0x80000000;

                    for (count = 0; count < size - 1; count++)
                    {
                        mask >>= 1;
                        mask |= 0x80000000;
                    }
                }

                aPtr->mask = mask;
                aPtr->ip = inet_addr(ip);

                if (!cfgPtr->aPtr)
                    cfgPtr->aPtr = aPtr;

                logIT(LOG_INFO, "     Allow IP:%s Size:/%d", ip, size);
            }
        }
        else if (logFound && strstr((char*)cur->name, "file"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cfgPtr->logfile = chrPtr;
            else
                cfgPtr->logfile = "";
        }
        else if (logFound && strstr((char*)cur->name, "syslog"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);
            ((*chrPtr == 'y') || (*chrPtr == '1')) ? (cfgPtr->syslog = 1) : (cfgPtr->syslog = 0);
        }
        else if (logFound && strstr((char*)cur->name, "debug"))
        {
            chrPtr = getTextNode(cur);
            ((*chrPtr == 'y') || (*chrPtr == '1')) ? (cfgPtr->debug = 1) : (cfgPtr->debug = 0);
        }

        if (cur->next && (cur->next->type != XML_TEXT_NODE || cur->next->next))
            cur = cur->next;
        else if (prevPtr)
        {
            cur = prevPtr->next;
            prevPtr = 0;
        }
        else
            cur = 0;
    }

    return (cfgPtr);

}

macroPtr parseMacro(xmlNodePtr cur)
{
    macroPtr mPtr = 0;
    macroPtr mStartPtr = NULL;
    char* macro;
    char* chrPtr;
    int macroFound = 0;
    xmlNodePtr prevPtr;

    while (cur)
    {
        logIT(LOG_INFO, "MACRO: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (cur->type == XML_TEXT_NODE)
        {
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "macro"))
        {
            macro = getPropertyNode(cur->properties, (xmlChar*)"name");

            if (macro)   /* neues Macro gelesen */
            {
                logIT(LOG_INFO, "Neues Macro: %s", macro);
                mPtr = newMacroNode(mStartPtr);

                if (!mStartPtr)
                    mStartPtr = mPtr;

                mPtr->name = (char*)calloc(strlen(macro) + 1, sizeof(char));
                strcpy(mPtr->name, macro);
                macroFound = 1;
                prevPtr = cur;
                cur = cur->children;
                continue;
            }
        }
        else if (macroFound && strstr((char*)cur->name, "command"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                mPtr->command = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(mPtr->command, chrPtr);
            }
            else
                nullIT(&mPtr->command);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
        {
            logIT(LOG_INFO, "Fehler beim parsen macro");
            return (NULL);
        }
    }

    return (mStartPtr);
}

Conversion parseConversionEnum(const std::string tagContent)
{
    if (tagContent == "NoConversion")
        return NoConversion;

    if (tagContent == "Div10")
        return Div10;

    if (tagContent == "Sec2Hour")
        return Sec2Hour;

    if (tagContent == "DateBCD")
        return DateBCD;

    if (tagContent == "HexBytes2AsciiByte")
        return HexBytes2AsciiByte;

    if (tagContent == "HeyByte2UTF16Byte")
        return HeyByte2UTF16Byte;

    if (tagContent == "Mult100")
        return Mult100;

    if (tagContent == "LastBurnerCheck")
        return LastBurnerCheck;

    if (tagContent == "LastCheckInterval")
        return LastCheckInterval;

    if (tagContent == "RotateBytes")
        return RotateBytes;

    if (tagContent == "Mult10")
        return Mult10;

    if (tagContent == "Mult2")
        return Mult2;

    if (tagContent == "Div100")
        return Div100;

    if (tagContent == "Div2")
        return Div2;

    if (tagContent == "DateTimeBCD")
        return DateTimeBCD;

    if (tagContent == "Mult5")
        return Mult5;

    if (tagContent == "MultOffset")
        return MultOffset;

    throw std::logic_error(std::string("conversion not found: ") + std::string(tagContent));
}

Parameter parseParameterEnum(const std::string tagContent)
{
    if (tagContent == "Byte")
        return Byte;

    if (tagContent == "SByte")
        return SByte;

    if (tagContent == "Int")
        return Int;

    if (tagContent == "SInt")
        return SInt;

    if (tagContent == "Int4")
        return Int4;

    if (tagContent == "SInt4")
        return SInt4;

    if (tagContent == "Array")
        return Array;

    throw std::logic_error(std::string("parameterType not found: ") + std::string(tagContent));
}


commandPtr parseCommand(xmlNodePtr cur, commandPtr cPtr)
{
    commandPtr cStartPtr = NULL;
    char* command;
    char* protocmd;
    char* chrPtr;
    xmlNodePtr prevPtr;
    char string[256];	// TODO: get rid of that one
    int commandFound;

    /* wir unterscheiden ob der Aufruf rekursiv erfolgte,
           dann ist cPtr gesetzt */
    if (!cPtr)
        commandFound = 0;
    else
    {
        commandFound = 1;
        prevPtr = NULL;
    }

    while (cur)
    {
        logIT(LOG_INFO, "COMMAND: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (xmlIsBlankNode(cur))
        {
            cur = cur->next;
            continue;
        }

        if (strcmp((char*)cur->name, "command") == 0)
        {
            command = getPropertyNode(cur->properties, (xmlChar*)"name");
            protocmd = getPropertyNode(cur->properties, (xmlChar*)"protocmd");

            if (command)   /* neues Command gelesen */
            {
                logIT(LOG_INFO, "Neues Command: %s", command);
                cPtr = newCommandNode(cStartPtr);

                if (!cStartPtr)
                    cStartPtr = cPtr;

                if (command)
                {
                    cPtr->name = (char*)calloc(strlen(command) + 1, sizeof(char));
                    strcpy(cPtr->name, command);
                }
                else
                    nullIT(&cPtr->name);

                if (protocmd)
                {
                    cPtr->pcmd = (char*)calloc(strlen(protocmd) + 1, sizeof(char));
                    strcpy(cPtr->pcmd, protocmd);
                }
                else
                    nullIT(&cPtr->pcmd);

                commandFound = 1;
                prevPtr = cur;
                cur = cur->children;
                continue;
            }
        }
        else if (commandFound && (strcmp((char*)cur->name, "addr") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cPtr->addr = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cPtr->addr, chrPtr);
            }
            else
                nullIT(&cPtr->addr);
        }
        else if (commandFound && (strcmp((char*)cur->name, "bytePosition") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bytePosition = atoi(chrPtr);
            else
                cPtr->bytePosition = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "byteLength") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->byteLength = atoi(chrPtr);
            else
                cPtr->byteLength = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "bitPosition") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bitPosition = (char)atoi(chrPtr);
            else
                cPtr->bitPosition = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "bitLength") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bitLength = (char)atoi(chrPtr);
            else
                cPtr->bitLength = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "parameter") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->parameter = parseParameterEnum(chrPtr);
            else
                cPtr->parameter = Array;
        }
        else if (commandFound && (strcmp((char*)cur->name, "conversion") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr-> conversion = parseConversionEnum(chrPtr);
            else
                cPtr->conversion = NoConversion;
        }
        else if (commandFound && (strcmp((char*)cur->name, "conversionFactor") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->conversionFactor = atoi(chrPtr);
            else
                cPtr->conversionFactor = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "conversionOffset") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->conversionOffset = atoi(chrPtr);
            else
                cPtr->conversionOffset = 0;
        }
        else if (commandFound && (strcmp((char*)cur->name, "error") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                bzero(string, sizeof(string));

                size_t count = string2chr(chrPtr, string, sizeof(string));

                if (count)
                {
                    cPtr->errStr = (char*)calloc(count, sizeof(char));
                    memcpy(cPtr->errStr, string, count);
                }
            }
            else
                nullIT(&cPtr->errStr);
        }
        else if (commandFound && (strcmp((char*)cur->name, "description") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cPtr->description = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cPtr->description, chrPtr);
            }
            else
                nullIT(&cPtr->description);
        }
        else if (commandFound && (strcmp((char*)cur->name, "shortDescription") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cPtr->shortDescription = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cPtr->shortDescription, chrPtr);
            }
            else
                nullIT(&cPtr->shortDescription);
        }
        else if (commandFound && (strcmp((char*)cur->name, "blockLength") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->blockLength = atoi(chrPtr);
        }

        if (cur->next && (cur->next->type != XML_TEXT_NODE || cur->next->next))
            cur = cur->next;
        else if (prevPtr)
            cur = prevPtr->next;
        else
            cur = NULL;
    }

    return (cStartPtr);
}

icmdPtr parseICmd(xmlNodePtr cur)
{
    icmdPtr icPtr = 0;
    icmdPtr icStartPtr = 0;
    char* command = 0;
    char* chrPtr = 0;
    int commandFound = 0;
    xmlNodePtr prevPtr = 0;

    while (cur)
    {
        logIT(LOG_INFO, "ICMD: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (xmlIsBlankNode(cur))
        {
            /* if (cur->type == XML_TEXT_NODE) { */
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "command"))
        {
            command = getPropertyNode(cur->properties, (xmlChar*)"name");

            if (command)   /* neues Command gelesen */
            {
                logIT(LOG_INFO, "Neues iCommand: %s", command);
                icPtr = newIcmdNode(icStartPtr);

                if (!icStartPtr)
                    icStartPtr = icPtr;

                icPtr->name = (char*)calloc(strlen(command) + 1, sizeof(char));
                strcpy(icPtr->name, command);
                commandFound = 1;
                prevPtr = cur;
                cur = cur->children;
                continue;
            }
        }
        else if (commandFound && strstr((char*)cur->name, "send"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                icPtr->send = (char*)calloc(strlen(chrPtr) + 2, sizeof(char));
                strcpy(icPtr->send, chrPtr);
            }
            else
                nullIT(&icPtr->send);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (commandFound && strstr((char*)cur->name, "retry"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                icPtr->retry = (char)atoi(chrPtr);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (commandFound && strstr((char*)cur->name, "recvTimeout"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                icPtr->recvTimeout = (unsigned short)atoi(chrPtr);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
        {
            logIT(LOG_ERR, "Fehler beim parsen command");
            return (NULL);
        }
    }

    return (icStartPtr);
}

protocolPtr parseProtocol(xmlNodePtr cur)
{
    int protoFound = 0;
    protocolPtr protoPtr = 0;
    protocolPtr protoStartPtr = 0;
    macroPtr mPtr = 0;
    icmdPtr icPtr = 0;
    char* proto = 0;
    char* chrPtr = 0;
    xmlNodePtr prevPtr = 0;

    while (cur)
    {
        logIT(LOG_INFO, "PROT: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (cur->type == XML_TEXT_NODE)
        {
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "protocol"))
        {
            proto = getPropertyNode(cur->properties, (xmlChar*)"name");

            if (proto)   /* neues Protocol gelesen */
            {
                logIT(LOG_INFO, "Neues Protokoll: %s", proto);
                protoPtr = newProtocolNode(protoStartPtr);

                if (!protoStartPtr)
                    protoStartPtr = protoPtr; /* Anker merken */

                if (proto)
                {
                    protoPtr->name = (char*)calloc(strlen(proto) + 1, sizeof(char));
                    strcpy(protoPtr->name, proto);
                }
                else
                    nullIT(&protoPtr->name);
            }
            else
            {
                logIT(LOG_ERR, "Fehler beim parsen proto");
                return (NULL);
            }

            protoFound = 1;
            prevPtr = cur;
            cur = cur->children;
        }
        else if (protoFound && strstr((char*)cur->name, "pid"))
        {
            chrPtr = getTextNode(cur);
            protoPtr->id = hex2chr(chrPtr);
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (protoFound && strstr((const char*)cur->name, "macros"))
        {
            mPtr = parseMacro(cur->children);

            if (mPtr)
                protoPtr->mPtr = mPtr;

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (protoFound && strstr((char*)cur->name, "commands"))
        {
            icPtr = parseICmd(cur->children);

            if (icPtr)
                protoPtr->icPtr = icPtr;

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
    }

    return (protoStartPtr);

}


void removeComments(xmlNodePtr node)
{
    while (node)
    {
        //printf("type:%d name=%s\n",node->type, node->name);
        if (node->children) 						// if the node has children process the children
            removeComments(node->children);

        if (node->type == XML_COMMENT_NODE)  		// if the node is a comment?
        {
            //printf("found comment\n");
            if (node->next)
                removeComments(node->next);

            xmlUnlinkNode(node);				//unlink
            xmlFreeNode(node);					//and free the node
        }

        node = node->next;
    }
}

int parseXMLFile(const char* filename)
{
    xmlDocPtr doc = 0;
    xmlNodePtr cur = 0;
    xmlNodePtr curStart = 0;
    xmlNodePtr prevPtr = 0;
    xmlNsPtr ns = 0;
    commandPtr cPtr = 0;
    protocolPtr TprotoPtr = 0;
    commandPtr TcmdPtr = 0;
    configPtr TcfgPtr = 0;



    xmlKeepBlanksDefault(0);
    doc = xmlParseFile(filename);

    if (doc == NULL)
        return (0);

    curStart = xmlDocGetRootElement(doc);
    cur = curStart;

    if (cur == NULL)
    {
        logIT(LOG_ERR, "empty document\n");
        xmlFreeDoc(doc);
        return (0);
    }

    ns = xmlSearchNsByHref(doc, cur, (const xmlChar*) "http://www.openv.de/vcontrol");

    if (ns == NULL)
    {
        logIT(LOG_ERR, "document of the wrong type, vcontrol Namespace not found");
        xmlFreeDoc(doc);
        return (0);
    }

    if (xmlStrcmp(cur->name, (const xmlChar*) "V-Control"))
    {
        logIT(LOG_ERR, "document of the wrong type, root node != V-Control");
        xmlFreeDoc(doc);
        return (0);
    }

    /* Xinlcude durchfuehren */
    int xc = 0;

    if ((xc = xmlXIncludeProcessFlags(doc, XML_PARSE_XINCLUDE | XML_PARSE_NOXINCNODE)) == 0)
        logIT(LOG_WARNING, "Kein XInclude durchgefuehrt");
    else if (xc < 0)
    {
        logIT(LOG_ERR, "Fehler bei XInclude");
        return (0);
    }
    else
        logIT(LOG_INFO, "%d XInclude durchgefuehrt", xc);

    removeComments(cur);		// now the xml tree is complete -> remove all comments

    int unixFound = 0;
    int protocolsFound = 0;
    cur = cur->children;

    while (cur)
    {
        logIT(LOG_INFO, "XML: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (xmlIsBlankNode(cur))
        {
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "unix"))
        {
            if (unixFound)   /* hier duerfen wir nie hinkommen, 2. Durchlauf */
            {
                logIT(LOG_ERR, "Fehler in der XML Konfig");
                return (0);
            }

            prevPtr = cur;
            unixFound = 1;
            cur = cur->children; /* unter Unix folgt config , also children */
            continue;
        }

        if (strstr((char*)cur->name, "extern"))
        {
            prevPtr = cur;
            cur = cur->children;

            if (cur && strstr((char*)cur->name, "vito"))
            {
                prevPtr = cur;
                cur = cur->children; /* der xinclude Kram steht unter <extern><vito> */
                continue;
            }
            else if (strstr((char*)cur->name, "protocols"))
            {
                if (protocolsFound)   /* hier duerfen wir nie hinkommen, 2. Durchlauf */
                {
                    logIT(LOG_ERR, "Fehler in der XML Konfig");
                    return (0);
                }

                protocolsFound = 1;

                if (!(TprotoPtr = parseProtocol(cur->children)))
                    return (0);

                cur = prevPtr->next;
                continue;
            }
            else
                cur = prevPtr->next;
        }
        else if (strstr((char*)cur->name, "commands"))
        {
            if (!(TcmdPtr = parseCommand(cur->children, NULL)))
                return (0);

            /* (cur->next && cur->next->next) ? (cur=cur->next) : (cur=prevPtr->next); */
            (cur->next) ? (cur = cur->next) : (cur = prevPtr->next);
            continue;
        }
        else if (strstr((char*)cur->name, "config"))
        {
            if (!(TcfgPtr = parseConfig(cur->children)))
                return (0);

            (cur->next && cur->next->next) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
            cur = cur->next;
    }

    /* fuer alle Kommandos, die Default Definitionen haben, grasen wir alle Devices ab
       und ergaenzen dort die einzelnen Befehle */
    cPtr = TcmdPtr;

    while (cPtr)
    {
        /*sanitize command*/
        if (!cPtr->byteLength)
            cPtr->byteLength = cPtr->blockLength;

        if (cPtr->bitPosition || cPtr->bitLength)
        {
            size_t additionalOffset = cPtr->bitPosition / 8;
            cPtr->bitPosition %= 8;
            cPtr->bytePosition += additionalOffset;

            if (!cPtr->bitLength)
                cPtr->bitLength = 1;

            if (cPtr->bitPosition + cPtr->bitLength > 8)
                cPtr->bitLength = (char)(8 - cPtr->bitPosition);

            cPtr->byteLength = 1;
        }

        //cPtr->blockLength = cPtr->bytePosition + cPtr->byteLength;

        cPtr = cPtr->next;
    }

    /* wir suchen das protocol  */

    if (!(TcfgPtr->protoPtr = getProtocolNode(TprotoPtr, TcfgPtr->protocolId.c_str())))
    {
        logIT(LOG_ERR, "Protocol %s nicht definiert\n", TcfgPtr->protocolId.c_str());
        return (0);
    }

    /* wenn wir hier angekommen sind, war das Laden erfolgreich */
    /* nun die alten Liste freigeben und die neuen zuweisen */

    /* falls wir wiederholt augferufen werden (sighup) sollte alles frei gegeben werden */
    freeAllLists();

    protoPtr = TprotoPtr;
    cmdPtr = TcmdPtr;
    cfgPtr = TcfgPtr;

    xmlFreeDoc(doc);
    return (1);


}


void freeAllLists()
{
    removeProtocolList(protoPtr);
    removeCommandList(cmdPtr);
    protoPtr = NULL;
    cmdPtr = NULL;

    if (cfgPtr)
    {
        removeAllowList(cfgPtr->aPtr);
        delete cfgPtr;
        cfgPtr = NULL;
    }
}

