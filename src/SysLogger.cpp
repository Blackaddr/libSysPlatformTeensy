#include <cstdio>
#include "Arduino.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysLogger.h"

namespace SysPlatform {

SysLogger sysLogger;

SysLogger::SysLogger()
{

}

SysLogger::~SysLogger()
{

}

void SysLogger::begin()
{
    Serial.begin(115200);  // default baud rate
}

void SysLogger::begin(unsigned baudRate)
{
    Serial.begin(baudRate);
}


void SysLogger::getSeverityStr(LogSeverity severity, char* str)
{
    switch(severity) {
    case LogPanic : strcpy(str, "LogPanic"); return;
    case LogError : strcpy(str, "LogError"); return;
    case LogWarning : strcpy(str, "LogWarning"); return;
    case LogNotice : strcpy(str, "LogNotice"); return;
    case LogDebug : strcpy(str, "LogDebug"); return;
    default : return;
    }
}

int SysLogger::printf(const char *fmt, ...)
{
    int result = 0;
    va_list args;

    va_start(args, fmt);
    result = Serial.vprintf(fmt, args);
    va_end(args);

    return result;
}

int SysLogger::vprintf(const char * format, va_list arg)
{
    return Serial.vprintf(format, arg);
}

void SysLogger::log(const char *pMessage, ...)
{
    va_list args;

    va_start(args, pMessage);
    Serial.printf(pMessage, args);
    va_end(args);
}

void SysLogger::log(const char *pSource, LogSeverity severity, const char *pMessage, ...)
{
    va_list args;

    va_start(args, pMessage);
    char buf[16];
    getSeverityStr(severity, buf);
    Serial.printf("%s: %s, %s\n", pSource, buf, pMessage);
    va_end(args);
}

unsigned SysLogger::available() {
    return Serial.available();
}

int SysLogger::read()
{
    return Serial.read();
}

void SysLogger::flush()
{
    Serial.flush();
}

SysLogger::operator bool() const
{
    if (Serial) { return true; }
    else { return false; }
}

}
