#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysCpuControl.h"
#include "sysPlatform/SysInitialize.h"

#include "SysIOMapping.h"

namespace SysPlatform {

static bool isInitialized = false;

int  sysInitialize() {

    // Ensure SPI CS are outputs and de-asserted
    digitalWrite(AVALON_SPI0_CS_PIN, HIGH);
    pinMode(AVALON_SPI0_CS_PIN, OUTPUT);

    digitalWrite(AVALON_SPI1_CS0_PIN, HIGH);
    pinMode(AVALON_SPI1_CS0_PIN, OUTPUT);

    digitalWrite(AVALON_SPI1_CS1_PIN, HIGH);
    pinMode(AVALON_SPI1_CS1_PIN, OUTPUT);

    isInitialized = true;
    return SYS_SUCCESS;
}

bool sysIsInitialized() { return isInitialized; }

void sysDeinitialize() {
    isInitialized = false;
    return;
}

void sysInitShowSummary()
{

}

} // end namespace SysPlatform
