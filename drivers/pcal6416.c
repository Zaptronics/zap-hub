// pcal6416.c
#include <stdio.h>
#include "pcal6416.h"
#include "pico/stdlib.h"
#include "ztmb/log.h"
#include "ztmb/io_exp.h"

// --- Low-level helpers ------------------------------------------------------

static bool pcal6416a_write_reg(pcal6416a_t *dev, uint8_t reg, uint8_t value) {
    return bus_i2c_write(&dev->i2c_bus, reg, value);
}

static bool pcal6416a_read_reg(pcal6416a_t *dev, uint8_t reg, uint8_t *value) {
    if (!value) return false;
    return bus_i2c_read(&dev->i2c_bus, reg, value);
}

// --- Public API -------------------------------------------------------------

bool pcal6416a_init(pcal6416a_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit) {
    log_printf("pcal6416a_init start\n");

    static const bus_i2c_config_t i2c_config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };
    if (!bus_i2c_init(&dev->i2c_bus, i2c, addr_7bit, EX_NONE, &i2c_config)) {
        return false;
    }
    //dev->i2c_bus.i2c    = i2c;
    //dev->i2c_bus.addr   = addr_7bit;

    log_printf("pcal6416a_init dev reg low\n");
    // All outputs initial LOW
    dev->out0 = 0x00;
    dev->out1 = 0x00;

    // Config bits: 1 = input, 0 = output
    //
    // Inputs: P0.4, P0.6
    //  P0 bits: [7:0] = P0.7..P0.0
    //           bit4 -> P0.4, bit6 -> P0.6
    log_printf("pcal6416a_init dev reg cfg0\n");
    dev->cfg0 = (1u << 4) | (1u << 6);      // 0b0101_0000 = 0x50

    // Inputs: P1.0, P1.2, P1.5, P1.7
    //           bit0 -> P1.0, bit2 -> P1.2, bit5 -> P1.5, bit7 -> P1.7
    log_printf("pcal6416a_init dev reg cfg1\n");
    dev->cfg1 = (1u << 0) | (1u << 2) | (1u << 5) | (1u << 7); // 0b1010_0101

    // 1) Ensure outputs are configured as push-pull (not open-drain).
    //    ODEN0=0 => port 0 push-pull, ODEN1=0 => port 1 push-pull. :contentReference[oaicite:3]{index=3}
    log_printf("PCAL6416A_REG_OUTCFG\n");
    if (!pcal6416a_write_reg(dev, PCAL6416A_REG_OUTCFG, 0x00))
        return false;

    // 2) Program the output registers LOW *before* switching pins to outputs,
    //    otherwise they'd briefly come up HIGH because default is 0xFF. :contentReference[oaicite:4]{index=4}
    log_printf("PCAL6416A_REG_OUTPUT0\n");
    if (!pcal6416a_write_reg(dev, PCAL6416A_REG_OUTPUT0, dev->out0)) return false;
    log_printf("PCAL6416A_REG_OUTPUT1\n");
    if (!pcal6416a_write_reg(dev, PCAL6416A_REG_OUTPUT1, dev->out1)) return false;

    // 3) Set directions (inputs/outputs as requested).
    log_printf("PCAL6416A_REG_CONFIG0\n");
    if (!pcal6416a_write_reg(dev, PCAL6416A_REG_CONFIG0, dev->cfg0)) return false;
    log_printf("PCAL6416A_REG_CONFIG1\n");
    if (!pcal6416a_write_reg(dev, PCAL6416A_REG_CONFIG1, dev->cfg1)) return false;

    // Optionally, you could also explicitly clear polarity inversion:
    // pcal6416a_write_reg(dev, PCAL6416A_REG_POLARITY0, 0x00);
    // pcal6416a_write_reg(dev, PCAL6416A_REG_POLARITY1, 0x00);

    return true;
}

bool pcal6416a_write_pin(pcal6416a_t *dev, uint8_t pin, bool level) {
    if (pin > 15) return false;

    uint8_t port = (pin < 8) ? 0 : 1;
    uint8_t bit  = pin & 0x07u;
    uint8_t mask = 1u << bit;

    uint8_t *out_reg = (port == 0) ? &dev->out0 : &dev->out1;
    uint8_t  reg_addr = (port == 0) ? PCAL6416A_REG_OUTPUT0 : PCAL6416A_REG_OUTPUT1;

    if (level)
        *out_reg |= mask;
    else
        *out_reg &= ~mask;

    return pcal6416a_write_reg(dev, reg_addr, *out_reg);
}

bool pcal6416a_set_pin_direction(pcal6416a_t *dev, uint8_t pin, bool input) {
    if (!dev || pin > 15) return false;

    uint8_t port = (pin < 8) ? 0u : 1u;
    uint8_t bit = pin & 0x07u;
    uint8_t mask = (uint8_t)(1u << bit);
    uint8_t *cfg = (port == 0u) ? &dev->cfg0 : &dev->cfg1;
    uint8_t reg = (port == 0u) ? PCAL6416A_REG_CONFIG0 : PCAL6416A_REG_CONFIG1;

    if (input) {
        *cfg |= mask;
    } else {
        *cfg &= (uint8_t)~mask;
    }

    return pcal6416a_write_reg(dev, reg, *cfg);
}

bool pcal6416a_read_pin(pcal6416a_t *dev, uint8_t pin, bool *level_out) {
    if (!level_out || pin > 15) return false;

    uint8_t port = (pin < 8) ? 0 : 1;
    uint8_t bit  = pin & 0x07u;
    uint8_t mask = 1u << bit;

    uint8_t reg_addr = (port == 0) ? PCAL6416A_REG_INPUT0 : PCAL6416A_REG_INPUT1;
    uint8_t val;

    if (!pcal6416a_read_reg(dev, reg_addr, &val)) return false;

    *level_out = (val & mask) != 0;
    return true;
}

bool pcal6416a_read_all(pcal6416a_t *dev, uint16_t *value_out) {
    if (!value_out) return false;

    uint8_t in0, in1;
    if (!pcal6416a_read_reg(dev, PCAL6416A_REG_INPUT0, &in0)) return false;
    if (!pcal6416a_read_reg(dev, PCAL6416A_REG_INPUT1, &in1)) return false;

    *value_out = (uint16_t)in0 | ((uint16_t)in1 << 8);
    return true;
}
