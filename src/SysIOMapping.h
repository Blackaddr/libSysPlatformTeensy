#pragma once

#include <cstdint>
#include "Arduino.h"

namespace SysPlatform {

constexpr uint8_t AVALON_POT0_PIN        = A8; // INPUT GAIN
constexpr uint8_t AVALON_POT1_PIN        = A0; // OUTPUT LEVEL
constexpr uint8_t AVALON_POT2_PIN        = A2; // EXP T
constexpr uint8_t AVALON_POT3_PIN        = A3; // EXP R

constexpr uint8_t AVALON_ENCODER0_A_PIN  = 33;
constexpr uint8_t AVALON_ENCODER0_B_PIN  = 2;
constexpr uint8_t AVALON_ENCODER0_SW_PIN = 45;

constexpr uint8_t AVALON_ENCODER1_A_PIN  = 31;
constexpr uint8_t AVALON_ENCODER1_B_PIN  = 36;
constexpr uint8_t AVALON_ENCODER1_SW_PIN = 32;

constexpr uint8_t AVALON_ENCODER2_A_PIN  = 5;
constexpr uint8_t AVALON_ENCODER2_B_PIN  = 4;
constexpr uint8_t AVALON_ENCODER2_SW_PIN = 41;

constexpr uint8_t AVALON_ENCODER3_A_PIN  = 3;
constexpr uint8_t AVALON_ENCODER3_B_PIN  = 30;
constexpr uint8_t AVALON_ENCODER3_SW_PIN = 40;

constexpr uint8_t AVALON_SW0_PIN         = 6;
constexpr uint8_t AVALON_SW1_PIN         = 34;

constexpr uint8_t AVALON_LED0_PIN        = 9;
constexpr uint8_t AVALON_LED1_PIN        = 42;

constexpr uint8_t AVALON_SPI0_CS_PIN     = 10;
constexpr uint8_t AVALON_SPI0_SCK_PIN    = 13;
constexpr uint8_t AVALON_SPI0_MISO_PIN   = 12;
constexpr uint8_t AVALON_SPI0_MOSI_PIN   = 11;

constexpr uint8_t AVALON_SPI1_CS0_PIN    = 38; // SPI Flash, changes to 38 in TEENSYDUINO 1.56, was 39 in earlier TEENSYDUINO
constexpr uint8_t AVALON_SPI1_CS1_PIN    = 43; // SRAM
constexpr uint8_t AVALON_SPI1_SCK_PIN    = 27;
constexpr uint8_t AVALON_SPI1_MISO_PIN   = 1;
constexpr uint8_t AVALON_SPI1_MOSI_PIN   = 26;

}
