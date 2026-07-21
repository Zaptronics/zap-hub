/* sx1509.h - SX1509B (16-ch) I2C GPIO expander driver for Raspberry Pi Pico SDK
 *
 * - Single .h/.c pair
 * - Multiple instances supported (one sx1509_t per device)
 * - Simple GPIO API + full register map + raw register access
 *
 * Notes:
 * - "Bank A" = IO[0..7]  => pins 0..7
 * - "Bank B" = IO[8..15] => pins 8..15
 * - Many registers are split B then A at consecutive addresses.
 */

#pragma once

#include "ztmb/bus.h"


#ifdef __cplusplus
extern "C" {
#endif

// ---------- Common SX1509B 7-bit I2C addresses (depend on ADDR pins) ----------
#define SX1509_I2C_ADDR_0 0x3E
#define SX1509_I2C_ADDR_1 0x3F
#define SX1509_I2C_ADDR_2 0x70
#define SX1509_I2C_ADDR_3 0x71

// ---------- Register map (SX1509B) ----------
typedef enum {
    SX1509_REG_INPUT_DISABLE_B      = 0x00,
    SX1509_REG_INPUT_DISABLE_A      = 0x01,
    SX1509_REG_LONG_SLEW_B          = 0x02,
    SX1509_REG_LONG_SLEW_A          = 0x03,
    SX1509_REG_LOW_DRIVE_B          = 0x04,
    SX1509_REG_LOW_DRIVE_A          = 0x05,
    SX1509_REG_PULL_UP_B            = 0x06,
    SX1509_REG_PULL_UP_A            = 0x07,
    SX1509_REG_PULL_DOWN_B          = 0x08,
    SX1509_REG_PULL_DOWN_A          = 0x09,
    SX1509_REG_OPEN_DRAIN_B         = 0x0A,
    SX1509_REG_OPEN_DRAIN_A         = 0x0B,
    SX1509_REG_POLARITY_B           = 0x0C,
    SX1509_REG_POLARITY_A           = 0x0D,
    SX1509_REG_DIR_B                = 0x0E,
    SX1509_REG_DIR_A                = 0x0F,
    SX1509_REG_DATA_B               = 0x10,
    SX1509_REG_DATA_A               = 0x11,
    SX1509_REG_INTERRUPT_MASK_B     = 0x12,
    SX1509_REG_INTERRUPT_MASK_A     = 0x13,
    SX1509_REG_SENSE_HIGH_B         = 0x14,
    SX1509_REG_SENSE_LOW_B          = 0x15,
    SX1509_REG_SENSE_HIGH_A         = 0x16,
    SX1509_REG_SENSE_LOW_A          = 0x17,
    SX1509_REG_INTERRUPT_SOURCE_B   = 0x18,
    SX1509_REG_INTERRUPT_SOURCE_A   = 0x19,
    SX1509_REG_EVENT_STATUS_B       = 0x1A,
    SX1509_REG_EVENT_STATUS_A       = 0x1B,
    SX1509_REG_LEVEL_SHIFTER_1      = 0x1C,
    SX1509_REG_LEVEL_SHIFTER_2      = 0x1D,
    SX1509_REG_CLOCK                = 0x1E,
    SX1509_REG_MISC                 = 0x1F,
    SX1509_REG_LED_DRIVER_ENABLE_B  = 0x20,
    SX1509_REG_LED_DRIVER_ENABLE_A  = 0x21,
    SX1509_REG_DEBOUNCE_CONFIG      = 0x22,
    SX1509_REG_DEBOUNCE_ENABLE_B    = 0x23,
    SX1509_REG_DEBOUNCE_ENABLE_A    = 0x24,
    SX1509_REG_KEY_CONFIG_1         = 0x25,
    SX1509_REG_KEY_CONFIG_2         = 0x26,
    SX1509_REG_KEY_DATA_1           = 0x27,
    SX1509_REG_KEY_DATA_2           = 0x28,

    // LED driver block: 0x29..0x68 (per-pin, not all pins have TRise/TFall)
    SX1509_REG_LED_BASE             = 0x29,

    SX1509_REG_HIGH_INPUT_B         = 0x69,
    SX1509_REG_HIGH_INPUT_A         = 0x6A,

    SX1509_REG_RESET                = 0x7D, // write 0x12 then 0x34 (consecutive) to reset
    SX1509_REG_TEST_1               = 0x7E, // do not write
    SX1509_REG_TEST_2               = 0x7F, // do not write
} sx1509_reg_t;

// ---------- Bit helpers (selected) ----------
#define SX1509_MISC_AUTOINC_OFF_BIT      (1u << 1) // 0=autoinc ON, 1=OFF
#define SX1509_MISC_NINT_AUTOCLEAR_BIT   (1u << 0) // 0=autoclear interrupt source on DATA read
#define SX1509_CLOCK_OSC_SRC_MASK        (0x3u << 5) // bits 6:5
#define SX1509_CLOCK_OSC_SRC_OFF         (0x0u << 5)
#define SX1509_CLOCK_OSC_SRC_EXT         (0x1u << 5)
#define SX1509_CLOCK_OSC_SRC_INT_2MHZ    (0x2u << 5)
#define SX1509_CLOCK_OSCIO_OUTPUT_BIT    (1u << 4)  // 0=OSCIO input, 1=OSCIO output
#define SX1509_CLOCK_OSCOUT_DIV_MASK     0x0Fu      // bits 3:0

// RegMisc bits
#define SX1509_MISC_LOG_B              0x80  // bit 7 (bank B fading-capable IOs)
//#define SX1509_MISC_LED_CLKDIV_MASK    0x70  // bits 6:4 (ClkX divisor; 0 disables ClkX) :contentReference[oaicite:4]{index=4}
#define SX1509_MISC_LED_CLKDIV_SHIFT   4
#define SX1509_MISC_LED_CLKDIV_MASK    (0x7u << 4)
#define SX1509_MISC_LOG_A              0x08  // bit 3 (bank A fading-capable IOs)
#define SX1509_MISC_NRESET_RESET_ALL   0x04  // bit 2
#define SX1509_MISC_AUTOINC_DISABLE    0x02  // bit 1 (1=disable autoinc)
#define SX1509_MISC_AUTOCLEAR_OFF      0x01  // bit 0 (1=disable autoclear)

// ---------- Types ----------
typedef enum {
    SX1509_OK                 = true,
    SX1509_ERR_BAD_ARG        = -1,
    SX1509_ERR_I2C            = -2,
    SX1509_ERR_NOT_PRESENT    = -3,
} sx1509_status_t;

typedef enum {
    SX1509_SENSE_NONE   = 0x0,
    SX1509_SENSE_RISING = 0x1,
    SX1509_SENSE_FALLING= 0x2,
    SX1509_SENSE_BOTH   = 0x3,
} sx1509_sense_t;

// Pin configuration flags (simple, composable)
typedef enum {
    SX1509_PIN_INPUT          = (1u << 0), // default if OUTPUT not set
    SX1509_PIN_OUTPUT         = (1u << 1),

    SX1509_PIN_PULLUP         = (1u << 2),
    SX1509_PIN_PULLDOWN       = (1u << 3),
    SX1509_PIN_OPENDRAIN      = (1u << 4),
    SX1509_PIN_INVERT         = (1u << 5),

    SX1509_PIN_INPUT_DISABLE  = (1u << 6), // disable input buffer (useful for LED sinking)
    SX1509_PIN_LONG_SLEW      = (1u << 7),
    SX1509_PIN_LOW_DRIVE      = (1u << 8), // reduced drive strength
    SX1509_PIN_HIGH_INPUT     = (1u << 9), // enable "high input" mode (per datasheet)
} sx1509_pin_flags_t;

// Runtime/config struct (fill fields, then call sx1509_init(&dev))
typedef struct {
    // --- user-provided configuration ---
    bus_i2c_t   i2c_bus;

    // --- runtime ---
    bool       present;       // set true by init() if device responds
    uint8_t    led_clk_div;
} sx1509_t;

// ---------- Init / presence ----------

int sx1509_init(sx1509_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit);
int sx1509_software_reset(sx1509_t *dev);

// ---------- Raw register access (full coverage) ----------
int sx1509_reg_read8(sx1509_t *dev, uint8_t reg, uint8_t *val);
int sx1509_reg_write8(sx1509_t *dev, uint8_t reg, uint8_t val);
int sx1509_reg_read_n(sx1509_t *dev, uint8_t start_reg, uint8_t *buf, size_t len);
int sx1509_reg_write_n(sx1509_t *dev, uint8_t start_reg, const uint8_t *buf, size_t len);

// Convenience for common paired registers (B then A)
int sx1509_reg_read16_be(sx1509_t *dev, uint8_t reg_b, uint16_t *val);   // (B<<8)|A
int sx1509_reg_write16_be(sx1509_t *dev, uint8_t reg_b, uint16_t val);

// ---------- Simple GPIO API ----------
int sx1509_pin_configure(sx1509_t *dev, uint8_t pin, uint32_t flags);

int sx1509_digital_read(sx1509_t *dev, uint8_t pin, bool *level);
int sx1509_digital_write(sx1509_t *dev, uint8_t pin, bool level);
int sx1509_digital_toggle(sx1509_t *dev, uint8_t pin);

int sx1509_port_read(sx1509_t *dev, uint16_t *value);
int sx1509_port_write(sx1509_t *dev, uint16_t value);      // writes DATA regs (all 16 bits)
int sx1509_port_set_bits(sx1509_t *dev, uint16_t mask);    // set bits high in DATA
int sx1509_port_clear_bits(sx1509_t *dev, uint16_t mask);  // clear bits low in DATA

// Direction helpers
int sx1509_pin_set_input(sx1509_t *dev, uint8_t pin);
int sx1509_pin_set_output(sx1509_t *dev, uint8_t pin);

// ---------- Interrupts ----------
int sx1509_interrupt_configure(sx1509_t *dev, uint8_t pin, bool enable, sx1509_sense_t sense);
int sx1509_interrupt_get_source(sx1509_t *dev, uint16_t *source);     // bits where interrupt occurred
int sx1509_interrupt_clear_source(sx1509_t *dev, uint16_t mask);      // write-1-to-clear

// ---------- Clock (OSC) ----------
int sx1509_clock_configure(sx1509_t *dev, uint8_t osc_src_bits, bool oscio_output, uint8_t oscout_div);

// ---------- Keypad engine (minimal helpers; raw regs available for full control) ----------
int sx1509_keypad_read(sx1509_t *dev, uint8_t *keydata1, uint8_t *keydata2);

// ---------- LED driver register helpers (addressing) ----------
static inline bool sx1509_pin_is_fade_capable(uint8_t pin) {
    return (pin >= 4 && pin <= 7) || (pin >= 12 && pin <= 15);
}

// Returns register addresses for LED driver parameters for a given pin.
// For non-fade-capable pins, trise/tfall regs are returned as 0xFF.
void sx1509_led_regs_for_pin(uint8_t pin,
                            uint8_t *reg_ton,
                            uint8_t *reg_ion,
                            uint8_t *reg_off,
                            uint8_t *reg_trise,
                            uint8_t *reg_tfall);


// LED-driver “PWM” helpers (active-low current sink; needs pull-up for logic-high)
int sx1509_pwm_enable(sx1509_t *dev, uint8_t clkdiv_code);              // 1..7
int sx1509_pwm_pin_start(sx1509_t *dev, uint8_t pin, uint8_t duty_pct); // 0..100
int sx1509_pwm_pin_set_duty(sx1509_t *dev, uint8_t pin, uint8_t duty_pct);
int sx1509_pwm_pin_stop(sx1509_t *dev, uint8_t pin);


// Effect modes (for blink/breathe/pulse helpers below)
typedef enum {
    SX1509_EFFECT_SOLID = 0,   // Static mode (TOn=0), uses RegData to select on/off level
    SX1509_EFFECT_BLINK,       // Blink mode (TOn!=0 && TOff!=0)
    SX1509_EFFECT_BREATHE,     // Blink mode + fades (only fade-capable pins)
    SX1509_EFFECT_PULSE        // Single-shot (TOn!=0 && TOff==0). Retrigger with sx1509_effect_trigger()
} sx1509_effect_mode_t;

typedef struct {
    sx1509_effect_mode_t mode;

    uint8_t  pin;

    // RegMisc[6:4] (1..7). 0 disables LED driver clock globally. :contentReference[oaicite:3]{index=3}
    uint8_t  clkx_div_code;

    // For fade-capable pins, you can select linear/log curve per bank via RegMisc bits. :contentReference[oaicite:4]{index=4}
    bool     log_mode;

    // Output driver: true=open-drain, false=push-pull
    bool     open_drain;

    // Brightness at max intensity:
    // If your existing PWM API uses "255 = fully ON" but the chip behaves inverted, set invert_brightness=true.
    bool     invert_brightness;
    uint8_t  on_brightness_0_255;

    // Off intensity is only 3 bits: RegOff[2:0], where IOff = 4 * RegOff[2:0] (linear). :contentReference[oaicite:5]{index=5}
    uint8_t  off_intensity_0_7;  // usually 0

    // Times in ms (hold times at max/min intensity). 0 means “no hold”.
    // Blink: on_ms>0 and off_ms>0
    // Pulse: on_ms>0 and off_ms=0 (single-shot) :contentReference[oaicite:6]{index=6}
    uint16_t on_ms;
    uint16_t off_ms;

    // Fade times (ms). Only applied on fade-capable pins.
    uint16_t rise_ms;
    uint16_t fall_ms;

} sx1509_effect_cfg_t;

// Configure LED engine registers for the effect (does NOT necessarily retrigger single-shot)
int sx1509_effect_configure(sx1509_t *dev, const sx1509_effect_cfg_t *cfg);

// Start continuous effects (blink/breathe) or force "on" in static mode: clears RegData bit low. :contentReference[oaicite:7]{index=7}
int sx1509_effect_start(sx1509_t *dev, uint8_t pin);

// Stop (hold at minimum intensity): sets RegData bit high. :contentReference[oaicite:8]{index=8}
int sx1509_effect_stop(sx1509_t *dev, uint8_t pin);

// For single-shot pulse: re-trigger by doing RegData high->low
int sx1509_effect_trigger(sx1509_t *dev, uint8_t pin);


#ifdef __cplusplus
} // extern "C"
#endif
