#pragma once

#include "Arduino.h"
#include "SPI.h"
#include "DmaSpi.h"

#include "SysSpi.h"

namespace SysPlatform {

// SPI Constants
constexpr int SPI_WRITE_MODE_REG = 0x1;
constexpr int SPI_WRITE_CMD      = 0x2;
constexpr int SPI_READ_CMD       = 0x3;
constexpr int SPI_ADDR_2_MASK    = 0xFF0000;
constexpr int SPI_ADDR_2_SHIFT   = 16;
constexpr int SPI_ADDR_1_MASK    = 0x00FF00;
constexpr int SPI_ADDR_1_SHIFT   = 8;
constexpr int SPI_ADDR_0_MASK    = 0x0000FF;

constexpr int CMD_ADDRESS_SIZE  = 4;
constexpr int MAX_DMA_XFER_SIZE = 0x400;

constexpr size_t MEM_ALIGNED_ALLOC = 32; // number of bytes to align DMA buffer to

constexpr uint8_t AVALON_SPI1_CS0_PIN    = 38; // SPI Flash, changes to 38 in TEENSYDUINO 1.56, was 39 in earlier TEENSYDUINO
constexpr uint8_t AVALON_SPI1_CS1_PIN    = 43; // SRAM
constexpr uint8_t AVALON_SPI1_SCK_PIN    = 27;
constexpr uint8_t AVALON_SPI1_MISO_PIN   = 1;
constexpr uint8_t AVALON_SPI1_MOSI_PIN   = 26;

constexpr size_t AVALON_SPI1_SPEED_HZ = 33333333;
constexpr size_t AVALON_SPI1_MEM_SIZE = 8*1024*1024;  // 64 MBit, 8 MByte

/******************************************************************************
 * SPI Memory Definitions
 *****************************************************************************/
// stores the SPI configuration for a device
struct SpiConfig {
    SpiConfig() = delete;
    SpiConfig(SPIClass& spiClass, unsigned csPin, unsigned sckPin,
              unsigned misoPin, unsigned mosiPin, unsigned size,
              unsigned boundary, unsigned speedHz);

    virtual ~SpiConfig();

    SPIClass& spi;
    unsigned csPin;
    unsigned sckPin;
    unsigned misoPin;
    unsigned mosiPin;
    unsigned size;
    unsigned boundary;
    unsigned speedHz;
};

struct SysSpi::_impl {

    _impl() = delete;
	_impl(bool useDma);
    ~_impl();

    // primary functions
    /// initialize and configure the SPI peripheral
	void begin();

	/// write a single 8-bit word to the specified address
	/// @param address the address in the SPI RAM to write to
	/// @param data the value to write
	void write(size_t address, uint8_t data);

	/// Write a block of 8-bit data to the specified address. Be check
    /// isWriteBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to write to
	/// @param src pointer to the source data block
	/// @param numBytes size of the data block in bytes
	void write(size_t address, uint8_t *src, size_t numBytes);

	/// Write a block of zeros to the specified address. Be check
    /// isWriteBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to write to
	/// @param numBytes size of the data block in bytes
	void zero(size_t address, size_t numBytes);

	/// write a single 16-bit word to the specified address
	/// @param address the address in the SPI RAM to write to
	/// @param data the value to write
	void write16(size_t address, uint16_t data);

	/// Write a block of 16-bit data to the specified address. Be check
	/// isWriteBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to write to
	/// @param src pointer to the source data block
	/// @param numWords size of the data block in 16-bit words
	void write16(size_t address, uint16_t *src, size_t numWords);

	/// Write a block of 16-bit zeros to the specified address. Be check
    /// isWriteBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to write to
	/// @param numWords size of the data block in 16-bit words
	void zero16(size_t address, size_t numWords);

	/// read a single 8-bit data word from the specified address
	/// @param address the address in the SPI RAM to read from
	/// @return the data that was read
	uint8_t read(size_t address);

	/// Read a block of 8-bit data from the specified address. Be check
    /// isReadBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to write to
	/// @param dest pointer to the destination
	/// @param numBytes size of the data block in bytes
	void read(size_t address, uint8_t *dest, size_t numBytes);

	/// read a single 16-bit data word from the specified address
	/// @param address the address in the SPI RAM to read from
	/// @return the data that was read
	uint16_t read16(size_t address);

	/// read a block 16-bit data word from the specified address. Be check
    /// isReadBusy() before sending the next DMA transfer.
	/// @param address the address in the SPI RAM to read from
	/// @param dest the pointer to the destination
	/// @param numWords the number of 16-bit words to transfer
	void read16(size_t address, uint16_t *dest, size_t numWords);

	/// Check if a DMA write is in progress
	/// @returns true if a write DMA is in progress, else false
	bool isWriteBusy() const;

	/// Check if a DMA read is in progress
	/// @returns true if a read DMA is in progress, else false
	bool isReadBusy() const;

	/// Readout the 8-bit contents of the DMA storage buffer to the specified destination
	/// @param dest pointer to the destination
	/// @param numBytes number of bytes to read out
	/// @param byteOffset, offset from the start of the DMA buffer in bytes to begin reading
	void readBufferContents(uint8_t *dest,  size_t numBytes, size_t byteOffset = 0);

	/// Readout the 8-bit contents of the DMA storage buffer to the specified destination
	/// @param dest pointer to the destination
	/// @param numWords number of 16-bit words to read out
	/// @param wordOffset, offset from the start of the DMA buffer in words to begin reading
	void readBufferContents(uint16_t *dest, size_t numWords, size_t wordOffset = 0);

	/// Creates and allocates an intermediate copy buffer that is suitable for DMA transfers. It is up to the
	/// user to ensure they never request a read/write larger than the size of this buffer when using this
	/// feature.
	/// @details In some use cases you may want to DMA to/from memory buffers that are in memory regions that
	/// are not directly usable for DMA. Specifying a non-zero copy buffer size will create an intermediate
	/// DMA-compatible buffer. By default, the size is zero and an intermediate copy is not performed.
	/// DMA requires the user data be in a DMA accessible region and that it be aligned to the
	/// the size of a cache line, and that the cache line isn't shared with any other data that might
	/// be used on a different thread. Best practice is for a DMA buffer to start on a cache-line
	/// boundary and be exactly sized to an integer multiple of cache lines.
	/// @param numBytes the number of bytes to allocate for the intermediate copy buffer.
	/// @returns true on success, false on failure
	bool setDmaCopyBufferSize(size_t numBytes);

	/// get the current size of the DMA copy buffer. Zero size means no intermediate copy is performed.
	/// @returns the size of the intermediate copy buffer in bytes.
	size_t getDmaCopyBufferSize(void);

	void stop(bool waitForStop = false);
	void start(bool waitForStart = false);
	bool isStopped() const;
    bool isStarted() const;

	SPIClass *m_spi = nullptr;
	SpiConfig m_spiConfig;
	bool m_useDma = false;

	uint8_t m_csPin; // the IO pin number for the CS on the controlled SPI device
	SPISettings m_settings; // the Wire settings for this SPI port
	bool m_started = false;
	size_t m_dieBoundary; // the address at which a SPI memory die rollsover

    DmaSpiGeneric      *m_spiDma = nullptr;
	AbstractChipSelect *m_cs     = nullptr;

	uint8_t          *m_txCommandBuffer = nullptr;
	DmaSpi::Transfer *m_txTransfer      = nullptr;
	uint8_t          *m_rxCommandBuffer = nullptr;
	DmaSpi::Transfer *m_rxTransfer      = nullptr;

	uint16_t m_txXferCount;
	uint16_t m_rxXferCount;

	size_t  m_dmaCopyBufferSize  = 0;
	uint8_t   *m_dmaWriteCopyBuffer = nullptr;
	volatile uint8_t   *m_dmaReadCopyBuffer  = nullptr;

	bool m_halted = false;

    size_t m_bytesToXfer(size_t address, size_t numBytes);
	void   m_setSpiCmdAddr(int command, size_t address, uint8_t *dest);
	void   m_rawWrite  (size_t address, uint8_t *src, size_t numBytes); // raw function for writing bytes
	void   m_rawZero   (size_t address, size_t numBytes);                // raw function for zeroing memory
	void   m_rawRead   (size_t address, uint8_t *dest, size_t numBytes); // raw function for reading bytes
};

}
