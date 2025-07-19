#include <cmath>
#include "Arduino.h"
#include <Wire.h>
#include "DMAChannel.h"
#include "memcpy_audio.h"
#include "sysPlatform/SysCpuControl.h"
#include "sysPlatform/AudioStream.h"
#include "sysPlatform/SysDebugPrint.h"
#include "sysPlatform/SysAudio.h"

#ifdef round
#undef round
#endif

#define REMOVE_DC_OFFSET
// DC removal must slew to avoid clicks at startup
constexpr float REMOVE_DC_SLEW_ALPHA = 0.85f;
constexpr float REMOVE_DC_SLEW_MINUS_ALPHA = 1.0f - REMOVE_DC_SLEW_ALPHA;

namespace SysPlatform {

/////////////////////
// SysAudioInputI2S
/////////////////////
struct SysAudioInputI2S::_impl {
	static bool update_responsibility;

	static DMAChannel dma;
	static void isr(void);

    static audio_block_t* block_left;
	static audio_block_t* block_right;
	static uint16_t       block_offset;

	// DC removal for WM8731 codec
	static int leftSum, rightSum;
	static int numBlocks;
	static int numCalibrateBlocks;
	static int leftDcOffset, rightDcOffset;
	static int leftDcOffsetSmoothed, rightDcOffsetSmoothed;
};

struct SysAudioOutputI2S::_impl {
	static void config_i2s(bool only_bclk = false);
	static audio_block_t *block_left_1st;
	static audio_block_t *block_right_1st;
	static bool update_responsibility;
	static DMAChannel dma;
	static void isr(void);

	static audio_block_t *block_left_2nd;
	static audio_block_t *block_right_2nd;
	static uint16_t block_left_offset;
	static uint16_t block_right_offset;
};

DMAMEM __attribute__((aligned(32))) static uint32_t i2s_rx_buffer[AUDIO_BLOCK_SAMPLES];
audio_block_t * SysAudioInputI2S::_impl::block_left = NULL;
audio_block_t * SysAudioInputI2S::_impl::block_right = NULL;
uint16_t SysAudioInputI2S::_impl::block_offset = 0;
bool SysAudioInputI2S::_impl::update_responsibility = false;
DMAChannel SysAudioInputI2S::_impl::dma(false);
int SysAudioInputI2S::_impl::leftSum = 0;
int SysAudioInputI2S::_impl::rightSum = 0;
int SysAudioInputI2S::_impl::numBlocks = 0;
int SysAudioInputI2S::_impl::numCalibrateBlocks = 0;
int SysAudioInputI2S::_impl::leftDcOffset = 0;
int SysAudioInputI2S::_impl::rightDcOffset = 0;
int SysAudioInputI2S::_impl::leftDcOffsetSmoothed = 0;
int SysAudioInputI2S::_impl::rightDcOffsetSmoothed = 0;

SysAudioInputI2S::SysAudioInputI2S(void)
//: AudioStream(0, (audio_block_float32_t**)NULL), m_pimpl(std::make_unique<_impl>())
: AudioStream(0, (audio_block_t**)NULL), m_pimpl(std::make_unique<_impl>())
{
	begin();
}

SysAudioInputI2S::~SysAudioInputI2S()
{

}

void SysAudioInputI2S::enable()
{
	m_enable = true;
}

void SysAudioInputI2S::disable()
{
	release(m_pimpl->block_left);  m_pimpl->block_left  = nullptr;
	release(m_pimpl->block_right); m_pimpl->block_right = nullptr;
	m_pimpl->block_offset = 0;
	m_enable = false;
}

void SysAudioInputI2S::begin(void)
{
	if (m_isInitialized) { return; }

	m_pimpl->dma.begin(true); // Allocate the DMA channel first

	SysAudioOutputI2S::_impl::config_i2s();

	CORE_PIN8_CONFIG  = 3;  //1:RX_DATA0
	IOMUXC_SAI1_RX_DATA0_SELECT_INPUT = 2;

	m_pimpl->dma.TCD->SADDR = (void *)((uint32_t)&I2S1_RDR0 + 2);
	m_pimpl->dma.TCD->SOFF = 0;
	m_pimpl->dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
	m_pimpl->dma.TCD->NBYTES_MLNO = 2;
	m_pimpl->dma.TCD->SLAST = 0;
	m_pimpl->dma.TCD->DADDR = i2s_rx_buffer;
	m_pimpl->dma.TCD->DOFF = 2;
	m_pimpl->dma.TCD->CITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
	m_pimpl->dma.TCD->DLASTSGA = -sizeof(i2s_rx_buffer);
	m_pimpl->dma.TCD->BITER_ELINKNO = sizeof(i2s_rx_buffer) / 2;
	m_pimpl->dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
	m_pimpl->dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_RX);
	m_pimpl->dma.enable();

	I2S1_RCSR = 0;
	I2S1_RCSR = I2S_RCSR_RE | I2S_RCSR_BCE | I2S_RCSR_FRDE | I2S_RCSR_FR;
	m_pimpl->update_responsibility = update_setup();
	m_pimpl->dma.attachInterrupt(SysAudioInputI2S::_impl::isr);

    m_pimpl->leftSum   = 0;
	m_pimpl->rightSum  = 0;
	m_pimpl->numBlocks = 0;
	m_pimpl->numCalibrateBlocks = (2*AUDIO_SAMPLE_RATE_HZ)/AUDIO_SAMPLES_PER_BLOCK; // 2 seconds for DC filter
	m_pimpl->leftDcOffset  = 0;
	m_pimpl->rightDcOffset = 0;
	m_pimpl->leftDcOffsetSmoothed  = 0;
	m_pimpl->rightDcOffsetSmoothed = 0;
	enable();
	m_isInitialized = true;
}

void SysAudioInputI2S::_impl::isr(void)
{
	uint32_t daddr, offset;
	const int16_t *src, *end;
	int16_t *dest_left, *dest_right;
	audio_block_t *left, *right;

	daddr = (uint32_t)(dma.TCD->DADDR);
	dma.clearInterrupt();
	//Serial.println("isr");

	if (daddr < (uint32_t)i2s_rx_buffer + sizeof(i2s_rx_buffer) / 2) {
		// DMA is receiving to the first half of the buffer
		// need to remove data from the second half
		src = (int16_t *)&i2s_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
		end = (int16_t *)&i2s_rx_buffer[AUDIO_BLOCK_SAMPLES];
		if (update_responsibility) AudioStream::update_all();
	} else {
		// DMA is receiving to the second half of the buffer
		// need to remove data from the first half
		src = (int16_t *)&i2s_rx_buffer[0];
		end = (int16_t *)&i2s_rx_buffer[AUDIO_BLOCK_SAMPLES/2];
	}
	left = block_left;
	right = block_right;
	if (left != NULL && right != NULL) {
		offset = block_offset;
		if (offset <= AUDIO_BLOCK_SAMPLES/2) {
			dest_left = &(left->data[offset]);
			dest_right = &(right->data[offset]);
			block_offset = offset + AUDIO_BLOCK_SAMPLES/2;
			arm_dcache_delete((void*)src, sizeof(i2s_rx_buffer) / 2);
			do {
				*dest_left++ = *src++;
				*dest_right++ = *src++;
			} while (src < end);
		}
	}
}

void SysAudioInputI2S::update(void)
{
	if (!m_enable) { return; }
	audio_block_t *new_left=NULL, *new_right=NULL, *out_left=NULL, *out_right=NULL;

	// allocate 2 new blocks, but if one fails, allocate neither
	new_left = allocate();
	if (new_left != NULL) {
		new_right = allocate();
		if (new_right == NULL) {
			release(new_left); new_left = NULL;
		}
	}
	__disable_irq();
	if (m_pimpl->block_offset >= AUDIO_BLOCK_SAMPLES) {
		// the DMA filled 2 blocks, so grab them and get the
		// 2 new blocks to the DMA, as quickly as possible
		out_left = m_pimpl->block_left;
		m_pimpl->block_left = new_left;
		out_right = m_pimpl->block_right;
		m_pimpl->block_right = new_right;
		m_pimpl->block_offset = 0;
		__enable_irq();

#ifdef REMOVE_DC_OFFSET
        if (m_pimpl->numBlocks < m_pimpl->numCalibrateBlocks) {
			for (unsigned i=0; i < AUDIO_SAMPLES_PER_BLOCK; i++) {
				m_pimpl->leftSum  += out_left->data[i];
				m_pimpl->rightSum += out_right->data[i];
			}
			m_pimpl->numBlocks++;
		}
		else if (m_pimpl->numBlocks == m_pimpl->numCalibrateBlocks) {
			m_pimpl->leftDcOffset  = std::round((float)m_pimpl->leftSum / ((float)m_pimpl->numBlocks * (float)AUDIO_SAMPLES_PER_BLOCK));
			m_pimpl->rightDcOffset = std::round((float)m_pimpl->rightSum / ((float)m_pimpl->numBlocks * (float)AUDIO_SAMPLES_PER_BLOCK));
			m_pimpl->numBlocks++;

			for (unsigned i=0; i < AUDIO_SAMPLES_PER_BLOCK; i++) {
				m_pimpl->leftDcOffsetSmoothed  = m_pimpl->leftDcOffsetSmoothed*REMOVE_DC_SLEW_ALPHA + m_pimpl->leftDcOffset*REMOVE_DC_SLEW_MINUS_ALPHA;
				m_pimpl->rightDcOffsetSmoothed = m_pimpl->rightDcOffsetSmoothed*REMOVE_DC_SLEW_ALPHA + m_pimpl->rightDcOffset*REMOVE_DC_SLEW_MINUS_ALPHA;

				out_left->data[i]  -= m_pimpl->leftDcOffsetSmoothed;
				out_right->data[i] -= m_pimpl->rightDcOffsetSmoothed;
			}
		}
		else {
			for (unsigned i=0; i < AUDIO_SAMPLES_PER_BLOCK; i++) {
				out_left->data[i]  -= m_pimpl->leftDcOffsetSmoothed;
				out_right->data[i] -= m_pimpl->rightDcOffsetSmoothed;
			}
		}
#endif

		// then transmit the DMA's former blocks
		transmit(out_left, 0);
		release(out_left); out_left = nullptr;
		transmit(out_right, 1);
		release(out_right); out_right = nullptr;
		//Serial.print(".");
	} else if (new_left != NULL) {
		// the DMA didn't fill blocks, but we allocated blocks
		if (m_pimpl->block_left == NULL) {
			// the DMA doesn't have any blocks to fill, so
			// give it the ones we just allocated
			m_pimpl->block_left = new_left;
			m_pimpl->block_right = new_right;
			m_pimpl->block_offset = 0;
			__enable_irq();
		} else {
			// the DMA already has blocks, doesn't need these
			__enable_irq();
			release(new_left); new_left = nullptr;
			release(new_right); new_right = nullptr;
		}
	} else {
		// The DMA didn't fill blocks, and we could not allocate
		// memory... the system is likely starving for memory!
		// Sadly, there's nothing we can do.
		__enable_irq();
	}
}

//////////////////////
// SysAudioOutputI2S
//////////////////////
audio_block_t * SysAudioOutputI2S::_impl::block_left_1st = NULL;
audio_block_t * SysAudioOutputI2S::_impl::block_right_1st = NULL;
audio_block_t * SysAudioOutputI2S::_impl::block_left_2nd = NULL;
audio_block_t * SysAudioOutputI2S::_impl::block_right_2nd = NULL;
uint16_t  SysAudioOutputI2S::_impl::block_left_offset = 0;
uint16_t  SysAudioOutputI2S::_impl::block_right_offset = 0;
bool SysAudioOutputI2S::_impl::update_responsibility = false;
DMAChannel SysAudioOutputI2S::_impl::dma(false);
DMAMEM __attribute__((aligned(32))) static uint32_t i2s_tx_buffer[AUDIO_BLOCK_SAMPLES];


SysAudioOutputI2S::SysAudioOutputI2S(void)
: AudioStream(2, inputQueueArray), m_pimpl(std::make_unique<_impl>())
{
	begin();
}

SysAudioOutputI2S::~SysAudioOutputI2S()
{

}

void SysAudioOutputI2S::enable()
{
	m_enable = true;
}

void SysAudioOutputI2S::disable()
{
	release(m_pimpl->block_left_1st);  m_pimpl->block_left_1st  = nullptr;
	release(m_pimpl->block_left_2nd);  m_pimpl->block_left_2nd  = nullptr;
	release(m_pimpl->block_right_1st); m_pimpl->block_right_1st = nullptr;
	release(m_pimpl->block_right_2nd); m_pimpl->block_right_2nd = nullptr;
	m_pimpl->block_left_offset = 0;
	m_pimpl->block_right_offset = 0;
	m_enable = false;
}

void SysAudioOutputI2S::begin(void)
{
	if (m_isInitialized) { return; }
	m_pimpl->dma.begin(true); // Allocate the DMA channel first

	m_pimpl->block_left_1st = NULL;
	m_pimpl->block_right_1st = NULL;

	SysAudioOutputI2S::_impl::config_i2s();

	CORE_PIN7_CONFIG  = 3;  //1:TX_DATA0
	m_pimpl->dma.TCD->SADDR = i2s_tx_buffer;
	m_pimpl->dma.TCD->SOFF = 2;
	m_pimpl->dma.TCD->ATTR = DMA_TCD_ATTR_SSIZE(1) | DMA_TCD_ATTR_DSIZE(1);
	m_pimpl->dma.TCD->NBYTES_MLNO = 2;
	m_pimpl->dma.TCD->SLAST = -sizeof(i2s_tx_buffer);
	m_pimpl->dma.TCD->DOFF = 0;
	m_pimpl->dma.TCD->CITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
	m_pimpl->dma.TCD->DLASTSGA = 0;
	m_pimpl->dma.TCD->BITER_ELINKNO = sizeof(i2s_tx_buffer) / 2;
	m_pimpl->dma.TCD->DADDR = (void *)((uint32_t)&I2S1_TDR0 + 2);
	m_pimpl->dma.TCD->CSR = DMA_TCD_CSR_INTHALF | DMA_TCD_CSR_INTMAJOR;
	m_pimpl->dma.triggerAtHardwareEvent(DMAMUX_SOURCE_SAI1_TX);
	m_pimpl->dma.enable();

	I2S1_RCSR |= I2S_RCSR_RE | I2S_RCSR_BCE;
	I2S1_TCSR = I2S_TCSR_TE | I2S_TCSR_BCE | I2S_TCSR_FRDE;

	m_pimpl->update_responsibility = update_setup();
	m_pimpl->dma.attachInterrupt(SysAudioOutputI2S::_impl::isr);

    enable();
	m_isInitialized = true;
}

void SysAudioOutputI2S::update(void)
{
    // null audio device: discard all incoming data
	//if (!active) return;
	//audio_block_t *block = receiveReadOnly();
	//if (block) release(block);

	if (!m_enable) { return; }

	audio_block_t *block;
	block = receiveReadOnly(0); // input 0 = left channel
	if (block) {
		__disable_irq();
		if (m_pimpl->block_left_1st == NULL) {
			m_pimpl->block_left_1st = block;
			m_pimpl->block_left_offset = 0;
			__enable_irq();
		} else if (m_pimpl->block_left_2nd == NULL) {
			m_pimpl->block_left_2nd = block;
			__enable_irq();
		} else {
			audio_block_t *tmp = m_pimpl->block_left_1st;
			m_pimpl->block_left_1st = m_pimpl->block_left_2nd;
			m_pimpl->block_left_2nd = block;
			m_pimpl->block_left_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
	block = receiveReadOnly(1); // input 1 = right channel
	if (block) {
		__disable_irq();
		if (m_pimpl->block_right_1st == NULL) {
			m_pimpl->block_right_1st = block;
			m_pimpl->block_right_offset = 0;
			__enable_irq();
		} else if (m_pimpl->block_right_2nd == NULL) {
			m_pimpl->block_right_2nd = block;
			__enable_irq();
		} else {
			audio_block_t *tmp = m_pimpl->block_right_1st;
			m_pimpl->block_right_1st = m_pimpl->block_right_2nd;
			m_pimpl->block_right_2nd = block;
			m_pimpl->block_right_offset = 0;
			__enable_irq();
			release(tmp);
		}
	}
}

void SysAudioOutputI2S::_impl::isr(void)
{
	int16_t *dest;
	audio_block_t *blockL, *blockR;
	uint32_t saddr, offsetL, offsetR;

	saddr = (uint32_t)(dma.TCD->SADDR);
	dma.clearInterrupt();
	if (saddr < (uint32_t)i2s_tx_buffer + sizeof(i2s_tx_buffer) / 2) {
		// DMA is transmitting the first half of the buffer
		// so we must fill the second half
		dest = (int16_t *)&i2s_tx_buffer[AUDIO_BLOCK_SAMPLES/2];
		if (SysAudioOutputI2S::_impl::update_responsibility) AudioStream::update_all();
	} else {
		// DMA is transmitting the second half of the buffer
		// so we must fill the first half
		dest = (int16_t *)i2s_tx_buffer;
	}

	blockL = SysAudioOutputI2S::_impl::block_left_1st;
	blockR = SysAudioOutputI2S::_impl::block_right_1st;
	offsetL = SysAudioOutputI2S::_impl::block_left_offset;
	offsetR = SysAudioOutputI2S::_impl::block_right_offset;

	if (blockL && blockR) {
		memcpy_tointerleaveLR(dest, blockL->data + offsetL, blockR->data + offsetR);
		offsetL += AUDIO_BLOCK_SAMPLES / 2;
		offsetR += AUDIO_BLOCK_SAMPLES / 2;
	} else if (blockL) {
		memcpy_tointerleaveL(dest, blockL->data + offsetL);
		offsetL += AUDIO_BLOCK_SAMPLES / 2;
	} else if (blockR) {
		memcpy_tointerleaveR(dest, blockR->data + offsetR);
		offsetR += AUDIO_BLOCK_SAMPLES / 2;
	} else {
		memset(dest,0,AUDIO_BLOCK_SAMPLES * 2);
	}

	arm_dcache_flush_delete(dest, sizeof(i2s_tx_buffer) / 2 );

	if (offsetL < AUDIO_BLOCK_SAMPLES) {
		SysAudioOutputI2S::_impl::block_left_offset = offsetL;
	} else {
		SysAudioOutputI2S::_impl::block_left_offset = 0;
		AudioStream::release(blockL); blockL = nullptr;
		SysAudioOutputI2S::_impl::block_left_1st = SysAudioOutputI2S::_impl::block_left_2nd;
		SysAudioOutputI2S::_impl::block_left_2nd = NULL;
	}
	if (offsetR < AUDIO_BLOCK_SAMPLES) {
		SysAudioOutputI2S::_impl::block_right_offset = offsetR;
	} else {
		SysAudioOutputI2S::_impl::block_right_offset = 0;
		AudioStream::release(blockR); blockR = nullptr;
		SysAudioOutputI2S::_impl::block_right_1st = SysAudioOutputI2S::_impl::block_right_2nd;
		SysAudioOutputI2S::_impl::block_right_2nd = NULL;
	}
}

void SysAudioOutputI2S::_impl::config_i2s(bool only_bclk)
{
	CCM_CCGR5 |= CCM_CCGR5_SAI1(CCM_CCGR_ON);

	// if either transmitter or receiver is enabled, do nothing
	if (I2S1_TCSR & I2S_TCSR_TE) return;
	if (I2S1_RCSR & I2S_RCSR_RE) return;

	// not using MCLK in slave mode - hope that's ok?
	//CORE_PIN23_CONFIG = 3;  // AD_B1_09  ALT3=SAI1_MCLK
	CORE_PIN21_CONFIG = 3;  // AD_B1_11  ALT3=SAI1_RX_BCLK
	CORE_PIN20_CONFIG = 3;  // AD_B1_10  ALT3=SAI1_RX_SYNC
	IOMUXC_SAI1_RX_BCLK_SELECT_INPUT = 1; // 1=GPIO_AD_B1_11_ALT3, page 868
	IOMUXC_SAI1_RX_SYNC_SELECT_INPUT = 1; // 1=GPIO_AD_B1_10_ALT3, page 872

	// configure transmitter
	I2S1_TMR = 0;
	I2S1_TCR1 = I2S_TCR1_RFW(1);  // watermark at half fifo size
	I2S1_TCR2 = I2S_TCR2_SYNC(1) | I2S_TCR2_BCP;
	I2S1_TCR3 = I2S_TCR3_TCE;
	I2S1_TCR4 = I2S_TCR4_FRSZ(1) | I2S_TCR4_SYWD(31) | I2S_TCR4_MF
		| I2S_TCR4_FSE | I2S_TCR4_FSP | I2S_RCR4_FSD;
	I2S1_TCR5 = I2S_TCR5_WNW(31) | I2S_TCR5_W0W(31) | I2S_TCR5_FBT(31);

	// configure receiver
	I2S1_RMR = 0;
	I2S1_RCR1 = I2S_RCR1_RFW(1);
	I2S1_RCR2 = I2S_RCR2_SYNC(0) | I2S_TCR2_BCP;
	I2S1_RCR3 = I2S_RCR3_RCE;
	I2S1_RCR4 = I2S_RCR4_FRSZ(1) | I2S_RCR4_SYWD(31) | I2S_RCR4_MF
		| I2S_RCR4_FSE | I2S_RCR4_FSP;
	I2S1_RCR5 = I2S_RCR5_WNW(31) | I2S_RCR5_W0W(31) | I2S_RCR5_FBT(31);
}


/////////////
// SysCodec
/////////////
SysCodec sysCodec;

SysCodec& SysCodec::getCodec() { return sysCodec; }

// Set proper pullups and drive strength for pins. This is necessary
// for WM8731 codec
#define SCL_PAD_CTRL IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_00
#define SDA_PAD_CTRL IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_01
constexpr uint32_t SCL_SDA_PAD_CFG = 0xF808;

#define MCLK_PAD_CTRL  IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_09
#define BCLK_PAD_CTRL  IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_10
#define LRCLK_PAD_CTRL IOMUXC_SW_PAD_CTL_PAD_GPIO_AD_B1_11
#define DAC_PAD_CTRL   IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_01
constexpr uint32_t I2S_PAD_CFG = 0x0008;

constexpr unsigned DEFAULT_PGA_GAIN = 0x17; // 0 dB Gain
constexpr unsigned BYPASS_PGA_GAIN = 0x13; // -6 dB Gain

constexpr int WM8731_NUM_REGS = 10; ///< Number of registers in the internal shadow array
static std::atomic<bool> ctrlBusy(false);

// use const instead of define for proper scoping
constexpr int WM8731_I2C_ADDR = 0x1A;

// The WM8731 register map
constexpr int WM8731_REG_LLINEIN   = 0;
constexpr int WM8731_REG_RLINEIN   = 1;
constexpr int WM8731_REG_LHEADOUT  = 2;
constexpr int WM8731_REG_RHEADOUT  = 3;
constexpr int WM8731_REG_ANALOG     =4;
constexpr int WM8731_REG_DIGITAL   = 5;
constexpr int WM8731_REG_POWERDOWN = 6;
constexpr int WM8731_REG_INTERFACE = 7;
constexpr int WM8731_REG_SAMPLING  = 8;
constexpr int WM8731_REG_ACTIVE    = 9;
constexpr int WM8731_REG_RESET    = 15;

// Register Masks and Shifts
// Register 0
constexpr int WM8731_LEFT_INPUT_GAIN_ADDR = 0;
constexpr int WM8731_LEFT_INPUT_GAIN_MASK = 0x1F;
constexpr int WM8731_LEFT_INPUT_GAIN_SHIFT = 0;
constexpr int WM8731_LEFT_INPUT_MUTE_ADDR = 0;
constexpr int WM8731_LEFT_INPUT_MUTE_MASK = 0x80;
constexpr int WM8731_LEFT_INPUT_MUTE_SHIFT = 7;
constexpr int WM8731_LINK_LEFT_RIGHT_IN_ADDR = 0;
constexpr int WM8731_LINK_LEFT_RIGHT_IN_MASK = 0x100;
constexpr int WM8731_LINK_LEFT_RIGHT_IN_SHIFT = 8;
// Register 1
constexpr int WM8731_RIGHT_INPUT_GAIN_ADDR = 1;
constexpr int WM8731_RIGHT_INPUT_GAIN_MASK = 0x1F;
constexpr int WM8731_RIGHT_INPUT_GAIN_SHIFT = 0;
constexpr int WM8731_RIGHT_INPUT_MUTE_ADDR = 1;
constexpr int WM8731_RIGHT_INPUT_MUTE_MASK = 0x80;
constexpr int WM8731_RIGHT_INPUT_MUTE_SHIFT = 7;
constexpr int WM8731_LINK_RIGHT_LEFT_IN_ADDR = 1;
constexpr int WM8731_LINK_RIGHT_LEFT_IN_MASK = 0x100;
constexpr int WM8731_LINK_RIGHT_LEFT_IN_SHIFT = 8;
// Register 2
constexpr int WM8731_LEFT_HEADPHONE_VOL_ADDR = 2;
constexpr int WM8731_LEFT_HEADPHONE_VOL_MASK = 0x7F;
constexpr int WM8731_LEFT_HEADPHONE_VOL_SHIFT = 0;
constexpr int WM8731_LEFT_HEADPHONE_ZCD_ADDR = 2;
constexpr int WM8731_LEFT_HEADPHONE_ZCD_MASK = 0x80;
constexpr int WM8731_LEFT_HEADPHONE_ZCD_SHIFT = 7;
constexpr int WM8731_LEFT_HEADPHONE_LINK_ADDR = 2;
constexpr int WM8731_LEFT_HEADPHONE_LINK_MASK = 0x100;
constexpr int WM8731_LEFT_HEADPHONE_LINK_SHIFT = 8;
// Register 3
constexpr int WM8731_RIGHT_HEADPHONE_VOL_ADDR = 3;
constexpr int WM8731_RIGHT_HEADPHONE_VOL_MASK = 0x7F;
constexpr int WM8731_RIGHT_HEADPHONE_VOL_SHIFT = 0;
constexpr int WM8731_RIGHT_HEADPHONE_ZCD_ADDR = 3;
constexpr int WM8731_RIGHT_HEADPHONE_ZCD_MASK = 0x80;
constexpr int WM8731_RIGHT_HEADPHONE_ZCD_SHIFT = 7;
constexpr int WM8731_RIGHT_HEADPHONE_LINK_ADDR = 3;
constexpr int WM8731_RIGHT_HEADPHONE_LINK_MASK = 0x100;
constexpr int WM8731_RIGHT_HEADPHONE_LINK_SHIFT = 8;
// Register 4
constexpr int WM8731_ADC_BYPASS_ADDR = 4;
constexpr int WM8731_ADC_BYPASS_MASK = 0x8;
constexpr int WM8731_ADC_BYPASS_SHIFT = 3;
constexpr int WM8731_DAC_SELECT_ADDR = 4;
constexpr int WM8731_DAC_SELECT_MASK = 0x10;
constexpr int WM8731_DAC_SELECT_SHIFT = 4;
// Register 5
constexpr int WM8731_DAC_MUTE_ADDR = 5;
constexpr int WM8731_DAC_MUTE_MASK = 0x8;
constexpr int WM8731_DAC_MUTE_SHIFT = 3;
constexpr int WM8731_HPF_DISABLE_ADDR = 5;
constexpr int WM8731_HPF_DISABLE_MASK = 0x1;
constexpr int WM8731_HPF_DISABLE_SHIFT = 0;
constexpr int WM8731_HPF_STORE_OFFSET_ADDR = 5;
constexpr int WM8731_HPF_STORE_OFFSET_MASK = 0x10;
constexpr int WM8731_HPF_STORE_OFFSET_SHIFT = 4;
// Register 7
constexpr int WM8731_LRSWAP_ADDR = 5;
constexpr int WM8731_LRSWAP_MASK = 0x20;
constexpr int WM8731_LRSWAPE_SHIFT = 5;

// Register 9
constexpr int WM8731_ACTIVATE_ADDR = 9;
constexpr int WM8731_ACTIVATE_MASK = 0x1;

struct SysCodec::_impl {
	// A shadow array for the registers on the codec since the interface is write-only.
	int regArray[WM8731_NUM_REGS];
    bool m_wireStarted = false;
	bool m_gainLocked  = false;

	// low-level write command
	bool write(unsigned int reg, unsigned int val);

	// resets the internal shadow register array
	void resetInternalReg(void);

	// Sets pullups, slew rate and drive strength
	void setOutputStrength(void);

	void setDacSelect(bool val);
	void setHPFDisable(bool val);
	void setActivate(bool val);
};

// Reset the internal shadow register array to match
// the reset state of the codec.
void SysCodec::_impl::resetInternalReg(void) {
	// Set to reset state
	regArray[0] = 0x97;
	regArray[1] = 0x97;
	regArray[2] = 0x79;
	regArray[3] = 0x79;
	regArray[4] = 0x0a;
	regArray[5] = 0x8;
	regArray[6] = 0x9f;
	regArray[7] = 0xa;
	regArray[8] = 0;
	regArray[9] = 0;
}

void SysCodec::_impl::setOutputStrength(void)
{
    // The T4 requires the pads be configured with correct pullups and drive strength
    SCL_PAD_CTRL   = SCL_SDA_PAD_CFG;
    SDA_PAD_CTRL   = SCL_SDA_PAD_CFG;
    MCLK_PAD_CTRL  = I2S_PAD_CFG;
    BCLK_PAD_CTRL  = I2S_PAD_CFG;
    LRCLK_PAD_CTRL = I2S_PAD_CFG;
    DAC_PAD_CTRL   = I2S_PAD_CFG;
}

// Low level write control for the codec via the Teensy I2C interface
bool SysCodec::_impl::write(unsigned int reg, unsigned int val)
{
	bool     done       = false;
	unsigned retryLimit = 50;

    while(ctrlBusy) {}
	ctrlBusy = true;

	while (!done && ((retryLimit--) > 0)) {
		Wire.beginTransmission(WM8731_I2C_ADDR);
		Wire.write((reg << 1) | ((val >> 8) & 1));
		Wire.write(val & 0xFF);
		byte error = Wire.endTransmission();
		if (error) {
			Wire.end();
			Wire.begin();
		    //SYS_DEBUG_PRINT(Serial.println(String("Wire::Error: ") + error + String(" retrying...")));
		} else {
			done = true;
			//SYS_DEBUG_PRINT(Serial.println("Wire::SUCCESS!"));
		}
	}

    ctrlBusy = false;
	return done;
}

// Enable/disable the dynamic HPF (recommended, it creates noise)
void SysCodec::_impl::setHPFDisable(bool val)
{
	if (val) {
		regArray[WM8731_HPF_DISABLE_ADDR] |= WM8731_HPF_DISABLE_MASK;
	} else {
		regArray[WM8731_HPF_DISABLE_ADDR] &= ~WM8731_HPF_DISABLE_MASK;
	}
	write(WM8731_HPF_DISABLE_ADDR, regArray[WM8731_HPF_DISABLE_ADDR]);
}

// Switches the DAC audio in/out of the output path
void SysCodec::_impl::setDacSelect(bool val)
{
	if (val) {
		regArray[WM8731_DAC_SELECT_ADDR] |= WM8731_DAC_SELECT_MASK;
	} else {
		regArray[WM8731_DAC_SELECT_ADDR] &= ~WM8731_DAC_SELECT_MASK;
	}
	write(WM8731_DAC_SELECT_ADDR, regArray[WM8731_DAC_SELECT_ADDR]);
}

// Activate/deactive the I2S audio interface
void SysCodec::_impl::setActivate(bool val)
{
	if (val) {
		write(WM8731_ACTIVATE_ADDR, WM8731_ACTIVATE_MASK);
	} else {
		write(WM8731_ACTIVATE_ADDR, 0);
	}
}

SysCodec::SysCodec()
: m_pimpl(std::make_unique<_impl>())
{
	m_pimpl->resetInternalReg();
}

SysCodec::~SysCodec() = default;

// Powerdown and disable the codec
void SysCodec::disable(void)
{
	SYS_DEBUG_PRINT(Serial.println("Disabling codec"));
	if (m_pimpl->m_wireStarted == false) {
	    Wire.begin();
	    m_pimpl->m_wireStarted = true;
	}
	m_pimpl->setOutputStrength();
	setLeftInputGain(0);
	setRightInputGain(0);
	setDacMute(true); // mute the DAC

	// set OUTPD to '1' (powerdown), which is bit 4
	m_pimpl->regArray[WM8731_REG_POWERDOWN] |= 0x10;
	m_pimpl->write(WM8731_REG_POWERDOWN, m_pimpl->regArray[WM8731_REG_POWERDOWN]);
	delay(100); // wait for power down

	// power down the rest of the supplies
	m_pimpl->write(WM8731_REG_POWERDOWN, 0x9f); // complete codec powerdown
	delay(100);

	resetCodec();
}

void SysCodec::setGainLock(bool lockEnabled)
{
	m_pimpl->m_gainLocked = lockEnabled;
}

// Set the PGA gain on the Left channel
void SysCodec::setLeftInputGain(int val)
{
	if (m_pimpl->m_gainLocked) { return; }

	// change gain incrementally to avoid pops or clicks
	int currentVal = m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR] & WM8731_LEFT_INPUT_GAIN_MASK;
	int increment = 1;
	if (val < currentVal) { increment = -1; }  // set stepping direction
	while (currentVal != val) {
		int writeVal = currentVal + increment;
		m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR] &= ~WM8731_LEFT_INPUT_GAIN_MASK;
		m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR] |=
				((writeVal << WM8731_LEFT_INPUT_GAIN_SHIFT) & WM8731_LEFT_INPUT_GAIN_MASK);
		m_pimpl->write(WM8731_LEFT_INPUT_GAIN_ADDR, m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR]);
		delay(1);
		currentVal = m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR] & WM8731_LEFT_INPUT_GAIN_MASK;
	}
}
int SysCodec::getLeftInputGain()
{
	int currentVal = m_pimpl->regArray[WM8731_LEFT_INPUT_GAIN_ADDR] & WM8731_LEFT_INPUT_GAIN_MASK;
	return currentVal;
}

// Mute control on the ADC Left channel
void SysCodec::setLeftInMute(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_LEFT_INPUT_MUTE_ADDR] |= WM8731_LEFT_INPUT_MUTE_MASK;
	} else {
		m_pimpl->regArray[WM8731_LEFT_INPUT_MUTE_ADDR] &= ~WM8731_LEFT_INPUT_MUTE_MASK;
	}
	m_pimpl->write(WM8731_LEFT_INPUT_MUTE_ADDR, m_pimpl->regArray[WM8731_LEFT_INPUT_MUTE_ADDR]);
}

// Link the gain/mute controls for Left and Right channels
void SysCodec::setLinkLeftRightIn(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_LINK_LEFT_RIGHT_IN_ADDR] |= WM8731_LINK_LEFT_RIGHT_IN_MASK;
		m_pimpl->regArray[WM8731_LINK_RIGHT_LEFT_IN_ADDR] |= WM8731_LINK_RIGHT_LEFT_IN_MASK;
	} else {
		m_pimpl->regArray[WM8731_LINK_LEFT_RIGHT_IN_ADDR] &= ~WM8731_LINK_LEFT_RIGHT_IN_MASK;
		m_pimpl->regArray[WM8731_LINK_RIGHT_LEFT_IN_ADDR] &= ~WM8731_LINK_RIGHT_LEFT_IN_MASK;
	}
	m_pimpl->write(WM8731_LINK_LEFT_RIGHT_IN_ADDR, m_pimpl->regArray[WM8731_LINK_LEFT_RIGHT_IN_ADDR]);
	m_pimpl->write(WM8731_LINK_RIGHT_LEFT_IN_ADDR, m_pimpl->regArray[WM8731_LINK_RIGHT_LEFT_IN_ADDR]);
}

// Set the PGA input gain on the Right channel
void SysCodec::setRightInputGain(int val)
{
	if (m_pimpl->m_gainLocked) { return; }

	// change gain incrementally to avoid pops or clicks
	int currentVal = m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR] & WM8731_RIGHT_INPUT_GAIN_MASK;
	int increment = 1;
	if (val < currentVal) { increment = -1; }  // set stepping direction

	while (currentVal != val) {
		int writeVal = currentVal + increment;
		m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR] &= ~WM8731_RIGHT_INPUT_GAIN_MASK;
		m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR] |=
				((writeVal << WM8731_RIGHT_INPUT_GAIN_SHIFT) & WM8731_RIGHT_INPUT_GAIN_MASK);
		m_pimpl->write(WM8731_RIGHT_INPUT_GAIN_ADDR, m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR]);
		delay(1);
		currentVal = m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR] & WM8731_RIGHT_INPUT_GAIN_MASK;
	}

	m_pimpl->setDacSelect(true);
}

int SysCodec::getRightInputGain()
{
	int currentVal = m_pimpl->regArray[WM8731_RIGHT_INPUT_GAIN_ADDR] & WM8731_RIGHT_INPUT_GAIN_MASK;
	return currentVal;
}

// Mute control on the input ADC right channel
void SysCodec::setRightInMute(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_RIGHT_INPUT_MUTE_ADDR] |= WM8731_RIGHT_INPUT_MUTE_MASK;
	} else {
		m_pimpl->regArray[WM8731_RIGHT_INPUT_MUTE_ADDR] &= ~WM8731_RIGHT_INPUT_MUTE_MASK;
	}
	m_pimpl->write(WM8731_RIGHT_INPUT_MUTE_ADDR, m_pimpl->regArray[WM8731_RIGHT_INPUT_MUTE_ADDR]);
}

// Left/right swap control
bool SysCodec::setLeftRightSwap(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_LRSWAP_ADDR] |= WM8731_LRSWAP_MASK;
	} else {
		m_pimpl->regArray[WM8731_LRSWAP_ADDR] &= ~WM8731_LRSWAP_MASK;
	}
	m_pimpl->write(WM8731_LRSWAP_ADDR, m_pimpl->regArray[WM8731_LRSWAP_ADDR]);
	return true;
}

bool SysCodec::setHeadphoneVolume(float volume)
{
	// the codec volume goes from 0x30 to 0x7F. Anything below 0x30 is mute.
	// 0dB gain is 0x79. Total range is 0x50 (80) possible values.
	unsigned vol;
	constexpr unsigned RANGE = 80.0f;
	if (volume < 0.0f) {
		vol = 0;
	} else if (volume > 1.0f) {
		vol = 0x7f;
	} else {
		vol = 0x2f + static_cast<unsigned>(volume * RANGE);
	}
	m_pimpl->regArray[WM8731_LEFT_HEADPHONE_VOL_ADDR] &= ~WM8731_LEFT_HEADPHONE_VOL_MASK; // clear the volume first
	m_pimpl->regArray[WM8731_LEFT_HEADPHONE_VOL_ADDR] |=
			((vol << WM8731_LEFT_HEADPHONE_VOL_SHIFT) & WM8731_LEFT_HEADPHONE_VOL_MASK);
	m_pimpl->write(WM8731_LEFT_HEADPHONE_VOL_ADDR, m_pimpl->regArray[WM8731_LEFT_HEADPHONE_VOL_ADDR]);
	return true;
}

// Dac output mute control
bool SysCodec::setDacMute(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_DAC_MUTE_ADDR] |= WM8731_DAC_MUTE_MASK;
	} else {
		m_pimpl->regArray[WM8731_DAC_MUTE_ADDR] &= ~WM8731_DAC_MUTE_MASK;
	}
	m_pimpl->write(WM8731_DAC_MUTE_ADDR, m_pimpl->regArray[WM8731_DAC_MUTE_ADDR]);
	return true;
}

// Bypass sends the ADC input audio (analog) directly to analog output stage
// bypassing all digital processing
bool SysCodec::setAdcBypass(bool val)
{
	if (val) {
		m_pimpl->regArray[WM8731_ADC_BYPASS_ADDR] |= WM8731_ADC_BYPASS_MASK;
	} else {
		m_pimpl->regArray[WM8731_ADC_BYPASS_ADDR] &= ~WM8731_ADC_BYPASS_MASK;
	}
	m_pimpl->write(WM8731_ADC_BYPASS_ADDR, m_pimpl->regArray[WM8731_ADC_BYPASS_ADDR]);
	return true;
}

// Trigger the on-chip codec reset
void SysCodec::resetCodec(void)
{
	m_pimpl->write(WM8731_REG_RESET, 0x0);
	m_pimpl->resetInternalReg();
}

bool SysCodec::recalibrateDcOffset(void)
{
    // mute the inputs
    setLeftInMute(true);
    setRightInMute(true);

    // enable the HPF and DC offset store
    m_pimpl->setHPFDisable(false);
    m_pimpl->regArray[WM8731_HPF_STORE_OFFSET_ADDR] |= WM8731_HPF_STORE_OFFSET_MASK;
    m_pimpl->write(WM8731_REG_DIGITAL, m_pimpl->regArray[WM8731_REG_DIGITAL]);

    delay(1000); // wait for the DC offset to be calculated over 1 second
    m_pimpl->setHPFDisable(true); // disable the dynamic HPF calculation
    delay(500);

    // unmute the inputs
    setLeftInMute(false);
    setRightInMute(false);
	return true;
}

// Direct write control to the codec
bool SysCodec::writeI2C(unsigned int addr, unsigned int val)
{
	return m_pimpl->write(addr, val);
}

void SysCodec::begin()
{

}

// Powerup and unmute the codec
void SysCodec::enable(void)
{

    disable(); // disable first in case it was already powered up

    SYS_DEBUG_PRINT(Serial.println("Enabling codec"));
    if (m_pimpl->m_wireStarted == false) {
        Wire.begin();
        m_pimpl->m_wireStarted = true;
    }
    m_pimpl->setOutputStrength();

    // Sequence from WAN0111.pdf
    // Begin configuring the codec
    resetCodec();
    delay(100); // wait for reset

    // Power up all domains except OUTPD and microphone
    m_pimpl->regArray[WM8731_REG_POWERDOWN] = 0x12;
    m_pimpl->write(WM8731_REG_POWERDOWN, m_pimpl->regArray[WM8731_REG_POWERDOWN]);
    delay(100); // wait for codec powerup


    setAdcBypass(false); // causes a slight click
    m_pimpl->setDacSelect(true);
    m_pimpl->setHPFDisable(true);
    setLeftInputGain(DEFAULT_PGA_GAIN); // default input gain
    setRightInputGain(DEFAULT_PGA_GAIN);
    setLeftInMute(false); // no input mute
    setRightInMute(false);
    setDacMute(false); // unmute the DAC

    // link, but mute the headphone outputs
    m_pimpl->regArray[WM8731_REG_LHEADOUT] = WM8731_LEFT_HEADPHONE_LINK_MASK;
    m_pimpl->write(WM8731_REG_LHEADOUT, m_pimpl->regArray[WM8731_REG_LHEADOUT]);      // volume off
    m_pimpl->regArray[WM8731_REG_RHEADOUT] = WM8731_RIGHT_HEADPHONE_LINK_MASK;
    m_pimpl->write(WM8731_REG_RHEADOUT, m_pimpl->regArray[WM8731_REG_RHEADOUT]);

    /// Configure the audio interface
    m_pimpl->write(WM8731_REG_INTERFACE, 0x42); // I2S, 16 bit, MCLK master
    m_pimpl->regArray[WM8731_REG_INTERFACE] = 0x42;

    m_pimpl->write(WM8731_REG_SAMPLING, 0x20);  // 256*Fs, 44.1 kHz, MCLK/1
    m_pimpl->regArray[WM8731_REG_SAMPLING] = 0x20;
    delay(100); // wait for interface config

    // Activate the audio interface
    m_pimpl->setActivate(true);
    delay(100);

    m_pimpl->write(WM8731_REG_POWERDOWN, 0x02); // power up outputs
    m_pimpl->regArray[WM8731_REG_POWERDOWN] = 0x02;
    delay(500); // wait for output to power up

    delay(100); // wait for mute ramp
}

/////////////////////////
// SysAudioGlobalBypass
/////////////////////////

SysAudioGlobalBypass sysAudioGlobalBypass;

SysAudioGlobalBypass& SysAudioGlobalBypass::getGlobalBypass() { return sysAudioGlobalBypass; }

struct SysAudioGlobalBypass::_impl {
	_impl() = delete;
	_impl(SysCodec& codec) : m_sysCodec(codec) {}
	virtual ~_impl() {}

	void init() {
		if (m_initComplete) { return; }
		m_rightInputGain = m_sysCodec.getRightInputGain();
		m_leftInputGain  = m_sysCodec.getLeftInputGain();
		m_initComplete   = true;
	}

    bool m_initComplete = false;
    bool m_bypass = false;
	SysCodec& m_sysCodec;
	int m_leftInputGain  = -1;
	int m_rightInputGain = -1;
};

SysAudioGlobalBypass::SysAudioGlobalBypass(void)
: m_pimpl(std::make_unique<_impl>(SysCodec::getCodec()))
{
    disable();
}

SysAudioGlobalBypass::~SysAudioGlobalBypass() {}

void SysAudioGlobalBypass::enable() {
	m_enable = true;
	bypass(m_pimpl->m_bypass);
}

void SysAudioGlobalBypass::disable() {
	bypass(false);
	m_enable = false;
}

void SysAudioGlobalBypass::bypass(bool byp) {
	if (!m_pimpl->m_initComplete) { m_pimpl->init(); }
	if (!m_enable) { return; }
	m_pimpl->m_bypass = byp;

	if (byp) { // remember the gain settings and set gain to bypass level
	    m_pimpl->m_rightInputGain = m_pimpl->m_sysCodec.getRightInputGain();
		m_pimpl->m_sysCodec.setRightInputGain(BYPASS_PGA_GAIN);
		m_pimpl->m_leftInputGain  = m_pimpl->m_sysCodec.getLeftInputGain();
		m_pimpl->m_sysCodec.setLeftInputGain(BYPASS_PGA_GAIN);

		m_pimpl->m_sysCodec.setGainLock(true); // locks the gain settings so they can't be changed
	} else { // restore the gain settings
		m_pimpl->m_sysCodec.setGainLock(false); // unlocks the gain settings
		if (m_pimpl->m_rightInputGain >= 0) { m_pimpl->m_sysCodec.setRightInputGain(m_pimpl->m_rightInputGain); }
		if (m_pimpl->m_leftInputGain >= 0)  { m_pimpl->m_sysCodec.setLeftInputGain(m_pimpl->m_leftInputGain); }
	}
	m_pimpl->m_sysCodec.setDacMute(byp);
	m_pimpl->m_sysCodec.setAdcBypass(byp);
}

}  // end SysPlatfrom namespace
