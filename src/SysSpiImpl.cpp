#include <cassert>
#include "SysCpuControl.h"

#include "SysSpiImpl.h"

namespace SysPlatform {

DmaSpi0 DMASPI0;  // TODO remove this, but keep getting linker error
DmaSpi1 DMASPI1;

static void * dma_aligned_malloc(size_t align, size_t size);
static void dma_aligned_free(void * ptr);

SpiConfig::SpiConfig(SPIClass& _spiClass, unsigned _csPin, unsigned _sckPin,
            unsigned _misoPin, unsigned _mosiPin, unsigned _size,
            unsigned _boundary, unsigned _speedHz)
: spi(_spiClass), csPin(_csPin), sckPin(_sckPin), misoPin(_misoPin), mosiPin(_mosiPin),
  size(_size), boundary(_boundary), speedHz(_speedHz)  {}

SpiConfig::~SpiConfig() = default;

SysSpi::_impl::_impl(bool useDma)
: m_spiConfig(SPI1,
    AVALON_SPI1_CS1_PIN,
    AVALON_SPI1_SCK_PIN,
    AVALON_SPI1_MISO_PIN,
    AVALON_SPI1_MOSI_PIN,
    AVALON_SPI1_MEM_SIZE,
    0,  // no boundaries on STRIDE SPI PSRAM chip
    AVALON_SPI1_SPEED_HZ),
    m_useDma(useDma) {}

SysSpi::_impl::~_impl()
{
	if (m_cs) delete m_cs;
	if (m_txTransfer) delete [] m_txTransfer;
	if (m_rxTransfer) delete [] m_rxTransfer;
	if (m_txCommandBuffer) delete [] m_txCommandBuffer;
	if (m_rxCommandBuffer) delete [] m_rxCommandBuffer;
}

void SysSpi::_impl::m_setSpiCmdAddr(int command, size_t address, uint8_t *dest)
{
	dest[0] = command;
	dest[1] = ((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
	dest[2] = ((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
	dest[3] = ((address & SPI_ADDR_0_MASK));
}

size_t SysSpi::_impl::m_bytesToXfer(size_t address, size_t numBytes)
{
    // Check if this burst will cross the die boundary
    size_t bytesToXfer = numBytes;
    if (m_dieBoundary) {
        if ((address < m_dieBoundary) && (address+numBytes > m_dieBoundary)) {
            // split into two xfers
            bytesToXfer = m_dieBoundary-address;
        }
    }
    return bytesToXfer;
}

// Intitialize the correct Arduino SPI interface
void SysSpi::_impl::begin()
{
    m_settings = {m_spiConfig.speedHz, MSBFIRST, SPI_MODE0};

    m_csPin = m_spiConfig.csPin;
    m_spi   = &m_spiConfig.spi;

    m_spi->setMOSI(m_spiConfig.mosiPin);
    m_spi->setMISO(m_spiConfig.misoPin);
    m_spi->setSCK(m_spiConfig.sckPin);
    m_spi->begin();
    m_dieBoundary = m_spiConfig.boundary;

    pinMode(m_csPin, OUTPUT);
    digitalWrite(m_csPin, HIGH);

    if (m_useDma) {
        m_cs = new ActiveLowChipSelect1(m_spiConfig.csPin, m_settings);  // STRIDE uses SPI1

        // add 4 bytes to buffer for SPI CMD and 3 bytes of address
        m_txCommandBuffer = new uint8_t[CMD_ADDRESS_SIZE];
        m_rxCommandBuffer = new uint8_t[CMD_ADDRESS_SIZE];
        m_txTransfer = new DmaSpi::Transfer[2];
        m_rxTransfer = new DmaSpi::Transfer[2];

        m_spiDma = new DmaSpiGeneric(1);  // STRIDE uses SPI1

        m_spiDma->begin();
        m_spiDma->start();
    }

    m_started = true;
}

// Single address write
void SysSpi::_impl::write(size_t address, uint8_t data)
{
	m_spi->beginTransaction(m_settings);
	digitalWrite(m_csPin, LOW);
	m_spi->transfer(SPI_WRITE_CMD);
	m_spi->transfer((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
	m_spi->transfer((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
	m_spi->transfer((address & SPI_ADDR_0_MASK));
	m_spi->transfer(data);
	m_spi->endTransaction();
	digitalWrite(m_csPin, HIGH);
}

// SPI must build up a payload that starts the teh CMD/Address first. It will cycle
// through the payloads in a circular buffer and use the transfer objects to check if they
// are done before continuing.
void SysSpi::_impl::write(size_t address, uint8_t *src, size_t numBytes)
{
    if (!m_useDma) {
        // Check if this burst will cross the die boundary
        while (numBytes > 0) {
            size_t bytesToWrite = m_bytesToXfer(address, numBytes);
            m_rawWrite(address, src, bytesToWrite);
            address += bytesToWrite;
            numBytes -= bytesToWrite;
            src += bytesToWrite;
        }
        return;
    }

    // else DMA
	size_t bytesRemaining = numBytes;
	uint8_t *srcPtr = src;
	size_t nextAddress = address;
    uint8_t *intermediateBuffer = nullptr;

    while ( m_txTransfer[1].busy() || m_txTransfer[0].busy()) { SysCpuControl::yield(); } // wait until not busy

    // Check for intermediate buffer use
    if (m_dmaCopyBufferSize) {
        // copy to the intermediate buffer;
        intermediateBuffer = m_dmaWriteCopyBuffer;
        memcpy(intermediateBuffer, src, numBytes);
    }

	while (bytesRemaining > 0) {
	    m_txXferCount = m_bytesToXfer(nextAddress, min(bytesRemaining, static_cast<size_t>(MAX_DMA_XFER_SIZE))); // check for die boundary
		m_setSpiCmdAddr(SPI_WRITE_CMD, nextAddress, m_txCommandBuffer);
		m_txTransfer[1] = DmaSpi::Transfer(m_txCommandBuffer, CMD_ADDRESS_SIZE, nullptr, 0, m_cs, TransferType::NO_END_CS);
		m_spiDma->registerTransfer(m_txTransfer[1]);

		while ( m_txTransfer[0].busy() || m_txTransfer[1].busy()) { SysCpuControl::yield(); } // wait until not busy
		m_txTransfer[0] = DmaSpi::Transfer(srcPtr, m_txXferCount, nullptr, 0, m_cs, TransferType::NO_START_CS, intermediateBuffer, nullptr);
		m_spiDma->registerTransfer(m_txTransfer[0]);
		bytesRemaining -= m_txXferCount;
		srcPtr += m_txXferCount;
		nextAddress += m_txXferCount;
	}
}


void SysSpi::_impl::zero(size_t address, size_t numBytes)
{
    if (!m_useDma) {
        // Check if this burst will cross the die boundary
        while (numBytes > 0) {
            size_t bytesToWrite = m_bytesToXfer(address, numBytes);
            m_rawZero(address, bytesToWrite);
            address += bytesToWrite;
            numBytes -= bytesToWrite;
        }
        return;
    }

    // else DMA
	size_t bytesRemaining = numBytes;
	size_t nextAddress = address;

	/// TODO: Why can't the T4 zero the memory when a NULLPTR is passed? It seems to write a constant random value.
	/// Perhaps there is somewhere we can set a fill value?
#if defined(__IMXRT1062__)
	static uint8_t zeroBuffer[MAX_DMA_XFER_SIZE];
	memset(zeroBuffer, 0, MAX_DMA_XFER_SIZE);
#else
	uint8_t *zeroBuffer = nullptr;
#endif

	while (bytesRemaining > 0) {
	    m_txXferCount = m_bytesToXfer(nextAddress, min(bytesRemaining, static_cast<size_t>(MAX_DMA_XFER_SIZE))); // check for die boundary

		while ( m_txTransfer[1].busy()) { SysCpuControl::yield(); } // wait until not busy
		m_setSpiCmdAddr(SPI_WRITE_CMD, nextAddress, m_txCommandBuffer);
		m_txTransfer[1] = DmaSpi::Transfer(m_txCommandBuffer, CMD_ADDRESS_SIZE, nullptr, 0, m_cs, TransferType::NO_END_CS);
		m_spiDma->registerTransfer(m_txTransfer[1]);

		while ( m_txTransfer[0].busy()) { SysCpuControl::yield(); } // wait until not busy
		//m_txTransfer[0] = DmaSpi::Transfer(nullptr, m_txXferCount, nullptr, 0, m_cs, TransferType::NO_START_CS);
		m_txTransfer[0] = DmaSpi::Transfer(zeroBuffer, m_txXferCount, nullptr, 0, m_cs, TransferType::NO_START_CS);
		m_spiDma->registerTransfer(m_txTransfer[0]);
		bytesRemaining -= m_txXferCount;
		nextAddress += m_txXferCount;
	}
}

void SysSpi::_impl::write16(size_t address, uint16_t data)
{
	m_spi->beginTransaction(m_settings);
	digitalWrite(m_csPin, LOW);
	m_spi->transfer16((SPI_WRITE_CMD << 8) | (address >> 16) );
	m_spi->transfer16(address & 0xFFFF);
	m_spi->transfer16(data);
	m_spi->endTransaction();
	digitalWrite(m_csPin, HIGH);
}

void SysSpi::_impl::write16(size_t address, uint16_t *src, size_t numWords)
{
	write(address, reinterpret_cast<uint8_t*>(src), sizeof(uint16_t)*numWords);
}

void SysSpi::_impl::zero16(size_t address, size_t numWords)
{
	zero(address, sizeof(uint16_t)*numWords);
}

// single address read
uint8_t SysSpi::_impl::read(size_t address)
{
	int data;

	m_spi->beginTransaction(m_settings);
	digitalWrite(m_csPin, LOW);
	m_spi->transfer(SPI_READ_CMD);
	m_spi->transfer((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
	m_spi->transfer((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
	m_spi->transfer((address & SPI_ADDR_0_MASK));
	data = m_spi->transfer(0);
	m_spi->endTransaction();
	digitalWrite(m_csPin, HIGH);
	return data;
}

void SysSpi::_impl::read(size_t address, uint8_t *dest, size_t numBytes)
{
    if (!m_useDma) {
        // Check if this burst will cross the die boundary
        while (numBytes > 0) {
            size_t bytesToRead = m_bytesToXfer(address, numBytes);
            m_rawRead(address, dest, bytesToRead);
            address += bytesToRead;
            numBytes -= bytesToRead;
            dest += bytesToRead;
        }
        return;
    }

    // else DMA
	size_t bytesRemaining = numBytes;
	uint8_t *destPtr = dest;
	size_t nextAddress = address;
	volatile uint8_t *intermediateBuffer = nullptr;

	// Check for intermediate buffer use
	if (m_dmaCopyBufferSize) {
	    intermediateBuffer = m_dmaReadCopyBuffer;
	}

	while (bytesRemaining > 0) {

	    while ( m_rxTransfer[1].busy() || m_rxTransfer[0].busy()) { SysCpuControl::yield(); }
	    m_rxXferCount = m_bytesToXfer(nextAddress, min(bytesRemaining, static_cast<size_t>(MAX_DMA_XFER_SIZE))); // check for die boundary
		m_setSpiCmdAddr(SPI_READ_CMD, nextAddress, m_rxCommandBuffer);
		m_rxTransfer[1] = DmaSpi::Transfer(m_rxCommandBuffer, CMD_ADDRESS_SIZE, nullptr, 0, m_cs, TransferType::NO_END_CS);
		m_spiDma->registerTransfer(m_rxTransfer[1]);

		while ( m_rxTransfer[0].busy() || m_rxTransfer[1].busy()) { SysCpuControl::yield(); }
		m_rxTransfer[0] = DmaSpi::Transfer(nullptr, m_rxXferCount, destPtr, 0, m_cs, TransferType::NO_START_CS, nullptr, intermediateBuffer);
		m_spiDma->registerTransfer(m_rxTransfer[0]);

		bytesRemaining -= m_rxXferCount;
		destPtr += m_rxXferCount;
		nextAddress += m_rxXferCount;
	}
}

uint16_t SysSpi::_impl::read16(size_t address)
{
	uint16_t data;
	m_spi->beginTransaction(m_settings);
	digitalWrite(m_csPin, LOW);
	m_spi->transfer16((SPI_READ_CMD << 8) | (address >> 16) );
	m_spi->transfer16(address & 0xFFFF);
	data = m_spi->transfer16(0);
	m_spi->endTransaction();

	digitalWrite(m_csPin, HIGH);
	return data;
}

void SysSpi::_impl::read16(size_t address, uint16_t *dest, size_t numWords)
{
	read(address, reinterpret_cast<uint8_t*>(dest), sizeof(uint16_t)*numWords);
}

void SysSpi::_impl::m_rawWrite(size_t address, uint8_t *src, size_t numBytes)
{
    uint8_t *dataPtr = src;

    m_spi->beginTransaction(m_settings);
    digitalWrite(m_csPin, LOW);
    m_spi->transfer(SPI_WRITE_CMD);
    m_spi->transfer((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
    m_spi->transfer((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
    m_spi->transfer((address & SPI_ADDR_0_MASK));

    for (size_t i=0; i < numBytes; i++) {
        m_spi->transfer(*dataPtr++);
    }
    m_spi->endTransaction();
    digitalWrite(m_csPin, HIGH);
}

void SysSpi::_impl::m_rawZero(size_t address, size_t numBytes)
{
    m_spi->beginTransaction(m_settings);
    digitalWrite(m_csPin, LOW);
    m_spi->transfer(SPI_WRITE_CMD);
    m_spi->transfer((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
    m_spi->transfer((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
    m_spi->transfer((address & SPI_ADDR_0_MASK));

    for (size_t i=0; i < numBytes; i++) {
        m_spi->transfer(0);
    }
    m_spi->endTransaction();
    digitalWrite(m_csPin, HIGH);
}

void SysSpi::_impl::m_rawRead(size_t address, uint8_t *dest, size_t numBytes)
{
    uint8_t *dataPtr = dest;

    m_spi->beginTransaction(m_settings);
    digitalWrite(m_csPin, LOW);
    m_spi->transfer(SPI_READ_CMD);
    m_spi->transfer((address & SPI_ADDR_2_MASK) >> SPI_ADDR_2_SHIFT);
    m_spi->transfer((address & SPI_ADDR_1_MASK) >> SPI_ADDR_1_SHIFT);
    m_spi->transfer((address & SPI_ADDR_0_MASK));

    for (size_t i=0; i<numBytes; i++) {
        *dataPtr++ = m_spi->transfer(0);
    }

    m_spi->endTransaction();
    digitalWrite(m_csPin, HIGH);
}

bool SysSpi::_impl::isWriteBusy(void) const
{
	return (m_txTransfer[0].busy() or m_txTransfer[1].busy());
}

bool SysSpi::_impl::isReadBusy(void) const
{
	return (m_rxTransfer[0].busy() or m_rxTransfer[1].busy());
}

void SysSpi::_impl::stop(bool waitForStop)
{
    if (m_spiDma) {
        //m_halted = true;
        while (isWriteBusy() || isReadBusy()) {}
        m_spiDma->stop();

        if (waitForStop) {
            while (!m_spiDma->stopped()) {}
        }
    }
}

bool SysSpi::_impl::isStopped() const
{
    if (!m_spiDma) { return true; }
    return m_spiDma->stopped();
}

void SysSpi::_impl::start(bool waitForStart)
{
    if (m_spiDma) {
        m_spiDma->start();

        if (waitForStart) {
            while (!m_spiDma->running()) {}
        }
    }
}

bool SysSpi::_impl::isStarted() const
{
    if (!m_spiDma) { return false; }
    return m_spiDma->running();
}

bool SysSpi::_impl::setDmaCopyBufferSize(size_t numBytes)
{
    if (m_dmaWriteCopyBuffer) {
        dma_aligned_free((void *)m_dmaWriteCopyBuffer);
        m_dmaCopyBufferSize = 0;
    }
    if (m_dmaReadCopyBuffer) {
        dma_aligned_free((void *)m_dmaReadCopyBuffer);
        m_dmaCopyBufferSize = 0;
    }

    if (numBytes > 0) {
        m_dmaWriteCopyBuffer = (uint8_t*)dma_aligned_malloc(MEM_ALIGNED_ALLOC, numBytes);
        if (!m_dmaWriteCopyBuffer) {
            // allocate failed
            m_dmaCopyBufferSize = 0;
            return false;
        }

        m_dmaReadCopyBuffer = (volatile uint8_t*)dma_aligned_malloc(MEM_ALIGNED_ALLOC, numBytes);
        if (!m_dmaReadCopyBuffer) {
            // allocate failed
            m_dmaCopyBufferSize = 0;
            return false;
        }
        m_dmaCopyBufferSize = numBytes;
    }

    return true;
}

size_t SysSpi::_impl::getDmaCopyBufferSize(void) { return m_dmaCopyBufferSize; }

// Some convenience functions to malloc and free aligned memory
// Number of bytes we're using for storing
// the aligned pointer offset
typedef uint16_t offset_t;
#define PTR_OFFSET_SZ sizeof(offset_t)

#ifndef align_up
#define align_up(num, align) \
    (((num) + ((align) - 1)) & ~((align) - 1))
#endif

void * dma_aligned_malloc(size_t align, size_t size)
{
    void * ptr = NULL;

    // We want it to be a power of two since
    // align_up operates on powers of two
    assert((align & (align - 1)) == 0);

    if(align && size)
    {
        /*
         * We know we have to fit an offset value
         * We also allocate extra bytes to ensure we
         * can meet the alignment
         */
        uint32_t hdr_size = PTR_OFFSET_SZ + (align - 1);
        void * p = malloc(size + hdr_size);

        if(p)
        {
            /*
             * Add the offset size to malloc's pointer
             * (we will always store that)
             * Then align the resulting value to the
             * target alignment
             */
            ptr = (void *) align_up(((uintptr_t)p + PTR_OFFSET_SZ), align);

            // Calculate the offset and store it
            // behind our aligned pointer
            *((offset_t *)ptr - 1) =
                (offset_t)((uintptr_t)ptr - (uintptr_t)p);

        } // else NULL, could not malloc
    } //else NULL, invalid arguments

    return ptr;
}

void dma_aligned_free(void * ptr)
{
    assert(ptr);

    /*
    * Walk backwards from the passed-in pointer
    * to get the pointer offset. We convert to an offset_t
    * pointer and rely on pointer math to get the data
    */
    offset_t offset = *((offset_t *)ptr - 1);

    /*
    * Once we have the offset, we can get our
    * original pointer and call free
    */
    void * p = (void *)((uint8_t *)ptr - offset);
    free(p);
}

}
