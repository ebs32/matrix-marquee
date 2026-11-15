/* SPDX-License-Identifier: MIT */

/* Scroll a printable ASCII message across a MAX7219-compatible LED matrix. */

#ifndef F_CPU
  #error "F_CPU must be defined (e.g. -DF_CPU=16000000UL)"
#endif

#include "charset.h"
#include "config.h"
#include "max7219.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <util/delay.h>

/*
 * Store the message in program memory so it can grow without using SRAM.
 * Leading and trailing spaces let the message enter and leave the display.
 */
static const char message[] PROGMEM = "  " MESSAGE_TEXT "  ";

/*
 * GPIO pins on the ATmega328P that are broken out on the Arduino R3.
 * Excludes PB6/PB7 (clock) and PC6 (RESET). PB5 also drives the L LED and
 * PD0/PD1 are shared with the USB-serial bridge.
 */
#define PORTB_AVAILABLE   0x3F
#define PORTC_AVAILABLE   0x3F
#define PORTD_AVAILABLE   0xFF

/*
 * Pins driven as outputs; other available pins are parked as inputs. Pins
 * selected by PORTx_DIDR remain Hi-Z; the rest have pull-ups enabled.
 */
#define MAX7219_DATA_PIN  PC3
#define MAX7219_LOAD_PIN  PC4
#define MAX7219_CLOCK_PIN PC5

/* clang-format off */
#define PORTB_OUTPUTS     (_BV(PB5))
#define PORTC_OUTPUTS     (_BV(MAX7219_DATA_PIN) | \
                           _BV(MAX7219_LOAD_PIN) | \
                           _BV(MAX7219_CLOCK_PIN))
#define PORTD_OUTPUTS     0
/* clang-format on */

static_assert(
  (PORTB_OUTPUTS & ~PORTB_AVAILABLE) == 0,
  "PORTB_OUTPUTS contains pins not in PORTB_AVAILABLE"
);
static_assert(
  (PORTC_OUTPUTS & ~PORTC_AVAILABLE) == 0,
  "PORTC_OUTPUTS contains pins not in PORTC_AVAILABLE"
);
static_assert(
  (PORTD_OUTPUTS & ~PORTD_AVAILABLE) == 0,
  "PORTD_OUTPUTS contains pins not in PORTD_AVAILABLE"
);

/* Pins whose digital input buffer can be disabled: PC0-PC5, PD6/PD7. */
#define PORTC_DIDR 0x3F
#define PORTD_DIDR 0xC0

static_assert(
  (PORTC_DIDR & ~PORTC_AVAILABLE) == 0,
  "PORTC_DIDR contains pins not in PORTC_AVAILABLE"
);
static_assert(
  (PORTD_DIDR & ~PORTD_AVAILABLE) == 0,
  "PORTD_DIDR contains pins not in PORTD_AVAILABLE"
);

/* Timer1 runs in CTC mode from clk/64. */
#define TIMER1_PRESCALER 64UL
#define TIMER1_CS_BITS   (_BV(CS11) | _BV(CS10))

/* Number of Timer1 ticks in a duration in milliseconds, rounded down. */
#define TIMER1_MS_TO_TICKS(ms) \
  ((unsigned long long) (ms) * F_CPU / (1000UL * TIMER1_PRESCALER))

/* Whether `ticks` is within timer1_wait()'s valid range. */
#define TIMER1_WAIT_TICKS_VALID(ticks) ((ticks) >= 2 && (ticks) <= UINT16_MAX)

/* Set by the Timer1 compare-match ISR to end the current timer1_wait(). */
static volatile bool timer1_expired;

/*
 * Timer1 reached OCR1A: signal timer1_wait() that the interval has elapsed.
 * Waking the CPU from idle sleep is a side effect of taking the interrupt.
 */
ISR(TIMER1_COMPA_vect) { timer1_expired = true; }

/*
 * Set up the GPIO and put the peripherals in a low-power state. Call with
 * interrupts off; this doesn't turn them on.
 */
static void mcu_init(void) {
  /*
   * Release PD0/PD1 from the USART (it may have been left enabled by the
   * bootloader).
   */
  UCSR0B = 0;

  /* Disable all GPIO outputs before changing their latches. */
  DDRB = 0;
  DDRC = 0;
  DDRD = 0;

  /* Allow the pull-ups; PUD overrides PORTx when set. */
  MCUCR &= ~_BV(PUD);

  /* Enable pull-ups on unused pins. Keep all output latches low initially. */
  PORTB = PORTB_AVAILABLE & ~PORTB_OUTPUTS;
  PORTC = PORTC_AVAILABLE & ~PORTC_OUTPUTS & ~PORTC_DIDR;
  PORTD = PORTD_AVAILABLE & ~PORTD_OUTPUTS & ~PORTD_DIDR;

  /* The MAX7219 idles with DIN and CLK low, and LOAD high. */
  PORTC |= _BV(MAX7219_LOAD_PIN);

  /* Enable outputs after their idle levels are established. */
  DDRB = PORTB_OUTPUTS;
  DDRC = PORTC_OUTPUTS;
  DDRD = PORTD_OUTPUTS;

  /* Disable the ADC and its interrupt. */
  ADCSRA = 0;

  /* Analog comparator off (no PRR bit). Also clears ACIE. */
  ACSR = _BV(ACD);

  /* Disconnect the input buffers on the Hi-Z pins. */
  DIDR0 = PORTC_DIDR & ~PORTC_OUTPUTS;
  DIDR1 = ((PORTD_DIDR & ~PORTD_OUTPUTS) >> 6) & 3;

  /* Stop the clocks of all peripherals. Re-enable what is needed after. */
  power_all_disable();

  /* Timer1 in CTC mode, left stopped. */
  power_timer1_enable();

  TCCR1A = 0;
  TCCR1B = _BV(WGM12);
  TCCR1C = 0;

  TCNT1 = 0;
  OCR1A = 0;

  TIFR1  = _BV(OCF1A);
  TIMSK1 = _BV(OCIE1A);
}

/*
 * Sleep in idle mode for `ticks` Timer1 counts, then stop the timer.
 * Interrupts are disabled on return.
 *
 * Valid range is 2..65535. Both 0 and 1 wait the full 65536 counts:
 *   - 0 wraps OCR1A to 0xFFFF.
 *   - 1 is missed because the TCNT1 write blocks the immediate compare match.
 */
static void timer1_wait(uint16_t ticks) {
  cli();

  timer1_expired = false;

  /* Set the compare value and clear OCF1A. */
  TCNT1 = 0;
  OCR1A = ticks - 1;
  TIFR1 = _BV(OCF1A);

  /* Start the clock. */
  TCCR1B |= TIMER1_CS_BITS;

  /*
   * sei() right before sleep_cpu() keeps the interrupt from firing between the
   * flag check and going to sleep: the instruction after sei() always runs
   * before a pending interrupt is taken.
   */
  while (!timer1_expired) {
    sei();
    sleep_cpu();
    cli();
  }

  /* Stop the clock. */
  TCCR1B &= ~TIMER1_CS_BITS;
}

/*
 * Send one MAX7219 register command. LOAD remains low while address and data
 * are shifted, then rises to latch both bytes into the display driver.
 */
static void send_command(uint8_t address, uint8_t value) {
  uint16_t cmd = ((uint16_t) address << 8) | value;

  /* Begin a transaction with LOAD and CLK low. */
  PORTC &= ~(
    _BV(MAX7219_DATA_PIN) | _BV(MAX7219_LOAD_PIN) | _BV(MAX7219_CLOCK_PIN)
  );

  /*
   * Give the MAX7219 a moment to notice LOAD went low before the first
   * clock pulse.
   */
  __builtin_avr_delay_cycles(MAX7219_LOAD_TO_CLOCK_CYCLES);

  /* Shift the register address first, followed by its data byte. */
  for (uint16_t mask = 0x8000; mask != 0; mask >>= 1) {
    /* Present the current bit before the clock's rising edge. */
    if (cmd & mask) {
      PORTC |= _BV(MAX7219_DATA_PIN);
    } else {
      PORTC &= ~_BV(MAX7219_DATA_PIN);
    }

    /* Let the data line settle so the chip reads the right bit. */
    __builtin_avr_delay_cycles(MAX7219_DATA_SETUP_CYCLES);

    /* The MAX7219 samples DIN on the rising clock edge. */
    PORTC |= _BV(MAX7219_CLOCK_PIN);

    /* Hold the clock high long enough for the chip to register the pulse. */
    __builtin_avr_delay_cycles(MAX7219_CLOCK_HIGH_CYCLES);

    /* Complete the clock pulse. */
    PORTC &= ~_BV(MAX7219_CLOCK_PIN);

    /* Keep the clock low briefly before the next bit's pulse. */
    __builtin_avr_delay_cycles(MAX7219_CLOCK_LOW_CYCLES);
  }

  /* A rising LOAD edge transfers the shifted command to the MAX7219. */
  PORTC |= _BV(MAX7219_LOAD_PIN);

  /*
   * Keep LOAD high long enough for the chip to latch the command before
   * another one can start.
   */
  __builtin_avr_delay_cycles(MAX7219_LOAD_HIGH_CYCLES);

  /* DIN and CLK back to idle; LOAD stays high. */
  PORTC &= ~(_BV(MAX7219_DATA_PIN) | _BV(MAX7219_CLOCK_PIN));
}

/* Clear every MAX7219 digit register. */
static void clear_panel(void) {
  for (uint8_t i = MAX7219_REG_DIGIT_0; i <= MAX7219_REG_DIGIT_7; i++) {
    send_command(i, MAX7219_ALL_SEGMENTS_OFF);
  }
}

/* The phase durations main() passes to timer1_wait(). */
static_assert(
  TIMER1_WAIT_TICKS_VALID(TIMER1_MS_TO_TICKS(LIT_PHASE_MS)),
  "LIT_PHASE_MS is outside timer1_wait()'s range"
);
static_assert(
  TIMER1_WAIT_TICKS_VALID(TIMER1_MS_TO_TICKS(DARK_PHASE_MS)),
  "DARK_PHASE_MS is outside timer1_wait()'s range"
);

int main(void) {
  /* The cursor identifies the glyph and column at the left display edge. */
  uint16_t char_index    = 0;
  uint8_t  column_offset = 0;

  mcu_init();

  /* Idle is the deepest sleep mode that keeps Timer1 running. */
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();

  /*
   * Put the MAX7219 in shutdown (it may still be running after a reset of the
   * Arduino alone), configure it and clear the digits. The main loop turns
   * the display on for each frame.
   */

  /* clang-format off */
  send_command(MAX7219_REG_SHUTDOWN    , MAX7219_SHUTDOWN_MODE   );
  send_command(MAX7219_REG_DECODE_MODE , MAX7219_NO_DECODE       );
  send_command(MAX7219_REG_INTENSITY   , DISPLAY_INTENSITY_LEVEL );
  send_command(MAX7219_REG_SCAN_LIMIT  , MAX7219_SCAN_ALL_DIGITS );
  send_command(MAX7219_REG_DISPLAY_TEST, MAX7219_DISPLAY_TEST_OFF);
  /* clang-format on */

  clear_panel();

  /* Keep the panel dark briefly after power-on. */
  _delay_ms(STARTUP_DELAY_MS);

  while (true) {
    uint8_t  column = 0;
    uint16_t index  = char_index;
    uint8_t  offset = column_offset;
    uint8_t  buffer[8];

    /* Build the columns currently visible on the matrix. */
    memset(buffer, 0, sizeof(buffer));

    while (column < sizeof(buffer) && index < sizeof(message) - 1) {
      uint8_t character = pgm_read_byte(&message[index]);

      /* Draw characters the font lacks with the fallback glyph. */
      if (character < CHARSET_FIRST_CHAR || character > CHARSET_LAST_CHAR) {
        character = MESSAGE_FALLBACK_CHAR;
      }

      buffer[column++] = pgm_read_byte(
        &font7x8[character - CHARSET_FIRST_CHAR][offset]
      );

      if (++offset == CHARSET_COLUMN_WIDTH) {
        offset = 0;

        index++;
      }
    }

    /* Move the cursor one font column for the next frame. */
    if (++column_offset == CHARSET_COLUMN_WIDTH) {
      column_offset = 0;

      if (++char_index == sizeof(message) - 1) {
        char_index = 0;
      }
    }

    /* Transfer the viewport to the MAX7219 digit registers. */
    for (uint8_t i = MAX7219_REG_DIGIT_0; i <= MAX7219_REG_DIGIT_7; i++) {
      send_command(i, buffer[i - MAX7219_REG_DIGIT_0]);
    }

    /* Keep the frame lit. */
    send_command(MAX7219_REG_SHUTDOWN, MAX7219_NORMAL_OPERATION);
    timer1_wait(TIMER1_MS_TO_TICKS(LIT_PHASE_MS));

    /*
     * Clear the digits before shutting down. Some MAX7219 clones briefly show
     * the old digit data again when they leave shutdown, which ghosts this
     * frame next to the new one. Blank digits leave nothing to show. The
     * clear must come before shutdown; clearing during shutdown is too late.
     */
    clear_panel();

    /*
     * Shutdown stops the scan for the dark phase, cutting the MAX7219's own
     * current. The digit registers stay writable, so the next frame is loaded
     * while the panel is still dark.
     */
    send_command(MAX7219_REG_SHUTDOWN, MAX7219_SHUTDOWN_MODE);
    timer1_wait(TIMER1_MS_TO_TICKS(DARK_PHASE_MS));
  }
}
