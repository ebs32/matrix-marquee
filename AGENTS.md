# Agent notes

AVR firmware in C11 for the ATmega328P on an Arduino Uno R3. It scrolls a
message across an 8×8 MAX7219 LED matrix, bit-banged on A3–A5 (PC3–PC5).
The README describes the hardware, wiring and settings.

## Layout

- `src/main.c`: everything that runs: MCU setup, Timer1 delays, the
  bit-banged MAX7219 driver and the scroll loop
- `src/max7219.h`: MAX7219 register addresses and values
- `src/charset.h`: the font, stored in flash
- `src/config.h`: user settings (message, brightness, timing) and the
  bit-banged signal timing (`MAX7219_*_CYCLES`)
- `tools/avr`: runs a command with the toolchain in Docker
  (`.devcontainer/Dockerfile`)

## Commands

```sh
tools/avr make                      # build main.hex
tools/avr make size                 # flash and RAM usage
tools/avr make dist VERSION=v1.2.3  # release files in dist/
tools/avr clang-format --dry-run --Werror src/*.c src/*.h
```

CI fails on any compiler warning or formatting difference.

## Conventions

- Follow `.clang-format` and `.editorconfig`.
- Keep the README in sync with the code, especially the settings table and
  the timing and wiring descriptions.

## Reviewing changes

Look for bugs, hardware or timing mistakes (MAX7219 datasheet timing, pin
and register use, integer overflow on the 8-bit AVR), style that breaks the
conventions above, and README text the changes make out of date. Don't
comment on matters of taste.
