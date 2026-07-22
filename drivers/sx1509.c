/* sx1509.c */

#include "pico/stdlib.h"
#include "sx1509.h"
#include "ztmb/io_exp.h"
#include "ztmb/log.h"

#define SX1509_PIN_COUNT 16

#ifndef SX1509_FOSC_HZ_INTERNAL
#define SX1509_FOSC_HZ_INTERNAL 2000000u  // internal oscillator is 2MHz when selected :contentReference[oaicite:9]{index=9}
#endif

static inline bool sx1509_pin_valid(uint8_t pin) { return pin < SX1509_PIN_COUNT; }
static inline bool sx1509_pin_is_bank_b(uint8_t pin) { return pin >= 8; }
static inline uint8_t sx1509_pin_bit(uint8_t pin) { return (uint8_t)(1u << (pin & 7u)); }

static inline uint8_t sx1509_reg_for_bank(uint8_t reg_a, uint8_t reg_b, uint8_t pin) {
    return sx1509_pin_is_bank_b(pin) ? reg_b : reg_a;
}

// LED driver per-pin register addresses from Table 10 :contentReference[oaicite:5]{index=5}
static const uint8_t s_reg_ton[SX1509_PIN_COUNT] = {
    0x29, 0x2C, 0x2F, 0x32,
    0x35, 0x3A, 0x3F, 0x44,
    0x49, 0x4C, 0x4F, 0x52,
    0x55, 0x5A, 0x5F, 0x64
};

static inline uint8_t sx1509_reg_ion(uint8_t pin) { return (uint8_t)(s_reg_ton[pin] + 1u); }
static inline uint8_t sx1509_reg_off(uint8_t pin) { return (uint8_t)(s_reg_ton[pin] + 2u); }


// ----------------- Raw register access -----------------

int sx1509_reg_read8(sx1509_t *dev, uint8_t reg, uint8_t *val) {
    if (!val) return false;
    return bus_i2c_read(&dev->i2c_bus, reg, val);
}

int sx1509_reg_write8(sx1509_t *dev, uint8_t reg, uint8_t val) {
    return bus_i2c_write(&dev->i2c_bus, reg, val);
}

int sx1509_reg_read_n(sx1509_t *dev, uint8_t start_reg, uint8_t *buf, size_t len) {
    if (!buf || len == 0) return SX1509_ERR_BAD_ARG;

    // Robust even if auto-increment is disabled: read each reg individually.
    for (size_t i = 0; i < len; i++) {
        int rc = sx1509_reg_read8(dev, (uint8_t)(start_reg + i), &buf[i]);
        if (rc != SX1509_OK) return rc;
    }
    return SX1509_OK;
}

int sx1509_reg_write_n(sx1509_t *dev, uint8_t start_reg, const uint8_t *buf, size_t len) {
    if (!buf || len == 0) return SX1509_ERR_BAD_ARG;

    // Robust even if auto-increment is disabled: write each reg individually.
    for (size_t i = 0; i < len; i++) {
        int rc = sx1509_reg_write8(dev, (uint8_t)(start_reg + i), buf[i]);
        if (rc != SX1509_OK) return rc;
    }
    return SX1509_OK;
}

int sx1509_reg_read16_be(sx1509_t *dev, uint8_t reg_b, uint16_t *val) {
    if (!val) return SX1509_ERR_BAD_ARG;
    uint8_t b = 0, a = 0;
    int rc = sx1509_reg_read8(dev, reg_b, &b);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_read8(dev, (uint8_t)(reg_b + 1), &a);
    if (rc != SX1509_OK) return rc;
    *val = ((uint16_t)b << 8) | (uint16_t)a;
    return SX1509_OK;
}

int sx1509_reg_write16_be(sx1509_t *dev, uint8_t reg_b, uint16_t val) {
    uint8_t b = (uint8_t)(val >> 8);
    uint8_t a = (uint8_t)(val & 0xFF);
    int rc = sx1509_reg_write8(dev, reg_b, b);
    if (rc != SX1509_OK) return rc;
    return sx1509_reg_write8(dev, (uint8_t)(reg_b + 1), a);
}

int sx1509_update_bits(sx1509_t *dev, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t cur = 0;
    int e = sx1509_reg_read8(dev, reg, &cur);
    if (e != SX1509_OK) return e;
    uint8_t next = (uint8_t)((cur & ~mask) | (value & mask));
    if (next == cur) return SX1509_OK;
    return sx1509_reg_write8(dev, reg, next);
}

// ----------------- Reset / init -----------------

int sx1509_software_reset(sx1509_t *dev) {
    if (!dev) return SX1509_ERR_BAD_ARG;
    // Datasheet: write consecutively 0x12 then 0x34 to RegReset (0x7D).
    int rc = sx1509_reg_write8(dev, SX1509_REG_RESET, 0x12);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, SX1509_REG_RESET, 0x34);
    if (rc != SX1509_OK) return rc;
    busy_wait_ms(2);
    return SX1509_OK;
}


int sx1509_init(sx1509_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit) {

    if (!dev) return SX1509_ERR_BAD_ARG;
    log_printf("sx1509_init start\n");

    static const bus_i2c_config_t i2c_config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };
    if (!bus_i2c_init(&dev->i2c_bus, i2c, addr_7bit, EX_NONE, &i2c_config)) {
        return SX1509_ERR_BAD_ARG;
    }

    dev->present = false;

    // Software reset to known defaults
    int rc = sx1509_software_reset(dev);
    if (rc != SX1509_OK) return rc;

    // Probe: read a known register (DIR_A defaults to 0xFF after reset)
    uint8_t dira = 0;
    rc = sx1509_reg_read8(dev, SX1509_REG_DIR_A, &dira);
    if (rc != SX1509_OK) return rc;

    dev->present = true;

    // 1) Ensure auto-increment ON and interrupt autoclear ON (both bits = 0)
    //    RegMisc[1]=0 => auto-inc ON, RegMisc[0]=0 => autoclear interrupt source on RegData read
    rc = sx1509_reg_write8(dev, SX1509_REG_MISC, 0x00);
    if (rc != SX1509_OK) return rc;

    // 2) Disable LED driver engine on all pins
    rc = sx1509_reg_write8(dev, SX1509_REG_LED_DRIVER_ENABLE_A, 0x00);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, SX1509_REG_LED_DRIVER_ENABLE_B, 0x00);
    if (rc != SX1509_OK) return rc;

    // 3) Disable keypad engine + debounce by default
    rc = sx1509_reg_write8(dev, SX1509_REG_KEY_CONFIG_1, 0x00);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, SX1509_REG_KEY_CONFIG_2, 0x00);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_reg_write8(dev, SX1509_REG_DEBOUNCE_CONFIG, 0x00);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, SX1509_REG_DEBOUNCE_ENABLE_A, 0x00);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, SX1509_REG_DEBOUNCE_ENABLE_B, 0x00);
    if (rc != SX1509_OK) return rc;

    // 4) Enable internal 2MHz oscillator (useful if you later enable debounce/LED/keypad)
    //    - internal osc: RegClock[6:5]=10
    //    - OSCIO as input: bit4=0
    //    - OSCOUT divider: 0 (doesn't matter when OSCIO is input)
    rc = sx1509_clock_configure(dev, SX1509_CLOCK_OSC_SRC_INT_2MHZ, false, 0x00);
    if (rc != SX1509_OK) return rc;

    return SX1509_OK;
}

// ----------------- Helpers -----------------


static inline uint16_t sx1509_pin_mask(uint8_t pin) {
    return (uint16_t)(1u << pin);
}

// For "paired" registers: read 16-bit (B<<8)|A, update a single bit, write back.
static int sx1509_update_bit16(sx1509_t *dev, uint8_t reg_b, uint8_t pin, bool set) {
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    uint16_t v = 0;
    int rc = sx1509_reg_read16_be(dev, reg_b, &v);
    if (rc != SX1509_OK) return rc;

    uint16_t m = sx1509_pin_mask(pin);
    if (set) v |= m;
    else     v &= (uint16_t)~m;

    return sx1509_reg_write16_be(dev, reg_b, v);
}

// Read-modify-write for 2-bit sense field
static int sx1509_update_sense2b(sx1509_t *dev, uint8_t pin, sx1509_sense_t sense) {
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    uint8_t reg = 0;
    uint8_t shift = 0;

    if (pin <= 3) { reg = SX1509_REG_SENSE_LOW_A;  shift = (uint8_t)(pin * 2); }
    else if (pin <= 7) { reg = SX1509_REG_SENSE_HIGH_A; shift = (uint8_t)((pin - 4) * 2); }
    else if (pin <= 11) { reg = SX1509_REG_SENSE_LOW_B; shift = (uint8_t)((pin - 8) * 2); }
    else { reg = SX1509_REG_SENSE_HIGH_B; shift = (uint8_t)((pin - 12) * 2); }

    uint8_t v = 0;
    int rc = sx1509_reg_read8(dev, reg, &v);
    if (rc != SX1509_OK) return rc;

    v &= (uint8_t)~(0x3u << shift);
    v |= (uint8_t)((uint8_t)sense << shift);

    return sx1509_reg_write8(dev, reg, v);
}

// ----------------- GPIO API -----------------

int sx1509_pin_set_input(sx1509_t *dev, uint8_t pin) {
    // DIR bit: 1=input, 0=output
    return sx1509_update_bit16(dev, SX1509_REG_DIR_B, pin, true);
}

int sx1509_pin_set_output(sx1509_t *dev, uint8_t pin) {
    return sx1509_update_bit16(dev, SX1509_REG_DIR_B, pin, false);
}

int sx1509_pin_configure(sx1509_t *dev, uint8_t pin, uint32_t flags) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    // Direction
    if (flags & SX1509_PIN_OUTPUT) {
        int rc = sx1509_pin_set_output(dev, pin);
        if (rc != SX1509_OK) return rc;
    } else {
        int rc = sx1509_pin_set_input(dev, pin);
        if (rc != SX1509_OK) return rc;
    }

    // Pulls / modes / polarity / buffers / drive controls
    int rc;
    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_UP_B,       pin, (flags & SX1509_PIN_PULLUP) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_DOWN_B,     pin, (flags & SX1509_PIN_PULLDOWN) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_OPEN_DRAIN_B,    pin, (flags & SX1509_PIN_OPENDRAIN) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_POLARITY_B,      pin, (flags & SX1509_PIN_INVERT) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_INPUT_DISABLE_B, pin, (flags & SX1509_PIN_INPUT_DISABLE) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_LONG_SLEW_B,     pin, (flags & SX1509_PIN_LONG_SLEW) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_LOW_DRIVE_B,     pin, (flags & SX1509_PIN_LOW_DRIVE) != 0);
    if (rc != SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_HIGH_INPUT_B,    pin, (flags & SX1509_PIN_HIGH_INPUT) != 0);
    if (rc != SX1509_OK) return rc;

    return SX1509_OK;
}

int sx1509_port_read(sx1509_t *dev, uint16_t *value) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    return sx1509_reg_read16_be(dev, SX1509_REG_DATA_B, value);
}

int sx1509_port_write(sx1509_t *dev, uint16_t value) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    return sx1509_reg_write16_be(dev, SX1509_REG_DATA_B, value);
}

int sx1509_port_set_bits(sx1509_t *dev, uint16_t mask) {
    uint16_t v = 0;
    int rc = sx1509_port_read(dev, &v);
    if (rc != SX1509_OK) return rc;
    v |= mask;
    return sx1509_port_write(dev, v);
}

int sx1509_port_clear_bits(sx1509_t *dev, uint16_t mask) {
    uint16_t v = 0;
    int rc = sx1509_port_read(dev, &v);
    if (rc != SX1509_OK) return rc;
    v &= (uint16_t)~mask;
    return sx1509_port_write(dev, v);
}

int sx1509_digital_read(sx1509_t *dev, uint8_t pin, bool *level) {
    if (!level) return SX1509_ERR_BAD_ARG;
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    uint16_t v = 0;
    int rc = sx1509_port_read(dev, &v);
    if (rc != SX1509_OK) return rc;

    *level = (v & sx1509_pin_mask(pin)) != 0;
    return SX1509_OK;
}

int sx1509_digital_write(sx1509_t *dev, uint8_t pin, bool level) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    uint16_t v = 0;
    int rc = sx1509_port_read(dev, &v);
    if (rc != SX1509_OK) return rc;

    uint16_t m = sx1509_pin_mask(pin);
    if (level) v |= m;
    else       v &= (uint16_t)~m;

    return sx1509_port_write(dev, v);
}

int sx1509_digital_toggle(sx1509_t *dev, uint8_t pin) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    uint16_t v = 0;
    int rc = sx1509_port_read(dev, &v);
    if (rc != SX1509_OK) return rc;

    v ^= sx1509_pin_mask(pin);
    return sx1509_port_write(dev, v);
}

// ----------------- Interrupts -----------------

int sx1509_interrupt_configure(sx1509_t *dev, uint8_t pin, bool enable, sx1509_sense_t sense) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!sx1509_pin_valid(pin)) return SX1509_ERR_BAD_ARG;

    // Configure edge sensitivity (2-bit field in SENSE regs)
    int rc = sx1509_update_sense2b(dev, pin, sense);
    if (rc != SX1509_OK) return rc;

    // INTERRUPT_MASK bit: 0=enable interrupt, 1=mask (disable)
    bool mask_bit = !enable;
    rc = sx1509_update_bit16(dev, SX1509_REG_INTERRUPT_MASK_B, pin, mask_bit);
    if (rc != SX1509_OK) return rc;

    return SX1509_OK;
}

int sx1509_interrupt_get_source(sx1509_t *dev, uint16_t *source) {
    if (!source) return SX1509_ERR_BAD_ARG;
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    return sx1509_reg_read16_be(dev, SX1509_REG_INTERRUPT_SOURCE_B, source);
}

int sx1509_interrupt_clear_source(sx1509_t *dev, uint16_t mask) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    // Write-1-to-clear in InterruptSource registers
    return sx1509_reg_write16_be(dev, SX1509_REG_INTERRUPT_SOURCE_B, mask);
}

// ----------------- Clock -----------------

int sx1509_clock_configure(sx1509_t *dev, uint8_t osc_src_bits, bool oscio_output, uint8_t oscout_div) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;

    uint8_t v = 0;
    int rc = sx1509_reg_read8(dev, SX1509_REG_CLOCK, &v);
    if (rc != SX1509_OK) return rc;

    v &= (uint8_t)~(SX1509_CLOCK_OSC_SRC_MASK | SX1509_CLOCK_OSCIO_OUTPUT_BIT | SX1509_CLOCK_OSCOUT_DIV_MASK);
    v |= (uint8_t)(osc_src_bits & SX1509_CLOCK_OSC_SRC_MASK);
    if (oscio_output) v |= SX1509_CLOCK_OSCIO_OUTPUT_BIT;
    v |= (uint8_t)(oscout_div & SX1509_CLOCK_OSCOUT_DIV_MASK);

    return sx1509_reg_write8(dev, SX1509_REG_CLOCK, v);
}

// ----------------- Keypad (minimal) -----------------

int sx1509_keypad_read(sx1509_t *dev, uint8_t *keydata1, uint8_t *keydata2) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (!keydata1 || !keydata2) return SX1509_ERR_BAD_ARG;

    int rc = sx1509_reg_read8(dev, SX1509_REG_KEY_DATA_1, keydata1);
    if (rc != SX1509_OK) return rc;
    return sx1509_reg_read8(dev, SX1509_REG_KEY_DATA_2, keydata2);
}

// ----------------- LED driver register addressing -----------------

void sx1509_led_regs_for_pin(uint8_t pin,
                            uint8_t *reg_ton,
                            uint8_t *reg_ion,
                            uint8_t *reg_off,
                            uint8_t *reg_trise,
                            uint8_t *reg_tfall) {
    if (reg_ton)   *reg_ton = 0xFF;
    if (reg_ion)   *reg_ion = 0xFF;
    if (reg_off)   *reg_off = 0xFF;
    if (reg_trise) *reg_trise = 0xFF;
    if (reg_tfall) *reg_tfall = 0xFF;

    if (pin >= 16) return;

    // Layout per datasheet:
    // IO0..3  : 3 regs each (TOn, IOn, Off) starting at 0x29
    // IO4..7  : 5 regs each (TOn, IOn, Off, TRise, TFall) starting at 0x35
    // IO8..11 : 3 regs each starting at 0x49
    // IO12..15: 5 regs each starting at 0x55

    uint8_t base = 0xFF;
    bool fade = sx1509_pin_is_fade_capable(pin);

    if (pin <= 3) {
        base = (uint8_t)(0x29 + pin * 3);
        if (reg_ton) *reg_ton = base;
        if (reg_ion) *reg_ion = (uint8_t)(base + 1);
        if (reg_off) *reg_off = (uint8_t)(base + 2);
        return;
    }

    if (pin <= 7) {
        base = (uint8_t)(0x35 + (pin - 4) * 5);
        if (reg_ton) *reg_ton = base;
        if (reg_ion) *reg_ion = (uint8_t)(base + 1);
        if (reg_off) *reg_off = (uint8_t)(base + 2);
        if (fade) {
            if (reg_trise) *reg_trise = (uint8_t)(base + 3);
            if (reg_tfall) *reg_tfall = (uint8_t)(base + 4);
        }
        return;
    }

    if (pin <= 11) {
        base = (uint8_t)(0x49 + (pin - 8) * 3);
        if (reg_ton) *reg_ton = base;
        if (reg_ion) *reg_ion = (uint8_t)(base + 1);
        if (reg_off) *reg_off = (uint8_t)(base + 2);
        return;
    }

    // 12..15
    base = (uint8_t)(0x55 + (pin - 12) * 5);
    if (reg_ton) *reg_ton = base;
    if (reg_ion) *reg_ion = (uint8_t)(base + 1);
    if (reg_off) *reg_off = (uint8_t)(base + 2);
    if (fade) {
        if (reg_trise) *reg_trise = (uint8_t)(base + 3);
        if (reg_tfall) *reg_tfall = (uint8_t)(base + 4);
    }
}



// -------------------- PWM (LED driver engine) --------------------
static uint8_t sx1509_duty_pct_to_255_(uint8_t duty_pct) {
    if (duty_pct > 100) duty_pct = 100;
    // round to nearest
    uint16_t v = (uint16_t)duty_pct * 255u;
    return (uint8_t)(255 - ((v + 50u) / 100u));
}

int sx1509_pwm_enable(sx1509_t *dev, uint8_t clkdiv_code) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;

    // clkdiv_code: 1..7 enables ClkX, 0 disables (don’t allow 0 here)
    if (clkdiv_code < 1) clkdiv_code = 1;
    if (clkdiv_code > 7) clkdiv_code = 7;

    // 1) Ensure internal oscillator is on (your driver already has this helper)
    int rc = sx1509_clock_configure(dev, SX1509_CLOCK_OSC_SRC_INT_2MHZ, false, 0x00);
    if (rc != SX1509_OK) return rc;

    // 2) Enable LED driver clock ClkX via RegMisc[6:4]
    uint8_t misc = 0;
    rc = sx1509_reg_read8(dev, SX1509_REG_MISC, &misc);
    if (rc != SX1509_OK) return rc;

    misc &= (uint8_t)~SX1509_MISC_LED_CLKDIV_MASK;
    misc |= (uint8_t)((clkdiv_code << SX1509_MISC_LED_CLKDIV_SHIFT) & SX1509_MISC_LED_CLKDIV_MASK);

    return sx1509_reg_write8(dev, SX1509_REG_MISC, misc);
}

int sx1509_pwm_pin_start(sx1509_t *dev, uint8_t pin, uint8_t duty_pct) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (pin >= 16) return SX1509_ERR_BAD_ARG;

    // Matches the working IO9 function:
    // - open drain + input buffer disabled
    // - no internal pulls
    // - oscillator + ClkX enabled
    // - LED driver enable set
    // - TOn=0, Off=0, IOn=duty
    // - RegData LOW to start

    int rc = sx1509_pwm_enable(dev, 7); // you said you’re using 7; change if desired
    if (rc != SX1509_OK) return rc;

    rc = sx1509_pin_configure(dev, pin,
                              SX1509_PIN_OUTPUT |
                              SX1509_PIN_OPENDRAIN |
                              SX1509_PIN_INPUT_DISABLE);
    if (rc != SX1509_OK) return rc;

    // ensure internal pulls are OFF so your external pull-up wins
    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_UP_B, pin, false);
    if (rc != SX1509_OK) return rc;
    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_DOWN_B, pin, false);
    if (rc != SX1509_OK) return rc;

    // enable LED driver on this pin
    rc = sx1509_update_bit16(dev, SX1509_REG_LED_DRIVER_ENABLE_B, pin, true);
    if (rc != SX1509_OK) return rc;

    // program LED driver regs for this pin
    uint8_t reg_ton = 0xFF, reg_ion = 0xFF, reg_off = 0xFF, reg_trise = 0xFF, reg_tfall = 0xFF;
    sx1509_led_regs_for_pin(pin, &reg_ton, &reg_ion, &reg_off, &reg_trise, &reg_tfall);

    uint8_t duty255 = sx1509_duty_pct_to_255_(duty_pct);

    rc = sx1509_reg_write8(dev, reg_ton, 0x00);      // TOn = 0 (static)
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, reg_ion, duty255);   // IOn = duty (0..255)
    if (rc != SX1509_OK) return rc;
    rc = sx1509_reg_write8(dev, reg_off, 0x00);      // Off = 0 (static)
    if (rc != SX1509_OK) return rc;

    // start LED driver: RegData bit LOW
    return sx1509_digital_write(dev, pin, false);
}

int sx1509_pwm_pin_set_duty(sx1509_t *dev, uint8_t pin, uint8_t duty_pct) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (pin >= 16) return SX1509_ERR_BAD_ARG;

    uint8_t reg_ton = 0xFF, reg_ion = 0xFF, reg_off = 0xFF, reg_trise = 0xFF, reg_tfall = 0xFF;
    sx1509_led_regs_for_pin(pin, &reg_ton, &reg_ion, &reg_off, &reg_trise, &reg_tfall);

    return sx1509_reg_write8(dev, reg_ion, sx1509_duty_pct_to_255_(duty_pct));
}

int sx1509_pwm_pin_stop(sx1509_t *dev, uint8_t pin) {
    if (!dev || !dev->present) return SX1509_ERR_NOT_PRESENT;
    if (pin >= 16) return SX1509_ERR_BAD_ARG;

    // stop: RegData HIGH (releases open drain)
    int rc = sx1509_digital_write(dev, pin, true);
    if (rc != SX1509_OK) return rc;

    // disable LED driver enable bit
    return sx1509_update_bit16(dev, SX1509_REG_LED_DRIVER_ENABLE_B, pin, false);
}


int sx1509_pwm9_start_10pct(sx1509_t *dev)
{
    int rc;

    // 1) Configure pin 9 for LED-driver style operation (open drain, input buffer disabled, no pulls)
    rc = sx1509_pin_configure(dev, 9,
                              SX1509_PIN_OUTPUT |
                              SX1509_PIN_OPENDRAIN |
                              SX1509_PIN_INPUT_DISABLE);
    if (rc!=SX1509_OK) return rc;

    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_UP_B,   9, false); if (rc!=SX1509_OK) return rc;
    rc = sx1509_update_bit16(dev, SX1509_REG_PULL_DOWN_B, 9, false); if (rc!=SX1509_OK) return rc;

    // 2) Enable internal osc (2 MHz) and enable ClkX (RegMisc[6:4] must be non-zero; 0 disables LED clock)
    // ClkX = fOSC / 2^(RegMisc[6:4]-1)  (when not OFF) :contentReference[oaicite:3]{index=3}
    rc = sx1509_clock_configure(dev, SX1509_CLOCK_OSC_SRC_INT_2MHZ, false, 0x00);
    if (rc!=SX1509_OK) return rc;

    // divider code = 7 => ClkX = 2MHz / 64 = 31.25kHz (and intensity PWM is derived from that)
    uint8_t misc;
    rc = sx1509_reg_read8(dev, SX1509_REG_MISC, &misc); if (rc!=SX1509_OK) return rc;
    misc &= (uint8_t)~(0x7u << 4);
    misc |= (uint8_t)(7u << 4);
    rc = sx1509_reg_write8(dev, SX1509_REG_MISC, misc); if (rc!=SX1509_OK) return rc;

    // 3) Enable LED driver on IO9
    rc = sx1509_update_bit16(dev, SX1509_REG_LED_DRIVER_ENABLE_B, 9, true);
    if (rc!=SX1509_OK) return rc;

    // 4) Program IO9 LED registers directly (addresses from datasheet)
    // 10% duty-ish => 0.10 * 255 ≈ 26
    rc = sx1509_reg_write8(dev, 0x4C, 0x00); if (rc!=SX1509_OK) return rc; // TOn9
    rc = sx1509_reg_write8(dev, 0x4D, 240);   if (rc!=SX1509_OK) return rc; // IOn9
    rc = sx1509_reg_write8(dev, 0x4E, 0x00); if (rc!=SX1509_OK) return rc; // Off9 (intensity=0, time=0)

    // 5) Start LED driver: set RegData bit low (datasheet tutorial step) :contentReference[oaicite:4]{index=4}
    rc = sx1509_digital_write(dev, 9, false);

    uint8_t ion9;
    sx1509_reg_read8(dev, 0x4D, &ion9);  // should be ~26 for 10%
    ion9 = ion9; // silence unused variable warning

    return rc;
}


// -------------------- Effects --------------------

static uint32_t sx1509_calc_clkx_hz(uint8_t clkx_div_code)
{
    // ClkX = fOSC / 2^(RegMisc[6:4]-1) for nonzero div_code :contentReference[oaicite:11]{index=11}
    if (clkx_div_code == 0) return 0;
    uint8_t shift = (uint8_t)(clkx_div_code - 1);
    return (uint32_t)(SX1509_FOSC_HZ_INTERNAL >> shift);
}

static uint8_t sx1509_intensity_from_brightness(uint8_t brightness, bool invert)
{
    return invert ? (uint8_t)(255u - brightness) : brightness;
}

static uint8_t sx1509_calc_tcode_ms(uint16_t ms, uint32_t clkx_hz)
{
    // TOn/TOff encoding:
    // 1..15: T = 64 * code * (255/ClkX)
    // 16..31: T = 512 * code * (255/ClkX) :contentReference[oaicite:12]{index=12}
    if (ms == 0 || clkx_hz == 0) return 0;

    uint64_t t_ns    = (uint64_t)ms * 1000000ull;
    uint64_t tick_ns = (255000000000ull) / (uint64_t)clkx_hz; // (255/ClkX) seconds in ns

    uint64_t denom1 = 64ull  * tick_ns;
    uint64_t denom2 = 512ull * tick_ns;

    uint64_t c1 = (t_ns + denom1/2) / denom1;
    if (c1 < 1)  c1 = 1;
    if (c1 > 15) c1 = 15;
    uint64_t t1 = denom1 * c1;

    uint64_t c2 = (t_ns + denom2/2) / denom2;
    if (c2 < 16) c2 = 16;
    if (c2 > 31) c2 = 31;
    uint64_t t2 = denom2 * c2;

    uint64_t d1 = (t1 > t_ns) ? (t1 - t_ns) : (t_ns - t1);
    uint64_t d2 = (t2 > t_ns) ? (t2 - t_ns) : (t_ns - t2);

    return (uint8_t)((d2 < d1) ? c2 : c1);
}

static uint8_t sx1509_calc_fadecode_ms(uint16_t ms, uint32_t clkx_hz, uint16_t delta_i)
{
    // TRise/TFall encoding:
    // 1..15: T = delta_i * code * (255/ClkX)
    // 16..31: T = 16 * delta_i * code * (255/ClkX) :contentReference[oaicite:13]{index=13}
    if (ms == 0 || clkx_hz == 0 || delta_i == 0) return 0;

    uint64_t t_ns    = (uint64_t)ms * 1000000ull;
    uint64_t tick_ns = (255000000000ull) / (uint64_t)clkx_hz;

    uint64_t denom1 = (uint64_t)delta_i * tick_ns;
    uint64_t denom2 = 16ull * (uint64_t)delta_i * tick_ns;

    uint64_t c1 = (t_ns + denom1/2) / denom1;
    if (c1 < 1)  c1 = 1;
    if (c1 > 15) c1 = 15;
    uint64_t t1 = denom1 * c1;

    uint64_t c2 = (t_ns + denom2/2) / denom2;
    if (c2 < 16) c2 = 16;
    if (c2 > 31) c2 = 31;
    uint64_t t2 = denom2 * c2;

    uint64_t d1 = (t1 > t_ns) ? (t1 - t_ns) : (t_ns - t1);
    uint64_t d2 = (t2 > t_ns) ? (t2 - t_ns) : (t_ns - t2);

    return (uint8_t)((d2 < d1) ? c2 : c1);
}

static int sx1509_led_clock_enable(sx1509_t *dev, uint8_t clkx_div_code)
{
    // Enable internal 2MHz oscillator (RegClock[6:5] = 10) :contentReference[oaicite:14]{index=14}
    uint8_t clk = 0;
    int rc = sx1509_reg_read8(dev, SX1509_REG_CLOCK, &clk);
    if (rc) return rc;
    clk = (uint8_t)((clk & ~(0x3u << 5)) | (0x2u << 5));
    rc = sx1509_reg_write8(dev, SX1509_REG_CLOCK, clk);
    if (rc) return rc;

    // Set LED driver clock divider RegMisc[6:4] (0 disables LED driver clock) :contentReference[oaicite:15]{index=15}
    uint8_t misc = 0;
    rc = sx1509_reg_read8(dev, SX1509_REG_MISC, &misc);
    if (rc) return rc;

    misc = (uint8_t)((misc & ~(0x7u << 4)) | ((clkx_div_code & 0x7u) << 4));
    return sx1509_reg_write8(dev, SX1509_REG_MISC, misc);
}

int sx1509_effect_configure(sx1509_t *dev, const sx1509_effect_cfg_t *cfg)
{
    if (!dev || !cfg) return -1;
    if (cfg->pin > 15) return -2;
    if (cfg->clkx_div_code == 0) return -3;

    // 1) Prep pin for LED driver usage (datasheet tutorial steps) :contentReference[oaicite:16]{index=16}
    int rc = 0;
    rc |= sx1509_update_bit16(dev, SX1509_REG_INPUT_DISABLE_B, cfg->pin, true);
    rc |= sx1509_update_bit16(dev, SX1509_REG_PULL_UP_B,       cfg->pin, false);
    rc |= sx1509_update_bit16(dev, SX1509_REG_PULL_DOWN_B,     cfg->pin, false);
    rc |= sx1509_update_bit16(dev, SX1509_REG_OPEN_DRAIN_B,    cfg->pin, cfg->open_drain);
    rc |= sx1509_update_bit16(dev, SX1509_REG_DIR_B,           cfg->pin, false); // output
    if (rc) return rc;

    // 2) Enable oscillator + LED driver clock (ClkX) :contentReference[oaicite:17]{index=17}
    rc = sx1509_led_clock_enable(dev, cfg->clkx_div_code);
    if (rc) return rc;

    // 3) Set linear/log mode for fade-capable pins’ bank via RegMisc bits :contentReference[oaicite:18]{index=18}
    if (sx1509_pin_is_fade_capable(cfg->pin)) {
        uint8_t misc = 0;
        rc = sx1509_reg_read8(dev, SX1509_REG_MISC, &misc);
        if (rc) return rc;

        if (cfg->pin >= 12) {
            // Bank B fading-capable (IO15-12): RegMisc bit7
            if (cfg->log_mode) misc |=  (1u << 7);
            else               misc &= ~(1u << 7);
        } else {
            // Bank A fading-capable (IO7-4): RegMisc bit3
            if (cfg->log_mode) misc |=  (1u << 3);
            else               misc &= ~(1u << 3);
        }
        rc = sx1509_reg_write8(dev, SX1509_REG_MISC, misc);
        if (rc) return rc;
    }

    // 4) Enable LED driver on this IO :contentReference[oaicite:19]{index=19}
    rc = sx1509_update_bit16(dev, SX1509_REG_LED_DRIVER_ENABLE_B, cfg->pin, true);
    if (rc) return rc;

    // 5) Program per-pin LED regs
    uint8_t reg_ton=0xFF, reg_ion=0xFF, reg_off=0xFF, reg_trise=0xFF, reg_tfall=0xFF;
    sx1509_led_regs_for_pin(cfg->pin, &reg_ton, &reg_ion, &reg_off, &reg_trise, &reg_tfall);

    uint32_t clkx_hz = sx1509_calc_clkx_hz(cfg->clkx_div_code);

    uint8_t ton_code  = 0;
    uint8_t toff_code = 0;

    if (cfg->mode == SX1509_EFFECT_SOLID) {
        ton_code  = 0; // static mode invoked when TOn=0 :contentReference[oaicite:20]{index=20}
        toff_code = 0;
    } else if (cfg->mode == SX1509_EFFECT_PULSE) {
        ton_code  = sx1509_calc_tcode_ms(cfg->on_ms, clkx_hz);
        toff_code = 0; // single-shot when TOff=0 :contentReference[oaicite:21]{index=21}
    } else {
        // BLINK / BREATHE
        ton_code  = sx1509_calc_tcode_ms(cfg->on_ms,  clkx_hz);
        toff_code = sx1509_calc_tcode_ms(cfg->off_ms, clkx_hz);
        if (ton_code == 0) ton_code = 1;
        if (toff_code == 0) toff_code = 1;
    }

    uint8_t ion_val = sx1509_intensity_from_brightness(cfg->on_brightness_0_255, cfg->invert_brightness);
    uint8_t off_int = (uint8_t)(cfg->off_intensity_0_7 & 0x7u);
    uint8_t off_val = (uint8_t)((toff_code << 3) | off_int); // RegOff[7:3]=TOff, [2:0]=IOff :contentReference[oaicite:22]{index=22}

    if (reg_ton != 0xFF) { rc = sx1509_reg_write8(dev, reg_ton, (uint8_t)(ton_code & 0x1Fu)); if (rc) return rc; }
    if (reg_ion != 0xFF) { rc = sx1509_reg_write8(dev, reg_ion, ion_val);                      if (rc) return rc; }
    if (reg_off != 0xFF) { rc = sx1509_reg_write8(dev, reg_off, off_val);                      if (rc) return rc; }

    // Fades only if supported + requested
    if (sx1509_pin_is_fade_capable(cfg->pin) && (cfg->mode == SX1509_EFFECT_BREATHE)) {
        // delta_i = RegIOn - (4*RegOff[2:0]) per datasheet :contentReference[oaicite:23]{index=23}
        uint16_t ioff_lin = (uint16_t)(4u * off_int);
        uint16_t delta_i  = (ion_val > ioff_lin) ? (uint16_t)(ion_val - ioff_lin) : 0;

        uint8_t tr = sx1509_calc_fadecode_ms(cfg->rise_ms, clkx_hz, delta_i);
        uint8_t tf = sx1509_calc_fadecode_ms(cfg->fall_ms, clkx_hz, delta_i);

        if (reg_trise != 0xFF) { rc = sx1509_reg_write8(dev, reg_trise, (uint8_t)(tr & 0x1Fu)); if (rc) return rc; }
        if (reg_tfall != 0xFF) { rc = sx1509_reg_write8(dev, reg_tfall, (uint8_t)(tf & 0x1Fu)); if (rc) return rc; }
    } else {
        // Ensure fades disabled if present
        if (reg_trise != 0xFF) (void)sx1509_reg_write8(dev, reg_trise, 0x00);
        if (reg_tfall != 0xFF) (void)sx1509_reg_write8(dev, reg_tfall, 0x00);
    }

    return 0;
}

int sx1509_effect_start(sx1509_t *dev, uint8_t pin)
{
    // Start LED engine: RegData bit low :contentReference[oaicite:24]{index=24}
    return sx1509_update_bit16(dev, SX1509_REG_DATA_B, pin, false);
}

int sx1509_effect_stop(sx1509_t *dev, uint8_t pin)
{
    // Hold at minimum intensity: RegData bit high :contentReference[oaicite:25]{index=25}
    return sx1509_update_bit16(dev, SX1509_REG_DATA_B, pin, true);
}

int sx1509_effect_trigger(sx1509_t *dev, uint8_t pin)
{
    // For single-shot, a reliable retrigger is: set high then low
    int rc = sx1509_effect_stop(dev, pin);
    if (rc) return rc;
    return sx1509_effect_start(dev, pin);
}
