#include <vector>
#include <cmath>
#include "TeensyThreads.h"
#include "sysPlatform/SysTypes.h"
#include "sysPlatform/SysThreads.h"

namespace SysPlatform {

SysThreads sysThreads;

struct SysThreads::_impl {

};

SysThreads::SysThreads()
: m_pimpl(nullptr)
{

}

SysThreads::~SysThreads()
{

}

int SysThreads::addThread(SysThreadFunction p, void * arg, int stack_size, void *stack)
{
    return threads.addThread(p, arg, stack_size, stack);
}

int SysThreads::getState(int id)
{
    int state =  threads.getState(id);
    switch(state) {
    case Threads::RUNNING   : return SysThreads::RUNNING;
    case Threads::SUSPENDED : return SysThreads::SUSPENDED;
    default : return SysThreads::NOT_STARTED;
    }
}

int SysThreads::setState(int id, int state) {
    switch(state) {
    case SysThreads::SUSPENDED : return threads.setState(id, Threads::SUSPENDED);
    case SysThreads::RUNNING   : return threads.setState(id, Threads::RUNNING);
    default :
        return threads.setState(id, Threads::SUSPENDED); 
    }    
}

int SysThreads::suspend(int id)
{
    return threads.suspend(id);
}

int SysThreads::restart(int id)
{
    return threads.restart(id);
}

int SysThreads::setTimeSlice(int id, unsigned ticks)
{
    threads.setTimeSlice(id, ticks);
    return SYS_SUCCESS;
}

int SysThreads::setTimeSliceUs(int id, unsigned microseconds)
{
    // TeensyThreads only supports millisecond slices
    int milliseconds = static_cast<int>(std::roundf(microseconds / 1000.f));
    if (milliseconds <= 0) { return SYS_FAILURE; }
    threads.setTimeSlice(id, milliseconds);
    return SYS_SUCCESS;
}

int SysThreads::id() {
    return threads.id();
}

void SysThreads::yield()
{
    threads.yield();
}

void SysThreads::delay(int milliseconds)
{
    threads.delay(milliseconds);
}

}