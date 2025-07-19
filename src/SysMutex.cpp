#include "TeensyThreads.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysMutex.h"

#if 1
#elif defined(SYSPLATFORM_STD_MUTEX)
namespace std {

Mutex::mutex() {
    mxPtr = new Threads::Mutex;
}
Mutex::~mutex() {
    if (mxPtr) { delete (Threads::Mutex*)mxPtr; }
}

void Mutex::lock() { (Threads::Mutex*)->lock(); }
void Mutex::unlock() { (Threads::Mutex*)->unlock(); }

}

#endif
