#include "Arduino.h"
#include "sysPlatform/SysSpi.h"
#include "sysPlatform/SysCpuControl.h"
#include "sysPlatform/SysLogger.h"

using namespace SysPlatform;

SysSpi sysSpi(true);

constexpr size_t BUF_SIZE = 1024U;
//uint8_t DMAMEM readBuf[BUF_SIZE];

void setup()
{
    uint8_t readBuf[BUF_SIZE];

    sysLogger.begin(115200);
    while(!Serial) {}

    sysLogger.printf("\n\n**** Starting SPI\n"); sysLogger.flush();
    sysSpi.begin();

    sysLogger.printf("Zeroing memory\n"); sysLogger.flush();    
    sysSpi.zero(0, BUF_SIZE);
    while (sysSpi.isWriteBusy()) { SysCpuControl::yield(); }
    
    sysLogger.printf("Reading back zeros\n"); sysLogger.flush();
    
    for (size_t i = 0; i < BUF_SIZE; i++) { readBuf[i] = i % 256; }
    sysSpi.read(0, readBuf, BUF_SIZE);
    while (sysSpi.isReadBusy()) { SysCpuControl::yield(); }

    sysLogger.printf("Validating zeros\n"); sysLogger.flush();
    unsigned errorCount = 0;
    unsigned correctCount = 0;
    for (size_t i=0; i<BUF_SIZE; i++) {
        if (readBuf[i] != 0) {
            errorCount++;
            sysLogger.printf("ERROR: value readBuf[%04X]=%02X is not zero\n", i, readBuf[i]);
        } else { correctCount++; }
        
        if (errorCount >= 16) { break; }
    }

    if (errorCount == 0) { sysLogger.printf("Zeros PASSED!\n"); }
    else { sysLogger.printf("Zeros FAILED! Correct:%d  Error:%d\n", correctCount, errorCount); while(true) { SysCpuControl::yield(); } }

    for (size_t i = 0; i < BUF_SIZE; i++) { readBuf[i] = i % 256; }
    sysLogger.printf("Programming addresses to memory\n"); sysLogger.flush();    
    sysSpi.write(0, readBuf, BUF_SIZE);
    while (sysSpi.isWriteBusy()) { SysCpuControl::yield(); }

    sysLogger.printf("Validating addresses\n"); sysLogger.flush();
    errorCount = 0;
    correctCount = 0;
    for (size_t i=0; i<BUF_SIZE; i++) {
        if (readBuf[i] != i % 256) {
            errorCount++;
            sysLogger.printf("ERROR: value readBuf[%04X]=%02X, expected %02X\n", i, readBuf[i], i % 256);
        } else { correctCount++; }
        
        if (errorCount >= 16) { break; }
    }

    if (errorCount == 0) { sysLogger.printf("Addresses PASSED!\n"); }
    else { sysLogger.printf("Addresses FAILED! Correct:%d  Error:%d\n", correctCount, errorCount); while(true) { SysCpuControl::yield(); } }


    sysLogger.printf("Test complete!\n");

}

void loop()
{

}
