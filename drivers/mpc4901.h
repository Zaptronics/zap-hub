// mpc4901.h - driver for Microchip MCP4901 8-bit DAC (SPI interface)
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ztmb/bus.h"

// --- SPI settings for MCP49x1 family ---
// MCP4901 uses SPI mode 0 (CPOL=0, CPHA=0), MSB first. Max clk ~20 MHz.
#define MCP4901_SPI_BAUD  (1000u * 1000u)

// Control bit fields for MCP49x1 (single-channel variant)
// [15]   : Don't care (channel select on dual parts) -> keep 0
// [14]BUF: 1=Vref buffered, 0=unbuffered
// [13]GA : 1=1x (Vout = D/256 * Vref), 0=2x
// [12]SHDN: 1=active, 0=shutdown (output goes Hi-Z via resistive net)
// [11:4] : 8-bit data (for MCP4901), left-aligned
// [3:0]  : don't care (0)
typedef struct {
    bus_spi_t spi_bus;
    bool ref_buffered;
    bool gain_1x;
} psu_tec_t;

bool mcp4901_init(psu_tec_t *ctx);
bool mcp4901_set_raw(psu_tec_t *dev, uint8_t code);
bool mcp4901_shutdown(psu_tec_t *dev);
bool mcp4901_set_then_enable(psu_tec_t *dev, uint8_t code, uint32_t settle_us);
bool mcp4901_set_enable(psu_tec_t *dev, bool enable_high);
bool mcp4901_set_millivolts(psu_tec_t *dev,
                            uint32_t vref_mV,
                            uint32_t vout_mV,
                            uint8_t *written_code);
