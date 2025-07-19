#include <imxrt.h>
#include <core_pins.h>
#include <Arduino.h>
#include "sysPlatform/SysDebugPrint.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysCpuTelemetry.h"

// from the linker
extern unsigned long _stext;
extern unsigned long _etext;
extern unsigned long _sdata;
extern unsigned long _edata;
extern unsigned long _sbss;
extern unsigned long _ebss;
extern unsigned long _estack;
extern unsigned long _heap_start;
extern unsigned long _heap_end;
extern unsigned long _bss_dma_start;
extern unsigned long _bss_dma_end;

extern unsigned long _ram0_length;
extern unsigned long _ram0_end;
extern unsigned long _ram1_length;
extern unsigned long _ram1_start;

extern char *__brkval;  // from linker script
extern float tempmonGetTemp(void);   // from cores/teensy4/tempmon.c

namespace SysPlatform {

unsigned SysCpuTelemetry::getCpuFreqHz()
{
    return (unsigned)F_CPU_ACTUAL;
}
float SysCpuTelemetry::getTemperatureCelcius()
{
    return tempmonGetTemp();
}

void   SysCpuTelemetry::markUnusedStack()
{
#if defined(__IMXRT1062__)
  uint32_t flexram_config = IOMUXC_GPR_GPR17;
  uint32_t dtcm_size = 0;
  uint32_t itcm_size = 0;
  for (; flexram_config; flexram_config >>= 2) {
    if ((flexram_config & 0x3) == 0x2) { dtcm_size += 32768; }
    else if ((flexram_config & 0x3) == 0x3) { itcm_size += 32768; }
  }
  // Guess of where it is safe to fill memory... Maybe address of last variable we have defined - some slop...
  for (uint32_t *pfill = (uint32_t*)(((uint32_t)&_ebss + (uint32_t)32)); pfill < (&itcm_size - (uint32_t)10); pfill++) {
    *pfill = 0x01020304;  // some random value
  }
#endif
}

size_t SysCpuTelemetry::getStackSizeBytes()
{
    size_t stackSize = (uint32_t)(&_estack) - ((uint32_t)&_ebss + 1);
    return stackSize;
}

size_t SysCpuTelemetry::getStackUsedBytes()
{
#if defined(__IMXRT1062__)
  uint32_t *pmem = (&_ebss + 32);
  while (*pmem == 0x01020304) pmem++;
  return ((uint32_t)&_estack - (uint32_t)pmem);
  //Serial.printf("Estimated max stack usage: 0x%08X\n", (uint32_t)&_estack - (uint32_t)pmem);
#endif
}

size_t SysCpuTelemetry::getStackFreeBytes()
{
    return (getStackSizeBytes() - getStackUsedBytes());
}

float  SysCpuTelemetry::getStackUsageRatio()
{
#if defined(__IMXRT1062__)
  uint32_t *pmem = (&_ebss + 32);
  while (*pmem == 0x01020304) pmem++;
  uint32_t stackUsed = ((uint32_t)&_estack  - (uint32_t)pmem);
  uint32_t stackSize = ((uint32_t)&_estack) - ((uint32_t)&_ebss + 1);
  return float(stackUsed) / float(stackSize);
#else
  return 0.0f;
#endif
}

size_t SysCpuTelemetry::getHeapSizeBytes()
{
    return (uint32_t)&_heap_end - (uint32_t)&_heap_start;
}

size_t SysCpuTelemetry::getHeapUsedBytes()
{
    return (uint32_t)__brkval - (uint32_t)&_heap_start;
}

size_t SysCpuTelemetry::getHeapFreeBytes()
{
    return (uint32_t)&_heap_end - (uint32_t)__brkval;
}

float  SysCpuTelemetry::getHeapUsageRatio()
{
    uint32_t heapSize = (uint32_t)&_heap_end - (uint32_t)&_heap_start;
    return float(getHeapUsedBytes()) / float(heapSize);
}

size_t SysCpuTelemetry::getRam0Size()
{
    return (uint32_t)&_ram0_length;
}

float SysCpuTelemetry::getRam0UsageRatio()
{
    size_t freeBytes = getStackFreeBytes();
    size_t usedBytes = getRam0Size() - freeBytes;
    return (float)(usedBytes) / (float)getRam0Size();
}

size_t SysCpuTelemetry::getRam1Size()
{
    return (uint32_t)&_ram1_length;
}

float SysCpuTelemetry::getRam1UsageRatio()
{
    size_t usedBytes = getRam1Size() - getHeapFreeBytes();
    return (float)usedBytes / (float)getRam1Size();
}

void SysCpuTelemetry::debugShowMemoryConfig() {
  uint32_t flexram_config = IOMUXC_GPR_GPR17;
  SYS_DEBUG_PRINT(Serial.printf("IOMUXC_GPR_GPR17:0x%08X IOMUXC_GPR_GPR16:0x%08X IOMUXC_GPR_GPR14:0x%08X\n",
                flexram_config, IOMUXC_GPR_GPR16, IOMUXC_GPR_GPR14));
  SYS_DEBUG_PRINT(Serial.printf("Initial Stack pointer: 0x%08X\n", &_estack));
  uint32_t stackSize = (uint32_t)(&_estack) - ((uint32_t)&_ebss + 1);
  uint32_t dtcm_size = 0;
  uint32_t itcm_size = 0;
  for (; flexram_config; flexram_config >>= 2) {
    if ((flexram_config & 0x3) == 0x2) { dtcm_size += 32768; }
    else if ((flexram_config & 0x3) == 0x3) { itcm_size += 32768; }
  }

  UNUSED(stackSize);  // supress warning when printf is disabled

  SYS_DEBUG_PRINT(Serial.printf("ITCM allocated: %u bytes, blocks:%u  DTCM allocated: %u bytes, blocks:%u\n", itcm_size, itcm_size / 32768, dtcm_size, dtcm_size / 32768 ));
  SYS_DEBUG_PRINT(Serial.printf("ITCM init range: 0x%08X - 0x%08X Count: %u bytes\n", &_stext, &_etext, (uint32_t)&_etext - (uint32_t)&_stext));
  SYS_DEBUG_PRINT(Serial.printf("DTCM init range: 0x%08X - 0x%08X Count: %u bytes\n", &_sdata, &_edata, (uint32_t)&_edata - (uint32_t)&_sdata));
  SYS_DEBUG_PRINT(Serial.printf("DTCM cleared range: 0x%08X - 0x%08X Count: %u\n", &_sbss, &_ebss, (uint32_t)&_ebss - (uint32_t)&_sbss));
  SYS_DEBUG_PRINT(Serial.printf("Linker sections:\n"));
  SYS_DEBUG_PRINT(Serial.printf("    FLEXRAM0: .text  (0x%08X-0x%08X)  %d bytes\n", &_stext, &_etext, (uint32_t)&_etext - (uint32_t)&_stext));
  SYS_DEBUG_PRINT(Serial.printf("            : .data  (0x%08X-0x%08X)  %d bytes\n", &_sdata, &_edata, (uint32_t)&_edata - (uint32_t)&_sdata));
  SYS_DEBUG_PRINT(Serial.printf("            : .bss   (0x%08X-0x%08X)  %d bytes\n", &_sbss, &_ebss,   (uint32_t)&_ebss - (uint32_t)&_sbss));
  SYS_DEBUG_PRINT(Serial.printf("            :  stack (0x%08X-0x%08X)  %d bytes\n", (uint32_t)&_ebss+1, &_estack, stackSize));
  SYS_DEBUG_PRINT(Serial.printf("        RAM1: .dma   (0x%08X-0x%08X)  %d bytes\n", &_bss_dma_start, &_bss_dma_end, (uint32_t)&_bss_dma_end - (uint32_t)&_bss_dma_start + 1));
  SYS_DEBUG_PRINT(Serial.printf("        RAM1: .heap  (0x%08X-0x%08X)  %d bytes\n", &_heap_start, &_heap_end, (uint32_t)&_heap_end - (uint32_t)&_heap_start));
  SYS_DEBUG_PRINT(Serial.printf("Area of DTCM with known pattern(0x%08X to 0x%08X)\n", ((uint32_t)&_ebss + 1), ((uint32_t)&itcm_size - 10)); Serial.flush()); //
  SYS_DEBUG_PRINT(Serial.printf("Stack size is (0x%08X-0x%08X) = %lu bytes\n", &_estack, (uint32_t)&_ebss+1, stackSize));
  SYS_DEBUG_PRINT(Serial.printf("Heap size is  (0x%08X-0x%08X) = %lu bytes\n", &_heap_start, &_heap_end, (uint32_t)&_heap_end - (uint32_t)&_heap_start));
}

}
