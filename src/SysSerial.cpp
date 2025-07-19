#include "sysPlatform/SysSerial.h"

namespace SysPlatform {

SysSerial::SysSerial(unsigned busId)
{

}

SysSerial::~SysSerial()
{

}

void SysSerial::begin(uint32_t baud)
{
    return;
}

int SysSerial::available()
{
    return 0;
}

int SysSerial::read()
{
    return 0;
}

size_t SysSerial::write(uint8_t dataByte)
{
    return 0;
}

void SysSerial::flush(void)
{
    return;
}

int SysSerial::printf(const char *fmt, ...)
{
    int result = 0;
    va_list args;

    va_start(args, fmt);
    //result = printf(fmt, args);
    va_end(args);

    return result;
}

}