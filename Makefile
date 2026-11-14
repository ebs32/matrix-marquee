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

# Serial port of the board. Set AVR_PORT in the environment (tools/avr passes
# it into the container), or override with `make flash PORT=/dev/ttyUSB0`.
AVR_PORT ?= /dev/ttyACM0
PORT    = $(AVR_PORT)
# Programmer type; `arduino` uploads through the Uno bootloader.
PROG    = arduino

TARGET  = main
SOURCES = src/main.c
HEADERS = src/charset.h src/config.h src/max7219.h

.PHONY: all flash size clean

all: $(TARGET).hex

$(TARGET).elf: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SOURCES)

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

size: $(TARGET).elf
	$(SIZE) --mcu=$(MCU) -C $<

flash: $(TARGET).hex
	$(AVRDUDE) -p $(MCU) -c $(PROG) -P $(PORT) -b $(BAUD) -U flash:w:$<:i -v

clean:
	rm -f $(TARGET).elf $(TARGET).hex
