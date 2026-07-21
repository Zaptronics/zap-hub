#pragma once

#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

// -------------------------------
// Low-level SPI hook
// -------------------------------
// You said SPI is already working.
// Provide a single full-duplex transfer hook.
// The driver will call this with small buffers.
//
// Return true on success.
typedef bool (*ad7124_spi_xfer_fn)(const uint8_t *tx, uint8_t *rx, size_t len, void *user);


#ifndef AD7124_AIN_R
#define AD7124_AIN_R 15
#endif

#ifndef AD7124_AIN_RTD_CJ
#define AD7124_AIN_RTD_CJ 6
#endif

#ifndef AD7124_AIN_RTD0_BASE
#define AD7124_AIN_RTD0_BASE 7
#endif

#define AD7124_RTD_MAX 8

// -------------------------------
// Register addresses (Table 64 summary)
// -------------------------------
typedef enum {
    AD7124_REG_STATUS        = 0x00,
    AD7124_REG_ADC_CONTROL   = 0x01,
    AD7124_REG_DATA          = 0x02,
    AD7124_REG_IO_CONTROL_1  = 0x03,
    AD7124_REG_IO_CONTROL_2  = 0x04,
    AD7124_REG_ID            = 0x05,
    AD7124_REG_ERROR         = 0x06,
    AD7124_REG_ERROR_EN      = 0x07,
    AD7124_REG_MCLK_COUNT    = 0x08,

    AD7124_REG_CHANNEL_0     = 0x09, // ... through 0x18
    AD7124_REG_CONFIG_0      = 0x19, // ... through 0x20
    AD7124_REG_FILTER_0      = 0x21, // ... through 0x28
    AD7124_REG_OFFSET_0      = 0x29, // ... through 0x30
    AD7124_REG_GAIN_0        = 0x31  // ... through 0x38
} ad7124_reg_t;

// -------------------------------
// Helpers for indexed register blocks
// -------------------------------
static inline ad7124_reg_t AD7124_REG_CHANNEL(uint8_t ch) { return (ad7124_reg_t)(0x09 + ch); }
static inline ad7124_reg_t AD7124_REG_CONFIG(uint8_t s)   { return (ad7124_reg_t)(0x19 + s); }
static inline ad7124_reg_t AD7124_REG_FILTER(uint8_t s)   { return (ad7124_reg_t)(0x21 + s); }
static inline ad7124_reg_t AD7124_REG_OFFSET(uint8_t s)   { return (ad7124_reg_t)(0x29 + s); }
static inline ad7124_reg_t AD7124_REG_GAIN(uint8_t s)     { return (ad7124_reg_t)(0x31 + s); }



// -------------------------------
// Enumerations reflecting bitfields
// -------------------------------

typedef enum {
    AD7124_POWER_LOW = 0,
    AD7124_POWER_MID = 1,
    AD7124_POWER_FULL = 2
} ad7124_power_mode_t;

typedef enum {
    AD7124_MODE_CONTINUOUS = 0x0,
    AD7124_MODE_SINGLE     = 0x1,
    AD7124_MODE_STANDBY    = 0x2,
    AD7124_MODE_POWERDOWN  = 0x3,
    AD7124_MODE_IDLE       = 0x4,
    AD7124_MODE_INT_ZERO_CAL = 0x5,
    AD7124_MODE_INT_FULL_CAL = 0x6,
    AD7124_MODE_SYS_ZERO_CAL = 0x7,
    AD7124_MODE_SYS_FULL_CAL = 0x8
} ad7124_operating_mode_t;

typedef enum {
    AD7124_REF_REFIN1 = 0,  // REFIN1+/REFIN1-
    AD7124_REF_REFIN2 = 1,  // REFIN2+/REFIN2-
    AD7124_REF_INTERNAL = 2,
    AD7124_REF_AVDD = 3
} ad7124_ref_sel_t;

typedef enum {
    AD7124_PGA_1   = 0,
    AD7124_PGA_2   = 1,
    AD7124_PGA_4   = 2,
    AD7124_PGA_8   = 3,
    AD7124_PGA_16  = 4,
    AD7124_PGA_32  = 5,
    AD7124_PGA_64  = 6,
    AD7124_PGA_128 = 7
} ad7124_pga_t;

typedef enum {
    AD7124_FILTER_SINC4 = 0,
    AD7124_FILTER_SINC3 = 2,
    AD7124_FILTER_FAST_SINC4 = 4,
    AD7124_FILTER_FAST_SINC3 = 5,
    AD7124_FILTER_POST = 7
} ad7124_filter_type_t;

// Excitation current value encoding for IOUT0/1 (IO_CONTROL_1)
typedef enum {
    AD7124_IOUT_OFF = 0,
    AD7124_IOUT_50UA = 1,
    AD7124_IOUT_100UA = 2,
    AD7124_IOUT_250UA = 3,
    AD7124_IOUT_500UA = 4,
    AD7124_IOUT_750UA = 5,
    AD7124_IOUT_1000UA = 6,
    AD7124_IOUT_0_1UA = 7
} ad7124_iout_val_t;

// -------------------------------
// Device instance
// -------------------------------
typedef struct {
    bus_spi_t spi_bus;
    // cached interface choices
    bool data_status_enabled;
} ad7124_dev_t;

// -------------------------------
// Public API
// -------------------------------

void ad7124_init(ad7124_dev_t *ctx);

bool ad7124_reset(ad7124_dev_t *dev);
bool ad7124_read_id(ad7124_dev_t *dev, uint8_t *id_out);

// Raw register access (width fixed by helper functions)
bool ad7124_read_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t *val);
bool ad7124_read_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t *val);
bool ad7124_read_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t *val);
bool ad7124_write_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t val);
bool ad7124_write_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t val);
bool ad7124_write_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t val);

// High-level configuration
bool ad7124_config_adc_control(ad7124_dev_t *dev,
                               ad7124_power_mode_t pwr,
                               ad7124_operating_mode_t mode,
                               bool data_status,
                               bool ref_en);

bool ad7124_config_setup(ad7124_dev_t *dev, uint8_t setup,
                         ad7124_ref_sel_t ref_sel,
                         bool bipolar,
                         ad7124_pga_t pga,
                         bool ain_bufp, bool ain_bufm,
                         bool ref_bufp, bool ref_bufm,
                         uint8_t burnout /*0..3*/);

bool ad7124_config_filter(ad7124_dev_t *dev, uint8_t setup,
                          ad7124_filter_type_t filter,
                          uint8_t post_filter /*0..7*/,
                          bool rej60,
                          bool single_cycle,
                          uint16_t fs /*1..2047*/);

bool ad7124_config_channel(ad7124_dev_t *dev, uint8_t ch,
                           bool enable,
                           uint8_t setup,
                           uint8_t ainp,
                           uint8_t ainm);

bool ad7124_set_iout(ad7124_dev_t *dev,
                     ad7124_iout_val_t iout0, uint8_t iout0_ain,
                     ad7124_iout_val_t iout1, uint8_t iout1_ain,
                     bool close_pdsw);

// Sampling helpers
bool ad7124_wait_ready(ad7124_dev_t *dev, uint32_t timeout_us);
bool ad7124_read_sample(ad7124_dev_t *dev, uint32_t *code, uint8_t *ch_active);

// Convenience: your board map
bool ad7124_init_temp_frontend(ad7124_dev_t *dev,
                               bool use_internal_ref_for_tc,
                               uint16_t fs_tc,
                               uint16_t fs_rtd,
                               uint8_t rtd_count);

// -------------------------------
// Small local helpers
// -------------------------------

static inline double ad7124_rtd_resistance_from_code_ratiometric(uint32_t code, double r_ref_ohms)
{
        // Assumes:
    // - Unipolar
    // - PGA = 1
    // - AINP/AINM measures RTD voltage
    // - REFIN1 measures reference resistor voltage
    //
    // Then:
    // Vin/Vref = code / fullscale
    // Rrtd = Rref * Vin/Vref
    //
    const double full = 16777215.0; // 0xFFFFFF
    double vin_over_vref = (double)code / full;
    return r_ref_ohms * vin_over_vref;
    //double r = ad7124_rtd_resistance_from_code_ratiometric(code, 2000.0);
}

static bool ad7124_set_mode_only(ad7124_dev_t *dev, ad7124_operating_mode_t mode)
{
    uint16_t ctrl = 0;
    if (!ad7124_read_reg16(dev, AD7124_REG_ADC_CONTROL, &ctrl)) return false;

    ctrl &= ~(0xFu << 2);
    ctrl |= (uint16_t)((mode & 0xF) << 2);

    return ad7124_write_reg16(dev, AD7124_REG_ADC_CONTROL, ctrl);
}

static inline uint8_t ad7124_logical_index_from_rtd_top_ain(uint8_t ain)
{
    if (ain >= 7 && ain <= 14) return (uint8_t)(ain - 7); // RTD0..7
    if (ain == 6) return 8;  // CJ (choose your own convention)
    return 0xFF;
}

static inline uint8_t ad7124_logical_index_from_channel(uint8_t ch)
{
    if (ch >= 4 && ch <= 11) return (uint8_t)(ch - 4);
    if (ch == 3) return 8;   // CJ
    return 0xFF;
}
// -------------------------------
// Config check report
// -------------------------------
typedef struct {
    bool ok;

    // per-item flags
    bool adc_control_ok;
    bool setup_rtd_ok;
    bool filter_rtd_ok;
    bool ch_cj_ok;
    bool ch_rtd_ok[AD7124_RTD_MAX];

    // readback values (useful for debug prints)
    uint16_t adc_control;
    uint16_t config_rtd;   // CONFIG_1 expected
    uint32_t filter_rtd;   // FILTER_1 expected
    uint16_t ch_cj;
    uint16_t ch_rtd[AD7124_RTD_MAX];
} ad7124_sanity_report_t;

// Validate the expected RTD topology + setup.
// - expected_rtd_setup is the setup index you use for RTDs (likely 1)
// - expected_rtd_count 0..8
bool ad7124_sanity_check_rtd(ad7124_dev_t *dev,
                             uint8_t expected_rtd_setup,
                             uint8_t expected_rtd_count,
                             ad7124_sanity_report_t *out);

// -------------------------------
// RTD scan result
// -------------------------------
typedef struct {
    uint8_t logical_index; // 0..7 where 0==CJ if you want, or use separate flag
    uint8_t ad7124_channel;
    uint8_t ainp;
    uint8_t ainm;
    uint32_t code;
    double resistance_ohms;
    bool ok;
} ad7124_rtd_sample_t;

// Scan CJ + RTD0..(count-1)
// - r_ref_ohms should be 2000.0 for your R5
// - timeout_us applies per-sample
bool ad7124_scan_rtds(ad7124_dev_t *dev,
                      uint8_t rtd_setup,
                      uint8_t rtd_count,
                      double r_ref_ohms,
                      uint32_t timeout_us,
                      ad7124_rtd_sample_t *cj_out,
                      ad7124_rtd_sample_t *rtd_out_array,
                      size_t rtd_out_array_len);

// -------------------------------
// Safe IOUT strategy helpers
// -------------------------------

// Enables excitation for exactly one RTD top node at a time.
// This will:
// 1) Set IOUT0 to the selected AIN pin
// 2) Turn IOUT1 OFF
// 3) Optionally close PDSW
// 4) Leave other IOUT routing disabled
bool ad7124_enable_safe_iout_for_rtd(ad7124_dev_t *dev,
                                     ad7124_iout_val_t iout,
                                     uint8_t rtd_top_ain,
                                     bool close_pdsw);

// Turns both IOUTs off.
bool ad7124_disable_iouts(ad7124_dev_t *dev);

// High-level "excite + single read + stop excite" for one RTD channel.
// Uses SINGLE conversion mode for determinism.
// This is safest for self-heating and cross-coupling.
bool ad7124_read_one_rtd_safe(ad7124_dev_t *dev,
                              uint8_t ad7124_channel,
                              uint8_t rtd_setup,
                              ad7124_iout_val_t iout,
                              uint8_t rtd_top_ain,
                              double r_ref_ohms,
                              uint32_t timeout_us,
                              ad7124_rtd_sample_t *out);

                              
#ifdef __cplusplus
}
#endif
