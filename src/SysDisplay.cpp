#include <cstdint>
#include <cstdarg>
#include "Adafruit_SH1106.h"
#include "Wire.h"
#include "TeensyThreads.h"
#include "Adafruit_GFX.h"
#include "Fonts/FreeSansBold9pt7b.h"
#include "Fonts/FreeSansBold12pt7b.h"
#include "FontTunerNotes24p0.h"
#include "OpenSansBold8p0.h"

#define DisplayType Adafruit_SH1106
#define SWITCHCAPVCC SH1106_SWITCHCAPVCC
#define DISPLAY_I2C SH1106_I2C_ADDRESS

#include "sysPlatform/SysDisplay.h"

namespace SysPlatform {

const unsigned SysDisplay::SYS_BLACK = 0;
const unsigned SysDisplay::SYS_WHITE = 1;
const unsigned SysDisplay::SYS_DISPLAY_HEIGHT = 64;
const unsigned SysDisplay::SYS_DISPLAY_WIDTH  = 128;

// SPI0 pinouts for Mutliverse
constexpr uint8_t SPI0_SCK_PIN  = 13;
constexpr uint8_t SPI0_CS_PIN   = 10;
constexpr uint8_t SPI0_MISO_PIN = 12;
constexpr uint8_t SPI0_MOSI_PIN = 11;
constexpr uint8_t SPI0_DC_PIN   = 37;
constexpr uint8_t SPI0_RST_PIN  = 35;

DisplayType* rawDisplayPtr = nullptr;

// struct SysDisplay::impl {
//     int dummy;
// };

SysDisplay sysDisplay;

SysDisplay::SysDisplay()
//: m_pimpl(nullptr)
{
    //m_pimpl->dummy = 0;
    //(void)(m_pimpl->dummy);
    rawDisplayPtr = new DisplayType(SPI0_DC_PIN, SPI0_RST_PIN, SPI0_CS_PIN); // The Avalon OLED display is on SPI0;
}

SysDisplay::~SysDisplay()
{

}

void SysDisplay::begin()
{
    rawDisplayPtr->begin(SWITCHCAPVCC, DISPLAY_I2C);
    rawDisplayPtr->setTextWrap(false);
}

unsigned SysDisplay::getHeight() { return SYS_DISPLAY_HEIGHT; }

unsigned SysDisplay::getWidth() { return SYS_DISPLAY_WIDTH; }

void SysDisplay::setFont(Font font)
{
    switch (font) {
        case Font::DEFAULT_SMALL : rawDisplayPtr->setFont(&Open_Sans_Bold_Small); break;
        case Font::TUNER         : rawDisplayPtr->setFont(&TunerNotes24p0); break;
        case Font::DEFAULT_LARGE :
        default:
            rawDisplayPtr->setFont(&FreeSansBold9pt7b); break;
    }
}

void SysDisplay::getTextBounds(const char *string, int16_t x, int16_t y, int16_t *x1,
                     int16_t *y1, uint16_t *w, uint16_t *h)
{
    rawDisplayPtr->getTextBounds(string, x, y, x1, y1, w, h);
}

void SysDisplay::setTextColor(uint16_t color)
{
    if (color) { color = WHITE; }
    else { color = BLACK; }
    rawDisplayPtr->setTextColor(color);
}

void SysDisplay::setCursor(int16_t x, int16_t y)
{
    rawDisplayPtr->setCursor(x,y);
}

void SysDisplay::clearDisplay()
{
    rawDisplayPtr->clearDisplay();
}

void SysDisplay::invertDisplay(bool invert)
{
    rawDisplayPtr->invertDisplay(uint8_t(invert));
}

int16_t SysDisplay::getCursorX()
{
    return rawDisplayPtr->getCursorX();
}

int16_t SysDisplay::getCursorY()
{
    return rawDisplayPtr->getCursorY();
}

int SysDisplay::printf(const char* str, const char *fmt, ...)
{
    int result;
    va_list args;

    va_start(args, fmt);
    result = rawDisplayPtr->printf(str, fmt, args);
    va_end(args);

    return result;
}

void SysDisplay::display()
{
    rawDisplayPtr->display();
}

void SysDisplay::fillScreen(uint16_t color)
{
    if (color) { color = WHITE; }
    else { color = BLACK; }
    rawDisplayPtr->fillScreen(color);
}

void SysDisplay::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                    uint16_t color)
{
    if (color) { color = WHITE; }
    else { color = BLACK; }
    rawDisplayPtr->drawLine(x0, y0, x1, y1, color);
}

void SysDisplay::drawRect(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint16_t color)
{
    if (color) { color = WHITE; }
    else { color = BLACK; }
    rawDisplayPtr->drawRect(x,y,w,h,color);
}

void SysDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint16_t color)
{
    if (color) { color = WHITE; }
    else { color = BLACK; }
    rawDisplayPtr->fillRect(x,y,w,h,color);
}

void SysDisplay::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, int16_t w, int16_t h,
                uint16_t color)
{
    rawDisplayPtr->drawBitmap(x,y,bitmap,w,h,color);
}

}