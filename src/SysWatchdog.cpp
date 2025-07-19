#include "Watchdog_t4.h"
#include "sysPlatform/SysWatchdog.h"

#define INCLUDE_OUTPUT_WATCHDOG

// No watchdog in debug mode
#if defined(INCLUDE_OUTPUT_WATCHDOG) && (defined(DEBUG) || defined(JUCE_DEBUG))
#undef INCLUDE_OUTPUT_WATCHDOG
#endif

namespace SysPlatform {

SysWatchdog sysWatchdog;

#if defined(INCLUDE_OUTPUT_WATCHDOG)

static WDT_timings_t wdtConfig;
static WDT_T4<WDT1>  wdt;

SysWatchdog::SysWatchdog()
{
    reset();
    wdtConfig.timeout = 0.500f; /* in seconds, 0->128 */
}

SysWatchdog::~SysWatchdog()
{

}

void SysWatchdog::begin(float seconds)
{
    wdtConfig.timeout = seconds; /* in seconds, 0->128 */
    wdt.begin(wdtConfig);
    m_isStarted = true;
}

bool SysWatchdog::isStarted()
{
    return m_isStarted;
}

void SysWatchdog::feed()
{
    wdt.feed();
}

void SysWatchdog::longFeed()
{
    // Feed the watchdog by writing the required sequence
    WDOG1_WSR = 0x5555;
    WDOG1_WSR = 0xAAAA;
}

void SysWatchdog::reset()
{
    WDOG1_WCR |= 0x0A00;  // set to 5 seconds initially to give time to boot
    longFeed();
}

#else

SysWatchdog::SysWatchdog()
{

}

SysWatchdog::~SysWatchdog()
{

}

void SysWatchdog::begin(float seconds)
{

}


void SysWatchdog::feed()
{

}

void SysWatchdog::longFeed()
{

}

void SysWatchdog::reset()
{

}
#endif


}