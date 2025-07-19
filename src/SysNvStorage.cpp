#include <cstdlib>
#include <EEPROM.h>
#include <sysPlatform/SysNvStorage.h>

namespace SysPlatform {

const size_t NVSTORAGE_SIZE_BYTES = E2END;
NvStorage sysNvStorage;

NvStorage::NvStorage()
{

}

NvStorage::~NvStorage()
{

}

uint8_t NvStorage::read( int idx ) {
    return EEPROM.read(idx);
}

void NvStorage::write( int idx, uint8_t val ) {
    EEPROM.write(idx, val);
}

void NvStorage::update( int idx, uint8_t in) {
    EEPROM.update(idx, in);
}

void NvStorage::flush()
{

}

}
