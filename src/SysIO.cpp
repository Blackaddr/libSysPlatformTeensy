#include "Arduino.h"
#include "Bounce2.h"
#include "Encoder.h"
#include "SysIOMapping.h"
#include "SysTypes.h"
#include "SysIO.h"

namespace SysPlatform {

const int   POT_CALIB_MIN      = 1;
const int   POT_CALIB_MAX      = 1018;
const bool  POT_SWAP_DIRECTION = true;
const float POT_THRESHOLD      = 0.01f;

const int   EXP_CALIB_MIN      = 515;
const int   EXP_CALIB_MAX      = 950;
const bool  EXP_SWAP_DIRECTION = false;
const float EXP_THRESHOLD      = 0.02f;

static int sysPinMode2BounceMode(PinMode mode);
static int sysPinId2BounceId(int id);
static int sysPinId2AnalogInputId(int id);

static int sysPinId2EncoderIdA(int id);
static int sysPinId2EncoderIdB(int id);

static int sysPinId2OutputId(int id);

///////////////
// SysIO
///////////////
SysIO sysIO;

struct SysIO::_impl {

};

SysIO::SysIO()
: m_pimpl(std::make_unique<_impl>())
{

}

SysIO::~SysIO()
{

}

int SysIO::begin()
{
    return SYS_SUCCESS;
}

bool SysIO::scanInputs()
{
    return false;
}

///////////////
// SysBounce
///////////////
struct SysBounce::_impl {
    Bounce bounce;
};

SysBounce::SysBounce()
: m_pimpl(std::make_unique<_impl>())
{

}

SysBounce::SysBounce(int id, PinMode mode)
: m_pimpl(std::make_unique<_impl>())
{
    int teensyPin = sysPinId2BounceId(id);
    pinMode(teensyPin, INPUT);
    m_pimpl->bounce.attach(teensyPin, sysPinMode2BounceMode(mode));
}

SysBounce::SysBounce(const SysBounce& sysBounce)
: m_pimpl(std::make_unique<_impl>())
{

}

SysBounce::~SysBounce() = default;

void SysBounce::setupPin(int id, PinMode mode)
{
    int teensyPin = sysPinId2BounceId(id);
    pinMode(teensyPin, INPUT);
    m_pimpl->bounce.attach(sysPinId2BounceId(id), sysPinMode2BounceMode(mode));
}

void SysBounce::debounceIntervalMs(int milliseconds)
{
    m_pimpl->bounce.interval(milliseconds);
}

bool SysBounce::read()
{
    return m_pimpl->bounce.read();
}

bool SysBounce::update() {
    return m_pimpl->bounce.update();
}

bool SysBounce::rose()
{
    return m_pimpl->bounce.rose();
}

bool SysBounce::fell()
{
    return m_pimpl->bounce.fell();
}

bool SysBounce::changed()
{
    return m_pimpl->bounce.changed();
}

//////////////
// SysButton
//////////////
SysButton::SysButton()
: SysBounce()
{

}

SysButton::SysButton(int id, PinMode mode)
: SysBounce(id, mode)
{

}

SysButton::~SysButton() = default;

void SysButton::setPressedState(bool state) { m_stateForPressed = state; }
bool SysButton::getPressedState() { return m_stateForPressed; }
bool SysButton::isPressed() {  return m_pimpl->bounce.read() == getPressedState(); }
bool SysButton::pressed() { return m_pimpl->bounce.changed() && isPressed(); }
bool SysButton::released() {  return m_pimpl->bounce.changed() && !isPressed(); }

///////////////
// SysEncoder
///////////////
struct SysEncoder::_impl {
    _impl(int idIn, int pinIdA, int pinIdB) : encoder(pinIdA, pinIdB), id(idIn) {
        pinMode(pinIdA, INPUT);
        pinMode(pinIdB, INPUT);
    }
    Encoder encoder;
    int id;
};

SysEncoder::SysEncoder(int id)
: m_pimpl(std::make_unique<_impl>(id, sysPinId2EncoderIdA(id), sysPinId2EncoderIdB(id)))
{

}

SysEncoder::~SysEncoder() = default;

SysEncoder::SysEncoder(const SysEncoder& sysEncoder)
: m_pimpl(std::make_unique<_impl>(sysEncoder.m_pimpl->id, sysPinId2EncoderIdA(sysEncoder.m_pimpl->id), sysPinId2EncoderIdB(sysEncoder.m_pimpl->id)))
{

}

int SysEncoder::read() {
    return m_pimpl->encoder.read();
}

//////////////
// SysOutput
//////////////
SysOutput::SysOutput() = default;

SysOutput::SysOutput(int id)
: m_mappedId(sysPinId2OutputId(id))
{
    pinMode(m_mappedId, OUTPUT);
}

void SysOutput::setPin(int id)
{
    m_mappedId = sysPinId2OutputId(id);
    pinMode(m_mappedId, OUTPUT);
}
void SysOutput::setValue(bool value)
{
    digitalWriteFast(m_mappedId, value ? 1 : 0);
}

////////////////////
// SysAnalogInput
////////////////////
SysAnalogInput::SysAnalogInput() = default;

SysAnalogInput::SysAnalogInput(int id)
: m_mappedId(sysPinId2AnalogInputId(id))
{
    pinMode(m_mappedId, INPUT);
}

void SysAnalogInput::setPin(int id)
{
    m_mappedId = sysPinId2AnalogInputId(id);
    pinMode(m_mappedId, INPUT);
}
int SysAnalogInput::getValueInt()
{
    return analogRead(m_mappedId);
}

int  sysAnalogReadInt(int id)
{
    return analogRead(sysPinId2AnalogInputId(id));
}

/////////////////
// PIN MAPPINGS
/////////////////
int sysPinMode2BounceMode(PinMode mode)
{
    switch(mode) {
    case PinMode::SYS_INPUT_PIN : return INPUT;
    case PinMode::SYS_OUTPUT_PIN : return OUTPUT;
    case PinMode::SYS_INPUT_PULLUP_PIN : return INPUT_PULLUP;
    default : return INPUT; // unsupported mode
    }
}

static int sysPinId2BounceId(int id)
{
    switch(id) {
    case 0 : return AVALON_SW0_PIN;
    case 1 : return AVALON_SW1_PIN;
    case 2 : return AVALON_ENCODER0_SW_PIN;
    case 3 : return AVALON_ENCODER1_SW_PIN;
    case 4 : return AVALON_ENCODER2_SW_PIN;
    case 5 : return AVALON_ENCODER3_SW_PIN;
    default : return -1;  // invalid pin
    }
}

static int sysPinId2AnalogInputId(int id)
{
    switch(id) {
    case 0 : return AVALON_POT0_PIN;
    case 1 : return AVALON_POT1_PIN;
    case 2 : return AVALON_POT2_PIN;
    case 3 : return AVALON_POT3_PIN;
    default : return -1;  // invalid pin
    }
}

static int sysPinId2OutputId(int id)
{
    switch(id) {
    case 0 : return AVALON_LED0_PIN;
    case 1 : return AVALON_LED1_PIN;
    default : return -1;
    }
}

static int sysPinId2EncoderIdA(int id)
{
    switch(id) {
    case 0 : return AVALON_ENCODER0_A_PIN;
    case 1 : return AVALON_ENCODER1_A_PIN;
    case 2 : return AVALON_ENCODER2_A_PIN;
    case 3 : return AVALON_ENCODER3_A_PIN;
    default : return -1;  // invalid pin
    }
}

static int sysPinId2EncoderIdB(int id)
{
    switch(id) {
    case 0 : return AVALON_ENCODER0_B_PIN;
    case 1 : return AVALON_ENCODER1_B_PIN;
    case 2 : return AVALON_ENCODER2_B_PIN;
    case 3 : return AVALON_ENCODER3_B_PIN;
    default : return -1;  // invalid pin
    }
}

}
