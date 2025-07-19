#include <Arduino.h>
#include "usb_dev.h"
#include "usb_audio.h"
#include "SysWatchdog.h"
#include "SysAudio.h"

namespace SysPlatform {

/////////////////////
// SysAudioInputUsb
/////////////////////
struct SysAudioInputUsb::_impl {
    int dummy;
};

SysAudioInputUsb::SysAudioInputUsb(void)
//: AudioStream(0, (audio_block_float32_t**)NULL), m_pimpl(nullptr)
: AudioStream(0, (audio_block_t**)NULL), m_pimpl(nullptr)
{
    begin();
}

SysAudioInputUsb::~SysAudioInputUsb()
{

}

void SysAudioInputUsb::enable()
{
	m_enable = true;
}

void SysAudioInputUsb::disable()
{
	release(AudioInputUSB::incoming_left);  AudioInputUSB::incoming_left  = nullptr;
	release(AudioInputUSB::incoming_right); AudioInputUSB::incoming_right = nullptr;
	release(AudioInputUSB::ready_left);     AudioInputUSB::ready_left     = nullptr;
	release(AudioInputUSB::ready_right);    AudioInputUSB::ready_right    = nullptr;
	AudioInputUSB::incoming_count = 0;
	AudioInputUSB::receive_flag = 0;
	m_enable = false;
}

void SysAudioInputUsb::update(void)
{
	if (!m_enable) { return; }
	audio_block_t *left, *right;

	__disable_irq();
	left = AudioInputUSB::ready_left;
	AudioInputUSB::ready_left = NULL;
	right = AudioInputUSB::ready_right;
	AudioInputUSB::ready_right = NULL;
	uint16_t c = AudioInputUSB::incoming_count;
	uint8_t f = AudioInputUSB::receive_flag;
	AudioInputUSB::receive_flag = 0;
	__enable_irq();
	if (f) {
		int diff = AUDIO_BLOCK_SAMPLES/2 - (int)c;
		feedback_accumulator += diff * 1;
		//uint32_t feedback = (feedback_accumulator >> 8) + diff * 100;
		//usb_audio_sync_feedback = feedback;

		//printf(diff >= 0 ? "." : "^");
	}
	//serial_phex(c);
	//serial_print(".");
	if (!left || !right) {
		usb_audio_underrun_count++;
		//printf("#"); // buffer underrun - PC sending too slow
		if (f) feedback_accumulator += 3500;
	}
	if (left) {
		transmit(left, 0);
		release(left); left = nullptr;
	}
	if (right) {
		transmit(right, 1);
		release(right); right = nullptr;
	}
}

void SysAudioInputUsb::begin(void)
{
    if (m_isInitialized) { return; }

	AudioInputUSB::incoming_count = 0;
	AudioInputUSB::incoming_left = NULL;
	AudioInputUSB::incoming_right = NULL;
	AudioInputUSB::ready_left = NULL;
	AudioInputUSB::ready_right = NULL;
	AudioInputUSB::receive_flag = 0;
	// update_responsibility = update_setup();
	// TODO: update responsibility is tough, partly because the USB
	// interrupts aren't sychronous to the audio library block size,
	// but also because the PC may stop transmitting data, which
	// means we no longer get receive callbacks from usb.c
	AudioInputUSB::update_responsibility = false;

    enable();
    m_isInitialized = true;
}

float SysAudioInputUsb::volume(void) {
    return AudioInputUSB::volume();
}

/////////////////////
// SysAudioOutputUsb
/////////////////////
struct SysAudioOutputUsb::_impl {
    int dummy;
};

SysAudioOutputUsb::SysAudioOutputUsb(void)
: AudioStream(2, inputQueueArray), m_pimpl(nullptr)
{
    begin();
}

SysAudioOutputUsb::~SysAudioOutputUsb()
{

}

void SysAudioOutputUsb::enable()
{
	m_enable = true;
}

void SysAudioOutputUsb::disable()
{
	release(AudioOutputUSB::left_1st);  AudioOutputUSB::left_1st  = nullptr;
	release(AudioOutputUSB::right_1st); AudioOutputUSB::right_1st = nullptr;
	release(AudioOutputUSB::left_2nd);  AudioOutputUSB::left_2nd  = nullptr;
	release(AudioOutputUSB::right_2nd); AudioOutputUSB::right_2nd = nullptr;
	AudioOutputUSB::offset_1st = 0;
	m_enable = false;
}

void SysAudioOutputUsb::update(void)
{
	audio_block_t *left, *right;

	// TODO: we shouldn't be writing to these......
	left = receiveReadOnly(0); // input 0 = left channel
	right = receiveReadOnly(1); // input 1 = right channel
	//left = receiveWritable(0); // input 0 = left channel
	//right = receiveWritable(1); // input 1 = right channel

    if (!sysWatchdog.isStarted()) {
        sysWatchdog.begin(0.5f);
    }

	if (usb_audio_transmit_setting == 0) {
		if (left) { release(left); left = nullptr; }
		if (right) { release(right); right = nullptr; }
		if (AudioOutputUSB::left_1st) { release(AudioOutputUSB::left_1st); AudioOutputUSB::left_1st = NULL; }
		if (AudioOutputUSB::left_2nd) { release(AudioOutputUSB::left_2nd); AudioOutputUSB::left_2nd = NULL; }
		if (AudioOutputUSB::right_1st) { release(AudioOutputUSB::right_1st); AudioOutputUSB::right_1st = NULL; }
		if (AudioOutputUSB::right_2nd) { release(AudioOutputUSB::right_2nd); AudioOutputUSB::right_2nd = NULL; }
		AudioOutputUSB::offset_1st = 0;
		return;
	}
	if (left == NULL) {
		left = allocate();
		if (left == NULL) {
			if (right) { release(right); right = nullptr; }
			return;
		}
		//memset(left->data, 0, sizeof(left->data));
		memset(left->data, 0, sizeof(int16_t)*AUDIO_SAMPLES_PER_BLOCK);
	}
	if (right == NULL) {
		right = allocate();
		if (right == NULL) {
			if (left) { release(left); left = nullptr; }
			return;
		}
		//memset(right->data, 0, sizeof(right->data));
		memset(right->data, 0, sizeof(int16_t)*AUDIO_SAMPLES_PER_BLOCK);
	}
	__disable_irq();
	if (AudioOutputUSB::left_1st == NULL) {
		AudioOutputUSB::left_1st = left;
		AudioOutputUSB::right_1st = right;
		AudioOutputUSB::offset_1st = 0;
	} else if (AudioOutputUSB::left_2nd == NULL) {
		AudioOutputUSB::left_2nd = left;
		AudioOutputUSB::right_2nd = right;
	} else {
		// buffer overrun - PC is consuming too slowly
		audio_block_t *discard1 = AudioOutputUSB::left_1st;
		AudioOutputUSB::left_1st = AudioOutputUSB::left_2nd;
		AudioOutputUSB::left_2nd = left;
		audio_block_t *discard2 = AudioOutputUSB::right_1st;
		AudioOutputUSB::right_1st = AudioOutputUSB::right_2nd;
		AudioOutputUSB::right_2nd = right;
		AudioOutputUSB::offset_1st = 0; // TODO: discard part of this data?
		//serial_print("*");
		release(discard1);  discard1 = nullptr; // we know left_1st is not NULL
		if (discard2) { release(discard2); discard2 = nullptr; }
	}
	__enable_irq();
}

void SysAudioOutputUsb::begin(void)
{
    if (m_isInitialized) { return; }
	AudioOutputUSB::update_responsibility = false;
	AudioOutputUSB::left_1st = NULL;
	AudioOutputUSB::right_1st = NULL;

	enable();

    m_isInitialized = true;
}

}

