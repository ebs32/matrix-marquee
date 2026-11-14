/* SPDX-License-Identifier: MIT */

#ifndef CONFIG_H
#define CONFIG_H

#include <assert.h>

/* Text to scroll across the matrix; printable ASCII only. */
#define MESSAGE_TEXT                 "Hello, world!"

/*
 * Shown in place of any message character the font does not cover. Must be
 * a printable ASCII character.
 */
#define MESSAGE_FALLBACK_CHAR        '?'

/* Display brightness, from 0x00 (dimmest) to 0x0F (brightest). */
#define DISPLAY_INTENSITY_LEVEL      0x01

/* How long the panel stays dark after power-on. */
#define STARTUP_DELAY_MS             1000UL

/*
 * Each scroll step shows the frame for LIT_PHASE_MS, then blanks the panel
 * for DARK_PHASE_MS. The dark gap stops the eye from merging the old and new
 * column positions into a doubled edge. Each phase must be at least 1 ms, and
 * the two together at most 100 ms.
 */
#define LIT_PHASE_MS                 5UL
#define DARK_PHASE_MS                20UL

/*
 * Pauses inserted while bit-banging a command to the MAX7219, in CPU cycles
 * (one cycle is 62.5 ns at 16 MHz). The datasheet minimums are in brackets;
 * every value here meets them, with CLK high held well beyond its minimum.
 */
#define MAX7219_LOAD_TO_CLOCK_CYCLES 2  /* LOAD low to CLK rise   [25 ns] */
#define MAX7219_DATA_SETUP_CYCLES    2  /* DIN valid to CLK rise  [25 ns] */
#define MAX7219_CLOCK_HIGH_CYCLES    18 /* CLK high time          [50 ns] */
#define MAX7219_CLOCK_LOW_CYCLES     2  /* CLK low time           [50 ns] */
#define MAX7219_LOAD_HIGH_CYCLES     2  /* LOAD high after latch  [50 ns] */

/*
 * Limits on the scroll timing settings above. Slower than 100 ms per column
 * is too slow to read as scrolling.
 */
static_assert(
  LIT_PHASE_MS > 0 && DARK_PHASE_MS > 0,
  "LIT_PHASE_MS and DARK_PHASE_MS must be at least 1 ms"
);
static_assert(
  LIT_PHASE_MS + DARK_PHASE_MS <= 100,
  "LIT_PHASE_MS + DARK_PHASE_MS must be at most 100 ms"
);

#endif /* CONFIG_H */
