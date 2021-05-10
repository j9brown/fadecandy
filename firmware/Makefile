#######################################################
# Environment setup

# toolchain
DFU_SUFFIX = dfu-suffix
DFU_UTIL = dfu-util
CC = arm-none-eabi-gcc
CXX = arm-none-eabi-g++
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
SIZE = arm-none-eabi-size

# Bootloader to include in whole-chip .hex image
FCBOOT_IMAGE = ../bin/fc-boot-v101.hex

#######################################################

# The name of your project (used to name the compiled .dfu file)
TARGET = fc-firmware

# Temporary .hex file for application image only, without bootloader
APP_HEX = app-image.hex

# Sources
C_FILES = \
	hw/startup.c \
	hw/pins_teensy.c \
	hw/usb_desc.c \
	hw/usb_dev.c \
	hw/usb_mem.c \
	hw/serial1.c

CPP_FILES = \
	debug.cpp \
	fadecandy.cpp \
	led_driver.cpp \
	time.cpp

# Headers
INCLUDES = -I. -I../common

# CPPFLAGS = compiler options for C and C++
CPPFLAGS = -Werror -Wall -Wno-sign-compare -Wno-strict-aliasing \
	-g -O3 -mcpu=cortex-m4 -mthumb -nostdlib \
	-fno-tree-loop-distribute-patterns -MMD \
	$(OPTIONS) $(INCLUDES)

# compiler options for C++ only
CXXFLAGS = -std=c++17 -felide-constructors -fno-exceptions -fno-rtti

# compiler options for C only
CFLAGS =

# linker script
LDSCRIPT = fcb-app.ld

# linker options
LDFLAGS = -Os -Wl,--gc-sections -mcpu=cortex-m4 -mthumb -nostdlib -T$(LDSCRIPT)

# additional libraries to link
LIBS = -lm

OBJS := $(C_FILES:.c=.o) $(CPP_FILES:.cpp=.o)

all: $(TARGET).dfu $(TARGET).hex

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

%.dfu: %.elf
	$(SIZE) $<
	$(OBJCOPY) -O binary $< $@
	$(DFU_SUFFIX) -a $@

%.hex: %.elf
	$(OBJCOPY) -O ihex $< $(APP_HEX)
	(grep -v :00000001FF $(FCBOOT_IMAGE); cat $(APP_HEX)) > $@

install: $(TARGET).dfu
	$(DFU_UTIL) -d 1d50 -D $<

benchmark: install
	python benchmark.py

# compiler generated dependency info
-include $(OBJS:.o=.d)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET).elf $(TARGET).dfu $(APP_HEX)

disassemble: $(TARGET).elf
	$(OBJDUMP) -d -C -S $< | less

symbols: $(TARGET).elf
	$(OBJDUMP) -t $< | sort | less

.PHONY: all clean install disassemble symbols benchmark
