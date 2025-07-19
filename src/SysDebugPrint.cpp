#include "SysDebugPrint.h"

namespace SysPlatform {

std::mutex g_sysSerialLock;
volatile bool g_sysDebugPrintEnable = false;

void sysEnableDebugPrint() { g_sysDebugPrintEnable = true; }
void sysDisableDebugPrint() { g_sysDebugPrintEnable = false; };
bool isSysDebugPrintEnabled() { return g_sysDebugPrintEnable; }

}
