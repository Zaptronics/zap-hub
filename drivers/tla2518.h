// tla2518.h
#pragma once


#include "ztmb/bus.h"


#ifdef __cplusplus
extern "C" {
#endif

// =========================
// Device context
// =========================
typedef struct {
    bus_spi_t spi_bus;

    // Cached configuration (optional but useful for parsing frames / convenience APIs)
    uint8_t  data_cfg;     // mirrors DATA_CFG
    uint8_t  osr_cfg;      // mirrors OSR_CFG
    uint8_t  seq_cfg;      // mirrors SEQUENCE_CFG
    uint16_t vref_mv;      // for code->uV conversion helpers (set by user)
} tla2518_dev_t;

// =========================
// Public enums / structs
// =========================
typedef enum {
    TLA2518_OK            = 0,
    TLA2518_ERR_ARG       = -1,
    TLA2518_ERR_SPI       = -2,
    TLA2518_ERR_TIMEOUT   = -3,
} tla2518_err_t;

typedef enum {
    TLA2518_SPI_00 = 0, // CPOL=0, CPHA=0
    TLA2518_SPI_01 = 1, // CPOL=0, CPHA=1
    TLA2518_SPI_10 = 2, // CPOL=1, CPHA=0
    TLA2518_SPI_11 = 3, // CPOL=1, CPHA=1
} tla2518_spi_mode_t;

typedef enum {
    TLA2518_OSR_NONE = 0, // 12-bit output
    TLA2518_OSR_2    = 1,
    TLA2518_OSR_4    = 2,
    TLA2518_OSR_8    = 3,
    TLA2518_OSR_16   = 4,
    TLA2518_OSR_32   = 5,
    TLA2518_OSR_64   = 6,
    TLA2518_OSR_128  = 7, // 16-bit output
} tla2518_osr_t;

typedef enum {
    TLA2518_SEQ_MANUAL     = 0, // channel selected by CHANNEL_SEL.MANUAL_CHID
    TLA2518_SEQ_AUTO       = 1, // sequencer uses AUTO_SEQ_CH_SEL + SEQ_START
    TLA2518_SEQ_ON_THE_FLY = 2, // channel select via first 5 bits after CS falling edge (advanced)
} tla2518_seq_mode_t;

typedef struct {
    uint16_t code;         // 12-bit (OSR_NONE) or 16-bit (OSR!=NONE)
    bool     has_chid;
    uint8_t  chid;         // 0..7 when appended; undefined otherwise
} tla2518_sample_t;

// =========================
// Init
// =========================
int tla2518_init(tla2518_dev_t *dev);

// =========================
// Raw register access
// =========================
int tla2518_reg_read(tla2518_dev_t *dev, uint8_t addr, uint8_t *val);
int tla2518_reg_write(tla2518_dev_t *dev, uint8_t addr, uint8_t val);
int tla2518_reg_set_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask);
int tla2518_reg_clear_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask);
int tla2518_reg_update_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask, uint8_t value);

// =========================
// Status / reset / calibration
// =========================
int  tla2518_get_system_status(tla2518_dev_t *dev, uint8_t *status);
bool tla2518_status_bor(uint8_t status);
bool tla2518_status_osr_done(uint8_t status);

int tla2518_soft_reset(tla2518_dev_t *dev);
int tla2518_calibrate_offset(tla2518_dev_t *dev, uint32_t timeout_reads);

// =========================
// Configuration helpers
// =========================
int tla2518_set_spi_mode(tla2518_dev_t *dev, tla2518_spi_mode_t mode);
int tla2518_set_append_channel_id(tla2518_dev_t *dev, bool enable);
int tla2518_set_fixed_pattern(tla2518_dev_t *dev, bool enable);

int tla2518_set_osr(tla2518_dev_t *dev, tla2518_osr_t osr);

// OPMODE_CFG: exposes raw fields (datasheet: OSC_SEL bit4, CLK_DIV[3:0])
int tla2518_set_opmode(tla2518_dev_t *dev, bool osc_sel, uint8_t clk_div);

// PIN_CFG: 1 = GPIO, 0 = analog
int tla2518_set_pin_cfg(tla2518_dev_t *dev, uint8_t gpio_mask);

// GPIO_CFG: 1 = output, 0 = input (only meaningful for channels configured as GPIO)
int tla2518_set_gpio_dir_outputs(tla2518_dev_t *dev, uint8_t output_mask);

// GPO_DRIVE_CFG: 1 = push-pull, 0 = open-drain
int tla2518_set_gpo_drive_pushpull(tla2518_dev_t *dev, uint8_t pushpull_mask);

// GPO_VALUE write, GPI_VALUE read
int tla2518_write_gpo(tla2518_dev_t *dev, uint8_t value);
int tla2518_read_gpi(tla2518_dev_t *dev, uint8_t *value);

// Sequencing
int tla2518_set_sequence_mode(tla2518_dev_t *dev, tla2518_seq_mode_t mode);
int tla2518_set_seq_start(tla2518_dev_t *dev, bool start);          // only used in AUTO mode
int tla2518_set_manual_channel(tla2518_dev_t *dev, uint8_t chid);   // 0..7
int tla2518_set_auto_sequence_mask(tla2518_dev_t *dev, uint8_t mask);

// =========================
// ADC reads
// =========================
// Reads one data frame (clocks out the ADC result). In MANUAL/AUTO modes, this typically returns the *previous*
// conversion result and starts the next conversion on CS rising edge.
int tla2518_read_sample(tla2518_dev_t *dev, tla2518_sample_t *out);

// If OSR averaging is enabled, this waits until OSR_DONE is 1, then reads one sample.
int tla2518_read_sample_osr_blocking(tla2518_dev_t *dev, tla2518_sample_t *out, uint32_t timeout_reads);

// Convenience: try to fetch a sample for a specific channel in MANUAL mode.
// If APPEND_STATUS is enabled, this will loop until chid matches (within a small limit).
int tla2518_read_channel_manual(tla2518_dev_t *dev, uint8_t chid, uint16_t *code, uint32_t timeout_reads);

// =========================
// Unit conversion helpers
// =========================
static inline void tla2518_set_vref_mv(tla2518_dev_t *dev, uint16_t vref_mv) {
    if (dev) dev->vref_mv = vref_mv;
}

// Returns microvolts for the current output width (12b or 16b), using dev->vref_mv.
// (For OSR_NONE: full-scale = 4095; for OSR!=NONE: full-scale = 65535)
uint32_t tla2518_code_to_uV(const tla2518_dev_t *dev, uint16_t code, int ratio_thousands);
#ifdef __cplusplus
}
#endif
