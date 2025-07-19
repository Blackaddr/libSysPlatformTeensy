#include "Arduino.h"
#include "SPI.h"
#include "SysSpi.h"

#include "SysSpiImpl.h"

namespace SysPlatform {

const size_t SYS_SPI_MEM_SIZE = AVALON_SPI1_MEM_SIZE;

SysSpi::SysSpi(bool useDma)
: m_useDma(useDma), m_pimpl(std::make_unique<_impl>(useDma))
{

}

SysSpi::~SysSpi()
{

}

void SysSpi::begin() { m_pimpl->begin(); }

void SysSpi::write(size_t address, uint8_t data) { return m_pimpl->write(address, data); }

void SysSpi::write(size_t address, uint8_t *src, size_t numBytes) { m_pimpl->write(address, src, numBytes); }

void SysSpi::zero(size_t address, size_t numBytes) { m_pimpl->zero(address, numBytes); }

void SysSpi::write16(size_t address, uint16_t data) { return m_pimpl->write16(address, data); }

void SysSpi::write16(size_t address, uint16_t *src, size_t numWords) { m_pimpl->write16(address, src, numWords); }

void SysSpi::zero16(size_t address, size_t numWords) { m_pimpl->zero16(address, numWords); }

uint8_t SysSpi::read(size_t address) { return m_pimpl->read(address); }

void SysSpi::read(size_t address, uint8_t *dest, size_t numBytes) { m_pimpl->read(address, dest, numBytes); }

uint16_t SysSpi::read16(size_t address) { return m_pimpl->read16(address); }

void SysSpi::read16(size_t address, uint16_t *dest, size_t numWords) { m_pimpl->read16(address, dest, numWords); }

bool SysSpi::isWriteBusy() const { return m_pimpl->isWriteBusy(); }

bool SysSpi::isReadBusy() const { return m_pimpl->isReadBusy(); }

void SysSpi::readBufferContents(uint8_t *dest,  size_t numBytes, size_t byteOffset) { m_pimpl->readBufferContents(dest, numBytes, byteOffset); }

void SysSpi::readBufferContents(uint16_t *dest, size_t numWords, size_t wordOffset) { m_pimpl->readBufferContents(dest, numWords, wordOffset); }

bool SysSpi::setDmaCopyBufferSize(size_t numBytes) { return m_pimpl->setDmaCopyBufferSize(numBytes); }

size_t SysSpi::getDmaCopyBufferSize(void) { return m_pimpl->getDmaCopyBufferSize(); }

bool SysSpi::isStarted() const { return m_pimpl->isStarted(); }

bool SysSpi::isStopped() const { return m_pimpl->isStopped(); }

void SysSpi::stop(bool waitForStop) { return m_pimpl->stop(waitForStop); }
void SysSpi::start(bool waitForStart) { return m_pimpl->start(waitForStart); }

void SysSpi::beginTransaction(SysSpiSettings settings)
{
    //SPI1.beginTransaction(settings);
}

uint8_t SysSpi::transfer(uint8_t data)
{
    return SPI1.transfer(data);
}

uint16_t SysSpi::transfer16(uint16_t data)
{
    return SPI1.transfer(data);
}

void SysSpi::transfer(void *buf, size_t count)
{
    return SPI1.transfer(buf, count);
}

void SysSpi::endTransaction(void)
{
    SPI1.endTransaction();
}


}
