#COMPILER_PATH=$(COMPILER_PATH)
SRC_FILE_LIST=Test
TARGET=Test
#CORE_FILENAME=core.$(APP_MAJOR_VER).$(APP_MINOR_VER).$(APP_PATCH_VER).dat
TARGET_HEXNAME=$(TARGET).hex
COMPILER_FILENAME=arm-none-eabi-
COMMON_FLAGS +=
TOOL_PREFIX=$(COMPILER_PATH)/$(COMPILER_FILENAME)
CC      = $(TOOL_PREFIX)gcc
CXX     = $(TOOL_PREFIX)g++
OBJCOPY = $(TOOL_PREFIX)objcopy
CPPFLAGS += -c -Wall $(COMMON_FLAGS)
CPPFLAGS += -ffunction-sections -fdata-sections -fno-exceptions
CPPFLAGS += -Wno-error=narrowing
CPPFLAGS += -mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16
CPPFLAGS += -nostdlib
CPPFLAGS += -D__IMXRT1062__ -DTEENSYDUINO=156 -DARDUINO=10819 -DF_CPU=600000000 -DUSB_MIDI_AUDIO_SERIAL -DLAYOUT_US_ENGLISH
CPPFLAGS += -DSTD_MUTEX_EXTERNAL="\"TeensyThreads.h\""
CPPFLAGS += -DAUDIO_BLOCK_SAMPLES=128 -DAUDIO_SAMPLE_RATE_EXACT=48000.0f

ifeq ($(AVALON_REV),2)
CPPFLAGS += -DARDUINO_TEENSY_MICROMOD -DAVALON_REV2
endif
CPPFLAGS += -I../dist/$(ARCH)/include
LIB_LIST = sysPlatform TeensyThreads Adafruit_SH1106 Adafruit_SSD1306 Adafruit_BusIO Adafruit-GFX-Library Bounce2 Encoder Teensy-4.x-Quad-Encoder-Library SPI Wire WDT_T4 EEPROM LittleFS cores
INCLUDE_DIRS = $(addprefix -I../dist/$(ARCH)/include/, $(LIB_LIST)) 
CPPFLAGS += $(INCLUDE_DIRS)
CXXFLAGS += -std=gnu++17 -fpermissive -fno-rtti -fno-threadsafe-statics -felide-constructors
LDFLAGS  += -O2 -Wl,--gc-sections,--relax
LDFLAGS  += -mthumb -mcpu=cortex-m7 -mfloat-abi=hard -mfpu=fpv5-d16 -L../dist/$(ARCH)/lib
LDFLAGS_BASE_LIBS += -larm_cortexM7lfsp_math -lm -lstdc++
LD_FILE  = -T./imxrt1062.micromod.ld
RELEASEFLAGS = -O2 -D NDEBUG
OBJ_FILES = $(addsuffix .o, $(SRC_FILE_LIST))
SYS_STAT_LIBS =
CORE_LIBS = $(addprefix -l, $(LIB_LIST)) -lsysPlatform
OBJCOPY_HEX_FLAGS += -O ihex -R .eeprom

all: $(TARGET)
%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(RELEASEFLAGS) -c -o $@ $<
$(TARGET): $(OBJ_FILES)
	$(CC) -o $(TARGET) $(LDFLAGS) $(LD_FILE) $(OBJ_FILES) $(SYS_STAT_LIBS) $(CORE_LIBS) $(LDFLAGS_BASE_LIBS)
	$(OBJCOPY) $(OBJCOPY_HEX_FLAGS) $(TARGET) $(TARGET_HEXNAME)
clean:
	-rm -f $(OBJ_FILES)
	-rm -f $(TARGET)
	-rm -f $(TARGET_HEXNAME)
