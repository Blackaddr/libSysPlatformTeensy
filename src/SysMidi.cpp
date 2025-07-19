#include <MIDI.h>
#include "SysMidi.h"
#include "SysLogger.h"

using namespace midi;

namespace SysPlatform {

SysMidiBase::SysMidiBase() = default;
SysMidiBase::~SysMidiBase() = default;

bool SysMidiBase::putMsg(uint8_t msg[4]) { return false; }

/////////////
// SERIAL MIDI
/////////////
#if defined(PROCESS_SERIAL_MIDI)
SysSerialMidi sysSerialMidi;
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, serialMIDI)

struct SysSerialMidi::_impl {

};

SysSerialMidi::SysSerialMidi()
: m_pimpl(nullptr)
{

}

SysSerialMidi::~SysSerialMidi()
{

}

void SysSerialMidi::init()
{
    serialMIDI.begin();
    serialMIDI.turnThruOff();
    m_isInitialized = true;
}

bool SysSerialMidi::read()
{
    return serialMIDI.read();
}

bool SysSerialMidi::putMsg(uint8_t msg[4]) { return false; }

MidiMessageType SysSerialMidi::getType()
{
    return static_cast<MidiMessageType>(serialMIDI.getType());
}

uint8_t* SysSerialMidi::getSysExArray()
{
    return (uint8_t*)serialMIDI.getSysExArray();
}

size_t   SysSerialMidi::getSysExArrayLength()
{
    return static_cast<size_t>(serialMIDI.getSysExArrayLength());
}

uint8_t  SysSerialMidi::getChannel()
{
    return static_cast<uint8_t>(serialMIDI.getChannel());
}

uint8_t  SysSerialMidi::getData1()
{
    return static_cast<uint8_t>(serialMIDI.getData1());
}

uint8_t  SysSerialMidi::getData2()
{
    return static_cast<uint8_t>(serialMIDI.getData2());
}

void SysSerialMidi::sendSysEx(size_t sysExDataLength, uint8_t* sysExData)
{
    serialMIDI.sendSysEx(static_cast<unsigned>(sysExDataLength), (byte*)sysExData);
}

void SysSerialMidi::sendProgramChange(unsigned program, unsigned channel)
{
    serialMIDI.sendProgramChange(static_cast<DataByte>(program), static_cast<Channel>(channel));
}

void SysSerialMidi::sendMidiMessage(uint8_t type, uint8_t channel, uint8_t data1, uint8_t data2)
{

}
#endif

/////////////
// USB MIDI
/////////////
#if defined(USB_MIDI_AUDIO_SERIAL) && defined(PROCESS_USB_MIDI)
SysUsbMidi sysUsbMidi;

struct SysUsbMidi::_impl {

};

SysUsbMidi::SysUsbMidi()
: m_pimpl(nullptr)
{

}

SysUsbMidi::~SysUsbMidi()
{

}

void SysUsbMidi::init()
{
    m_isInitialized = true;
}

bool SysUsbMidi::read()
{
    return usbMIDI.read();
}

bool SysUsbMidi::putMsg(uint8_t msg[4]) { return false; }

MidiMessageType SysUsbMidi::getType()
{
    return static_cast<MidiMessageType>(usbMIDI.getType());
}

uint8_t* SysUsbMidi::getSysExArray()
{
    return (uint8_t*)usbMIDI.getSysExArray();
}

size_t   SysUsbMidi::getSysExArrayLength()
{
    return static_cast<size_t>(usbMIDI.getSysExArrayLength());
}

uint8_t  SysUsbMidi::getChannel()
{
    return static_cast<uint8_t>(usbMIDI.getChannel());
}

uint8_t  SysUsbMidi::getData1()
{
    return static_cast<uint8_t>(usbMIDI.getData1());
}

uint8_t  SysUsbMidi::getData2()
{
    return static_cast<uint8_t>(usbMIDI.getData2());
}

void SysUsbMidi::sendSysEx(size_t sysExDataLength, uint8_t* sysExData)
{
    usbMIDI.sendSysEx(static_cast<unsigned>(sysExDataLength), (byte*)sysExData);
}

void SysUsbMidi::sendProgramChange(unsigned program, unsigned channel)
{
    usbMIDI.sendProgramChange(static_cast<DataByte>(program), static_cast<Channel>(channel));
}

void SysUsbMidi::sendMidiMessage(uint8_t type, uint8_t channel, uint8_t data1, uint8_t data2)
{
    usbMIDI.send(type, data1, data2, channel, 0 /* cable */);
}
#endif

}
