#include "Arduino.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysTimer.h"

namespace SysPlatform {

// milliseconds since program start
uint32_t SysTimer::millis()
{
    return ::millis();
}

// microseonds since program start
uint32_t SysTimer::micros()
{
    return ::micros();
}

// CPU cycle counts since program start
uint32_t SysTimer::cycleCnt32()
{
    return ARM_DWT_CYCCNT;
}

uint64_t SysTimer::cycleCnt64()
{
    return ARM_DWT_CYCCNT; // Note: Teensy doesn't support 64-bit cycle counter
}

void SysTimer::delayMilliseconds(unsigned x)
{
    ::delay(x);
}

void SysTimer::delayMicroseconds(unsigned x)
{
    ::delayMicroseconds(x);
}

}
