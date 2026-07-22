// pcal6416.h
#pragma once


#include "ztmb/bus.h"


// 7-bit base address when ADDR = 0
#define PCAL6416A_ADDR_BASE      0x20u

// Register addresses (pointer/command bytes) :contentReference[oaicite:1]{index=1}
#define PCAL6416A_REG_INPUT0     0x00u
#define PCAL6416A_REG_INPUT1     0x01u
#define PCAL6416A_REG_OUTPUT0    0x02u
#define PCAL6416A_REG_OUTPUT1    0x03u
#define PCAL6416A_REG_POLARITY0  0x04u
#define PCAL6416A_REG_POLARITY1  0x05u
#define PCAL6416A_REG_CONFIG0    0x06u
#define PCAL6416A_REG_CONFIG1    0x07u
#define PCAL6416A_REG_OUTCFG     0x4Fu   // output port configuration (push-pull/open-drain) :contentReference[oaicite:2]{index=2}

/*
 * Pin numbering used by the driver:
 *
 *   0..7   -> P0.0 .. P0.7
 *   8..15  -> P1.0 .. P1.7
 *
 * Some handy macros:
 */
#define PCAL6416A_PIN_P0_0   0
#define PCAL6416A_PIN_P0_1   1
#define PCAL6416A_PIN_P0_2   2
#define PCAL6416A_PIN_P0_3   3
#define PCAL6416A_PIN_P0_4   4
#define PCAL6416A_PIN_P0_5   5
#define PCAL6416A_PIN_P0_6   6
#define PCAL6416A_PIN_P0_7   7

#define PCAL6416A_PIN_P1_0   8
#define PCAL6416A_PIN_P1_1   9
#define PCAL6416A_PIN_P1_2   10
#define PCAL6416A_PIN_P1_3   11
#define PCAL6416A_PIN_P1_4   12
#define PCAL6416A_PIN_P1_5   13
#define PCAL6416A_PIN_P1_6   14
#define PCAL6416A_PIN_P1_7   15

typedef struct {
    bus_i2c_t i2c_bus;

    // shadow copies of output and config registers so we can RMW quickly
    uint8_t out0;        // Output port 0 (P0.x)
    uint8_t out1;        // Output port 1 (P1.x)
    uint8_t cfg0;        // Config port 0
    uint8_t cfg1;        // Config port 1
} pcal6416a_t;

/**
 * Initialise the PCAL6416A with your required pin directions:
 *
 *  - P0.4, P0.6, P1.0, P1.2, P1.5 as inputs
 *  - Everything else outputs, initial LOW
 *
 * Returns true on success.
 */
bool pcal6416a_init(pcal6416a_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit);

/**
 * Set/clear an individual *output* pin (0..15).
 * If you call this on an input pin it will only update the shadow register,
 * the actual pin will remain input.
 */
bool pcal6416a_write_pin(pcal6416a_t *dev, uint8_t pin, bool level);

/** Configure one pin direction. input=true selects input; false selects output. */
bool pcal6416a_set_pin_direction(pcal6416a_t *dev, uint8_t pin, bool input);

/**
 * Read the logic level of a pin (0..15).
 * Works for both inputs and outputs (it uses the INPUT registers).
 */
bool pcal6416a_read_pin(pcal6416a_t *dev, uint8_t pin, bool *level_out);

/**
 * Optional: read all 16 input bits in one go.
 * bit 0..7  -> P0.0..P0.7
 * bit 8..15 -> P1.0..P1.7
 */
bool pcal6416a_read_all(pcal6416a_t *dev, uint16_t *value_out);
