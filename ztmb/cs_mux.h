// cs_mux.h
#pragma once

#include <stdint.h>
#include "hardware/gpio.h"
#include "ztmb/cs_lines.h"

// ---- Pin mapping -----------------------------------------------------------

#ifndef CS_A0_PIN
#define CS_A0_PIN 6   // First 74HC138: A0
#endif

#ifndef CS_A1_PIN
#define CS_A1_PIN 5   // First 74HC138: A1
#endif

#ifndef CS_A2_PIN
#define CS_A2_PIN 4   // First 74HC138: A2
#endif

#ifndef CS_A3_PIN
#define CS_A3_PIN 26  // Second 74HC138: A0
#endif

#ifndef CS_A4_PIN
#define CS_A4_PIN 27  // Second 74HC138: A1
#endif

#ifndef CS_A5_PIN
#define CS_A5_PIN 28  // Second 74HC138: A2
#endif

// ---- Per-chip "none selected" addresses -----------------------------------
// For a 74HC138, address bits (A2,A1,A0) = 0,1,1 (binary 3) will select Y3.
// You said your unused / idle state is 0,1,1 for both chips.
//
// If you need to change this later, override before including this header:
//   #define CS0_NONE_ADDR  3
//   #define CS1_NONE_ADDR  3

#ifndef CS0_NONE_ADDR
#define CS0_NONE_ADDR 3
#endif

#ifndef CS1_NONE_ADDR
#define CS1_NONE_ADDR 3
#endif

// Public "no line" indicator
#define CS_INIT_STATE (-2)
#define CS_NO_LINE (-1)

#ifdef __cplusplus
extern "C" {
#endif

// Initialise all CS GPIOs and put both decoders into their idle state.
void cs_init(void);

// Select a decoded CS line by integer:
//
//   line 0..7   -> first 74HC138, Y0..Y7  (second chip idle)
//   line 8..15  -> second 74HC138, Y0..Y7 (first chip idle)
//   line <0 or >15 -> both chips idle (no device selected)
//
// You can safely pass CS_NO_LINE or 16 to mean "none".
void cs_select_line(int line);

// Convenience wrapper: deselect all devices.
static inline void cs_deselect(void) {
    cs_select_line(CS_NO_LINE);
}

// Optional: get the currently selected line, or CS_NO_LINE.
int cs_get_current_line(void);

#ifdef __cplusplus
}
#endif
