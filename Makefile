MCU     = atmega328p
F_CPU   = 16000000UL
BAUD    = 115200

CC      = avr-gcc
OBJCOPY = avr-objcopy
SIZE    = avr-size
AVRDUDE = avrdude

CFLAGS  = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=c11 \
          -Wall -Wextra -Wpedantic
LDFLAGS = -mmcu=$(MCU)

# Version in release file names; the release workflow sets it from the tag.
VERSION ?= dev

# Serial port of the board. Set AVR_PORT in the environment (tools/avr passes
# it into the container), or override with `make flash AVR_PORT=/dev/ttyUSB0`.
AVR_PORT ?= /dev/ttyACM0

# Programmer type; `arduino` uploads through the Uno bootloader.
PROG    = arduino

TARGET  = main
SOURCES = src/main.c
HEADERS = src/charset.h src/config.h src/max7219.h

.PHONY: all flash size dist clean

all: $(TARGET).hex

$(TARGET).elf: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	$(SIZE) --mcu=$(MCU) -C $<

flash: $(TARGET).hex
	$(AVRDUDE) -p $(MCU) -c $(PROG) -P $(AVR_PORT) -b $(BAUD) -U flash:w:$<:i -v

dist: $(TARGET).hex
	rm -rf dist
	mkdir dist
	cp $< 'dist/matrix-marquee-$(VERSION)-$(MCU).hex'

clean:
	rm -f $(TARGET).elf $(TARGET).hex
	rm -rf dist
