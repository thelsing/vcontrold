/* Routinen zum lesen von XML Dateien */

#include <cstdlib>
#include <cstdio>
#include <cstring>
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
unitPtr uPtr = NULL;
devicePtr devPtr = NULL;
configPtr cfgPtr = NULL;
commandPtr cmdPtr = NULL;

void freeAllLists();

protocolPtr newProtocolNode(protocolPtr ptr)
{
    protocolPtr nptr;

    if (ptr && ptr->next)
        return (newProtocolNode(ptr->next));

    nptr = (protocolPtr) calloc(1, sizeof(Protocol));

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
    return (nptr);
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



unitPtr newUnitNode(unitPtr ptr)
{
    unitPtr nptr;

    if (ptr && ptr->next)
        return (newUnitNode(ptr->next));

    nptr = (unitPtr)calloc(1, sizeof(Unit));

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

unitPtr getUnitNode(unitPtr ptr, const char* name)
{
    if (!ptr)
        return (NULL);

    if (ptr->abbrev && (strcmp(ptr->abbrev, name) != 0))
        return (getUnitNode(ptr->next, name));

    return (ptr);
}

void removeEnumList(enumPtr ptr)
{
    if (ptr && ptr->next)
        removeEnumList(ptr->next);

    if (ptr)
    {
        free(ptr->text);
        free(ptr->bytes);
        free(ptr);
    }
}

void removeUnitList(unitPtr ptr)
{
    if (ptr && ptr->next)
        removeUnitList(ptr->next);

    if (ptr)
    {
        removeEnumList(ptr->ePtr);
        free(ptr->name);
        free(ptr->abbrev);
        free(ptr->gCalc);
        free(ptr->sCalc);
        free(ptr->gICalc);
        free(ptr->sICalc);
        free(ptr->entity);
        free(ptr->type);
        free(ptr);
    }
}

macroPtr newMacroNode(macroPtr ptr)
{
    macroPtr nptr;

    if (ptr && ptr->next)
        return (newMacroNode(ptr->next));

    nptr = (macroPtr)calloc(1, sizeof(Macro));

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

    nptr = (commandPtr) calloc(1, sizeof(Command));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    nptr->next = NULL;
    nptr->cmpPtr = NULL;
    nptr->bit = 0;
    nptr->bytePosition = 0;
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

        if (ptr->nodeType > 0)   /* wurde aus der xml Datei gelesen */
        {
            if (ptr->addr)
                free(ptr->addr);

            if (ptr->precmd)
                free(ptr->precmd);

            if (ptr->unit)
                free(ptr->unit);

            if (ptr->pcmd)
                free(ptr->pcmd);

            if (ptr->errStr)
                free(ptr->errStr);

            if (ptr->nodeType == 1)   /* Originalkonoten */
            {
                if (ptr->name)
                    free(ptr->name);

                if (ptr->description)
                    free(ptr->description);
            }
        }

        free(ptr);
    }
}

devicePtr newDeviceNode(devicePtr ptr)
{
    devicePtr nptr;

    if (ptr && ptr->next)
        return (newDeviceNode(ptr->next));

    nptr = (devicePtr)calloc(1, sizeof(Device));

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

devicePtr getDeviceNode(devicePtr ptr, char* id)
{
    if (!ptr)
        return (NULL);

    if (strcmp(ptr->id, id) != 0)
        return (getDeviceNode(ptr->next, id));

    return (ptr);
}

void removeDeviceList(devicePtr ptr)
{
    if (ptr && ptr->next)
        removeDeviceList(ptr->next);

    if (ptr)
    {
        removeCommandList(ptr->cmdPtr);
        free(ptr->name);
        free(ptr->id);
        free(ptr);
    }
}



icmdPtr newIcmdNode(icmdPtr ptr)
{
    icmdPtr nptr;

    if (ptr && ptr->next)
        return (newIcmdNode(ptr->next));

    nptr = (icmdPtr)calloc(1, sizeof(iCmd));

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

    nptr = (allowPtr)calloc(1, sizeof(Allow));

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



enumPtr newEnumNode(enumPtr ptr)
{
    enumPtr nptr;

    if (ptr && ptr->next)
        return (newEnumNode(ptr->next));

    nptr = (enumPtr)calloc(1, sizeof(Enumerate));

    if (!nptr)
    {
        fprintf(stderr, "malloc gescheitert\n");
        exit(1);
    }

    if (ptr)
        ptr->next = nptr;

    return (nptr);
}


enumPtr getEnumNode(enumPtr ptr, char* search, size_t len)
{
    if (!ptr)
        return (NULL);

    /* len angegeben, wir suchen nach Bytes */
    if ((len > 0) && ptr->bytes && (memcmp(ptr->bytes, search, len) == 0))
        return (ptr);
    /* len == 0 -> Stringvergelich */
    else if (!len && ptr->text && (strcmp(ptr->text, search) == 0))
        return (ptr);
    else
        return (getEnumNode(ptr->next, search, len));
}

enumPtr getDefaultEnumNode(enumPtr ptr, char* search)
{
    if (!ptr)
        return NULL;
    /*  default Wert */
    else if (ptr->bytes == NULL)
        return ptr;
    else
        return getDefaultEnumNode(ptr->next, search);
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
    *ptr = (char*)calloc(1, sizeof(char));
    ** ptr = '\0';
}

configPtr parseConfig(xmlNodePtr cur)
{
    int serialFound = 0;
    int netFound = 0;
    int logFound = 0;
    configPtr cfgPtr;
    char* chrPtr;
    xmlNodePtr prevPtr;
    //char string[256];
    allowPtr aPtr;
    char ip[16];

    cfgPtr = (configPtr)calloc(1, sizeof(Config));
    cfgPtr->port = 0;
    cfgPtr->syslog = 0;
    cfgPtr->debug = 0;
    cfgPtr->aPtr = NULL;

    while (cur)
    {
        logIT(LOG_INFO, "CONFIG:(%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (strstr((char*)cur->name, "serial"))
        {
            serialFound = 1;
            prevPtr = cur;
            cur = cur->children;
        }
        else if (strstr((char*)cur->name, "net"))
        {
            netFound = 1;
            prevPtr = cur;
            cur = cur->children;
        }
        else if (strstr((char*)cur->name, "logging"))
        {
            logFound = 1;
            prevPtr = cur;
            cur = cur->children;
        }
        else if (strstr((char*)cur->name, "device"))
        {
            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"ID");
            logIT(LOG_INFO, "     Device ID=%s", cfgPtr->devID);

            if (chrPtr)
            {
                cfgPtr->devID = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cfgPtr->devID, chrPtr);
            }
            else
                nullIT(&cfgPtr->devID);

            cur = cur->next;
        }

        else if (serialFound && strstr((char*)cur->name, "tty"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cfgPtr->tty = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cfgPtr->tty, chrPtr);
            }
            else
                nullIT(&cfgPtr->devID);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (netFound && strstr((char*)cur->name, "port"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cfgPtr->port = atoi(chrPtr);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
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
#if 0
                strncpy(string, ptr + 1, sizeof(string) - 1);
                size = atoi(string);
#endif
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

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (logFound && strstr((char*)cur->name, "file"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cfgPtr->logfile = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cfgPtr->logfile, chrPtr);
            }
            else
                nullIT(&cfgPtr->devID);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (logFound && strstr((char*)cur->name, "syslog"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);
            ((*chrPtr == 'y') || (*chrPtr == '1')) ? (cfgPtr->syslog = 1) : (cfgPtr->syslog = 0);
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (logFound && strstr((char*)cur->name, "debug"))
        {
            chrPtr = getTextNode(cur);
            ((*chrPtr == 'y') || (*chrPtr == '1')) ? (cfgPtr->debug = 1) : (cfgPtr->debug = 0);
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
    }

    return (cfgPtr);

}

unitPtr parseUnit(xmlNodePtr cur)
{
    unitPtr uPtr;
    unitPtr uStartPtr = NULL;
    char* unit;
    char* chrPtr;
    int unitFound = 0;
    xmlNodePtr prevPtr;
    char string[256];
    enumPtr ePtr;

    while (cur)
    {
        logIT(LOG_INFO, "UNIT: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (cur->type == XML_TEXT_NODE)
        {
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "unit"))
        {
            unit = getPropertyNode(cur->properties, (xmlChar*)"name");

            if (unit)   /* neue Unit gelesen */
            {
                logIT(LOG_INFO, "Neue Unit: %s", unit);
                uPtr = newUnitNode(uStartPtr);

                if (!uStartPtr)
                    uStartPtr = uPtr;

                uPtr->name = (char*)calloc(strlen(unit) + 1, sizeof(char));
                strcpy(uPtr->name, unit);
                unitFound = 1;
                prevPtr = cur;
                cur = cur->children;
                continue;
            }
        }
        else if (unitFound && strstr((char*)cur->name, "enum"))
        {
            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"text");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s (text)", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                ePtr = newEnumNode(uPtr->ePtr);

                if (!uPtr->ePtr)
                    uPtr->ePtr = ePtr;

                ePtr->text = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strncpy(ePtr->text, chrPtr, strlen(chrPtr));
                chrPtr = getPropertyNode(cur->properties, (xmlChar*)"bytes");

                if (chrPtr)
                {
                    logIT(LOG_INFO, "          (%d) Node::Name=%s Type:%d Content=%s (bytes)", cur->line, cur->name, cur->type, chrPtr);
                    bzero(string, sizeof(string));
                    ePtr->len = string2chr(chrPtr, string, sizeof(string));
                    ePtr->bytes = (char*)calloc(ePtr->len, sizeof(char));
                    memcpy(ePtr->bytes, string, ePtr->len);
                }
            }
            else
            {
                logIT(LOG_ERR, "Property Node ohne text=");
                return (NULL);
            }

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (unitFound && strstr((char*)cur->name, "abbrev"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->abbrev = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->abbrev, chrPtr);
            }
            else
                nullIT(&uPtr->abbrev);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (unitFound && (strcmp((char*)cur->name, "calc") == 0))
        {
            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"get");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s (get)", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->gCalc = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->gCalc, chrPtr);
            }
            else
                nullIT(&uPtr->gCalc);

            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"set");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s (set)", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->sCalc = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->sCalc, chrPtr);
            }
            else
                nullIT(&uPtr->sCalc);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (unitFound && (strcmp((char*)cur->name, "icalc") == 0))
        {
            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"get");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s (get)", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->gICalc = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->gICalc, chrPtr);
            }
            else
                nullIT(&uPtr->gICalc);

            chrPtr = getPropertyNode(cur->properties, (xmlChar*)"set");
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s (set)", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->sICalc = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->sICalc, chrPtr);
            }
            else
                nullIT(&uPtr->sICalc);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (unitFound && strstr((char*)cur->name, "type"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->type = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->type, chrPtr);
            }
            else
                nullIT(&uPtr->type);

            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else if (unitFound && strstr((char*)cur->name, "entity"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                uPtr->entity = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(uPtr->entity, chrPtr);
            }
            else
                nullIT(&uPtr->entity);

            /* (cur->next && cur->next->next) ? (cur=cur->next) : (cur=prevPtr->next); */
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
        }
        else
        {
            logIT(LOG_ERR, "Fehler beim parsen unit");
            return (NULL);
        }
    }

    return (uStartPtr);
}

macroPtr parseMacro(xmlNodePtr cur)
{
    macroPtr mPtr;
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

    return NoConversion;
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

    return Array;
}

commandPtr parseCommand(xmlNodePtr cur, commandPtr cPtr, devicePtr dePtr)
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

                cPtr->nodeType = 1; /* keine Kopie, brauchen wir beim loeschen */

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
        else if (commandFound && strstr((char*)cur->name, "addr"))
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
        else if (commandFound && strstr((char*)cur->name, "bytePosition"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bytePosition = atoi(chrPtr);
            else
                cPtr->bytePosition = 0;
        }
        else if (commandFound && strstr((char*)cur->name, "byteLength"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->byteLength = atoi(chrPtr);
            else
                cPtr->byteLength = 0;
        }
        else if (commandFound && strstr((char*)cur->name, "bitPosition"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bitPosition = atoi(chrPtr);
            else
                cPtr->bitPosition = 0;
        }
        else if (commandFound && strstr((char*)cur->name, "bitLength"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->bitLength = atoi(chrPtr);
            else
                cPtr->bitLength = 0;
        }
        else if (commandFound && strstr((char*)cur->name, "parameter"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->parameter = parseParameterEnum(chrPtr);
            else
                cPtr->parameter = Array;
        }
        else if (commandFound && strstr((char*)cur->name, "conversion"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr-> conversion = parseConversionEnum(chrPtr);
            else
                cPtr->conversion = NoConversion;
        }
        else if (commandFound && strstr((char*)cur->name, "error"))
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
        else if (commandFound && strstr((char*)cur->name, "unit"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cPtr->unit = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cPtr->unit, chrPtr);
            }
            else
                nullIT(&cPtr->unit);
        }
        else if (commandFound && (strcmp((char*)cur->name, "precommand") == 0))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
            {
                cPtr->precmd = (char*)calloc(strlen(chrPtr) + 1, sizeof(char));
                strcpy(cPtr->precmd, chrPtr);
            }
            else
                nullIT(&cPtr->precmd);
        }
        else if (commandFound && strstr((char*)cur->name, "description"))
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
        else if (commandFound && strstr((char*)cur->name, "shortDescription"))
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
        else if (commandFound && strstr((char*)cur->name, "blockLength"))
        {
            chrPtr = getTextNode(cur);
            logIT(LOG_INFO, "   (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, chrPtr);

            if (chrPtr)
                cPtr->blockLength = atoi(chrPtr);
        }

        if (cur->next &&
            (!(cur->next->type == XML_TEXT_NODE) || cur->next->next))
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
    icmdPtr icPtr;
    icmdPtr icStartPtr = NULL;
    char* command;
    char* chrPtr;
    int commandFound = 0;
    xmlNodePtr prevPtr;

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

devicePtr parseDevice(xmlNodePtr cur, protocolPtr pPtr)
{
    devicePtr dPtr;
    devicePtr dStartPtr = NULL;
    char* proto;
    char* name;
    char* id;
    xmlNodePtr prevPtr;

    while (cur)
    {
        logIT(LOG_INFO, "DEVICE: (%d) Node::Name=%s Type:%d Content=%s", cur->line, cur->name, cur->type, cur->content);

        if (cur->type == XML_TEXT_NODE)
        {
            cur = cur->next;
            continue;
        }

        if (strstr((char*)cur->name, "device"))
        {
            name = getPropertyNode(cur->properties, (xmlChar*)"name");
            id = getPropertyNode(cur->properties, (xmlChar*)"ID");
            proto = getPropertyNode(cur->properties, (xmlChar*)"protocol");

            if (proto)   /* neues Protocol gelesen */
            {
                logIT(LOG_INFO, "    Neues Device: name=%s ID=%s proto=%s", name, id, proto);
                dPtr = newDeviceNode(dStartPtr);

                if (!dStartPtr)
                    dStartPtr = dPtr; /* Anker merken */

                if (name)
                {
                    dPtr->name = (char*)calloc(strlen(name) + 1, sizeof(char));
                    strcpy(dPtr->name, name);
                }
                else
                    nullIT(&dPtr->name);

                if (id)
                {
                    dPtr->id = (char*)calloc(strlen(id) + 1, sizeof(char));
                    strcpy(dPtr->id, id);
                }
                else
                    nullIT(&dPtr->id);

                if (!(dPtr->protoPtr = getProtocolNode(pPtr, proto)))
                {
                    logIT(LOG_ERR, "Protokoll %s nicht definiert", proto);
                    return (NULL);
                }

            }
            else
            {
                logIT(LOG_ERR, "Fehler beim parsen device");
                return (NULL);
            }

            prevPtr = cur;
            cur = cur->next;
        }
        else
            (cur->next &&
             (!(cur->next->type == XML_TEXT_NODE) || cur->next->next)) ? (cur = cur->next) : (cur = prevPtr->next);
    }

    return (dStartPtr);

}

protocolPtr parseProtocol(xmlNodePtr cur)
{
    int protoFound = 0;
    protocolPtr protoPtr;
    protocolPtr protoStartPtr = NULL;
    macroPtr mPtr;
    icmdPtr icPtr;
    char* proto;
    char* chrPtr;
    xmlNodePtr prevPtr;

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
    xmlDocPtr doc;
    xmlNodePtr cur, curStart;
    xmlNodePtr prevPtr;
    xmlNsPtr ns;
    //char string[256];
    devicePtr dPtr;
    commandPtr cPtr, ncPtr;
    protocolPtr TprotoPtr = NULL;
    unitPtr TuPtr = NULL;
    devicePtr TdevPtr = NULL;
    commandPtr TcmdPtr = NULL;
    configPtr TcfgPtr = NULL;



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
            else if (strstr((char*)cur->name, "units"))
            {
                if (!(TuPtr = parseUnit(cur->children)))
                    return (0);

                cur = prevPtr->next;
                continue;
            }
            else
                cur = prevPtr->next;
        }
        else if (strstr((char*)cur->name, "commands"))
        {
            if (!(TcmdPtr = parseCommand(cur->children, NULL, TdevPtr)))
                return (0);

            /* (cur->next && cur->next->next) ? (cur=cur->next) : (cur=prevPtr->next); */
            (cur->next) ? (cur = cur->next) : (cur = prevPtr->next);
            continue;
        }
        else if (strstr((char*)cur->name, "devices"))
        {
            if (!(TdevPtr = parseDevice(cur->children, TprotoPtr)))
                return (0);

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
                cPtr->bitLength = 8 - cPtr->bitPosition;

            cPtr->byteLength = 1;
        }

        cPtr->blockLength = cPtr->bytePosition + cPtr->byteLength;

        dPtr = TdevPtr;

        while (dPtr)
        {
            if (!getCommandNode(dPtr->cmdPtr, cPtr->name))
            {
                /* den kennen wir nicht und kopieren die Daten */
                logIT(LOG_INFO, "Kopiere Kommando %s nach Device %s", cPtr->name, dPtr->id);
                ncPtr = newCommandNode(dPtr->cmdPtr);

                if (!dPtr->cmdPtr)
                    dPtr->cmdPtr = ncPtr;

                ncPtr->name = cPtr->name;
                ncPtr->pcmd = cPtr->pcmd;
                ncPtr->addr = cPtr->addr;
                ncPtr->unit = cPtr->unit;
                ncPtr->bit = cPtr->bit;
                ncPtr->errStr = cPtr->errStr;
                ncPtr->precmd = cPtr->precmd;
                ncPtr->description = cPtr->description;
                ncPtr->blockLength = cPtr->blockLength;
                ncPtr->bytePosition = cPtr->bytePosition;
                ncPtr->byteLength = cPtr->byteLength;
                ncPtr->shortDescription = cPtr->shortDescription;
                ncPtr->bitPosition = cPtr->bitPosition;
                ncPtr->bitLength = cPtr->bitLength;
                ncPtr->conversion = cPtr->conversion;
                ncPtr->parameter = cPtr->parameter;
            }

            dPtr = dPtr->next;
        }

        cPtr = cPtr->next;
    }

    /* wir suchen das default Devive  */

    if (!(TcfgPtr->devPtr = getDeviceNode(TdevPtr, TcfgPtr->devID)))
    {
        logIT(LOG_ERR, "Device %s nicht definiert\n", TcfgPtr->devID);
        return (0);
    }

    /* wenn wir hier angekommen sind, war das Laden erfolgreich */
    /* nun die alten Liste freigeben und die neuen zuweisen */

    /* falls wir wiederholt augferufen werden (sighup) sollte alles frei gegeben werden */
    freeAllLists();

    protoPtr = TprotoPtr;
    uPtr = TuPtr;
    devPtr = TdevPtr;
    cmdPtr = TcmdPtr;
    cfgPtr = TcfgPtr;

    xmlFreeDoc(doc);
    return (1);


}


void freeAllLists()
{
    removeProtocolList(protoPtr);
    removeUnitList(uPtr);
    removeDeviceList(devPtr);
    removeCommandList(cmdPtr);
    protoPtr = NULL;
    uPtr = NULL;
    devPtr = NULL;
    cmdPtr = NULL;

    if (cfgPtr)
    {
        free(cfgPtr->tty);
        free(cfgPtr->logfile);
        free(cfgPtr->devID);
        removeAllowList(cfgPtr->aPtr);
        free(cfgPtr);
        cfgPtr = NULL;
    }
}

