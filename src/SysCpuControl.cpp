#include "Arduino.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysCpuControl.h"
#include "sysPlatform/SysWatchdog.h"

namespace SysPlatform
{

    int SysCpuControl::halt(void)
    {
        while (true)
        {
        } // infinite loop
        return SYS_SUCCESS;
    }

    int SysCpuControl::reboot(void)
    {
        // send reboot command -----
        SCB_AIRCR = 0x05FA0004;
        return SYS_SUCCESS;
    }

    void SysCpuControl::yield(void)
    {
        ::yield();
    }

    void SysCpuControl::disableIrqs()
    {
        __disable_irq();
    }

    void SysCpuControl::enableIrqs()
    {
        __enable_irq();
    }

    void SysCpuControl::AudioNoInterrupts()
    {
        NVIC_DISABLE_IRQ(IRQ_SOFTWARE);
    }

    void SysCpuControl::AudioInterrupts()
    {
        NVIC_ENABLE_IRQ(IRQ_SOFTWARE);
    }

    void SysCpuControl::AudioTriggerInterrupt()
    {
        NVIC_SET_PENDING(IRQ_SOFTWARE);
    }

    void SysCpuControl::AudioSetInterruptPriority(int priority)
    {
        NVIC_SET_PRIORITY(IRQ_SOFTWARE, priority); // 255 = lowest priority
    }

    void SysCpuControl::AudioAttachInterruptVector(void (*function)(void))
    {
        attachInterruptVector(IRQ_SOFTWARE, function);
    }

    void SysCpuControl::SysDataSyncBarrier()
    {
        asm("DSB");
    }

    // This function will powerdown the USB port entirely. A reboot is necessary to restore it.
    // The powerdown bits are from the i.MX RT1060 Processor Reference Manual. Chapter 42, USB-PHY,
    // Table 42.4.1
    void SysCpuControl::shutdownUsb()
    {
        // USBPHY1_PWD = 0x001E1C00U;  // this works but seems to lock up the host app using the USB port
        USB1_USBCMD = 0;  // this seems to be a more gentle pseudo-disconnect
        delay(30);
    }

#define WDOG1_WCR_ADDRESS 0x400B8000U // 16-bit register!
#define WCR_DEBUG_ENABLE_MASK (0x1U << 1)
#define WCR_MAX_TIME_MASK 0xFF00U;
    void SysCpuControl::disableWdt()
    {
        // if (Serial) { Serial.printf("WDOG DEBUG\n"); Serial.flush(); Serial.printf("WDOG1_WCR: 0x%04X\n", WDOG1_WCR); Serial.flush(); }
        // WDOG1_WCR |= WCR_DEBUG_ENABLE_MASK;
        WDOG1_WCR |= WCR_MAX_TIME_MASK;
        // if (Serial) { Serial.printf("WDOG1_WCR: 0x%04X\n", WDOG1_WCR); Serial.flush(); }
        //  volatile uint16_t* wcrPtr = (uint16_t*)WDOG1_WCR_ADDRESS;
        //  if (Serial) { Serial.printf("WDOG DEBUG\n"); Serial.flush(); Serial.printf("wcrPtr: 0x%04X\n", *wcrPtr); Serial.flush(); }
        //  *wcrPtr |= WCR_DEBUG_ENABLE_MASK;  // suspend the WDT
        //  if (Serial) { Serial.printf("wcrPtr: 0x%04X\n", *wcrPtr); Serial.flush(); }
    }

    void SysCpuControl::enableWdt()
    {
        sysWatchdog.reset();
        sysWatchdog.begin(0.500f);  // half second
    }

}
