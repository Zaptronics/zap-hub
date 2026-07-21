// tla2518.c
#include "tla2518.h"

// =========================
// Opcodes (datasheet Table 5)
// =========================
#define TLA2518_OP_NOP      0x00
#define TLA2518_OP_RD_REG   0x10
#define TLA2518_OP_WR_REG   0x08
#define TLA2518_OP_SET_BIT  0x18
#define TLA2518_OP_CLR_BIT  0x20

// =========================
// Register map (datasheet Table 7)
// =========================
#define TLA2518_REG_SYSTEM_STATUS    0x00
#define TLA2518_REG_GENERAL_CFG      0x01
#define TLA2518_REG_DATA_CFG         0x02
#define TLA2518_REG_OSR_CFG          0x03
#define TLA2518_REG_OPMODE_CFG       0x04
#define TLA2518_REG_PIN_CFG          0x05
#define TLA2518_REG_GPIO_CFG         0x07
#define TLA2518_REG_GPO_DRIVE_CFG    0x09
#define TLA2518_REG_GPO_VALUE        0x0B
#define TLA2518_REG_GPI_VALUE        0x0D
#define TLA2518_REG_SEQUENCE_CFG     0x10
#define TLA2518_REG_CHANNEL_SEL      0x11
#define TLA2518_REG_AUTO_SEQ_CH_SEL  0x12

// =========================
// Bitfields
// =========================
// SYSTEM_STATUS (inferred common usage)
#define TLA2518_SYSTEM_STATUS_BOR      (1u << 7)
#define TLA2518_SYSTEM_STATUS_OSR_DONE (1u << 0)

// GENERAL_CFG
#define TLA2518_GENERAL_CFG_CH_RST (1u << 2)
#define TLA2518_GENERAL_CFG_CAL    (1u << 1)
#define TLA2518_GENERAL_CFG_RST    (1u << 0)

// DATA_CFG
#define TLA2518_DATA_CFG_FIX_PAT        (1u << 7)
#define TLA2518_DATA_CFG_APPEND_STATUS_SHIFT 4
#define TLA2518_DATA_CFG_APPEND_STATUS_MASK  (3u << TLA2518_DATA_CFG_APPEND_STATUS_SHIFT)
#define TLA2518_DATA_CFG_CPOL_CPHA_MASK 0x03

// OSR_CFG
#define TLA2518_OSR_CFG_MASK 0x07

// OPMODE_CFG
#define TLA2518_OPMODE_CFG_OSC_SEL (1u << 4)
#define TLA2518_OPMODE_CFG_CLK_DIV_MASK 0x0F

// SEQUENCE_CFG
#define TLA2518_SEQUENCE_CFG_SEQ_START (1u << 4)
#define TLA2518_SEQUENCE_CFG_SEQ_MODE_MASK 0x03

// CHANNEL_SEL
#define TLA2518_CHANNEL_SEL_MANUAL_CHID_MASK 0x0F

// =========================
// Internal helpers
// =========================
static inline int _spi_wr(tla2518_dev_t *dev, const uint8_t *tx, size_t len) {
    return bus_spi_write(&dev->spi_bus, tx, len) ? TLA2518_OK : TLA2518_ERR_SPI;
}

static inline int _spi_wrrd(tla2518_dev_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    return bus_spi_xfer(&dev->spi_bus, tx, rx, len) ? TLA2518_OK : TLA2518_ERR_SPI;
}

static bool _set_host_spi_mode(tla2518_dev_t *dev, tla2518_spi_mode_t mode) {
    spi_cpol_t cpol = (((uint8_t)mode & 0x02u) != 0) ? SPI_CPOL_1 : SPI_CPOL_0;
    spi_cpha_t cpha = (((uint8_t)mode & 0x01u) != 0) ? SPI_CPHA_1 : SPI_CPHA_0;
    return bus_spi_set_mode(&dev->spi_bus, cpol, cpha);
}

static int _spi_wr_then_stage_mode(tla2518_dev_t *dev,
                                   const uint8_t *tx,
                                   size_t len,
                                   tla2518_spi_mode_t next_mode) {
    if (!bus_spi_begin(&dev->spi_bus)) return TLA2518_ERR_SPI;

    bool ok = bus_spi_transfer(&dev->spi_bus, tx, NULL, len);
    if (ok) {
        ok = _set_host_spi_mode(dev, next_mode);
    }
    bool ended = bus_spi_end(&dev->spi_bus);
    return (ok && ended) ? TLA2518_OK : TLA2518_ERR_SPI;
}



static inline uint32_t _u32_from_be(const uint8_t *b, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; i++) v = (v << 8) | b[i];
    return v;
}

static inline uint8_t _data_cfg_append_status(const tla2518_dev_t *dev) {
    return (uint8_t)((dev->data_cfg & TLA2518_DATA_CFG_APPEND_STATUS_MASK) >> TLA2518_DATA_CFG_APPEND_STATUS_SHIFT);
}

static inline bool _data_cfg_append_chid(const tla2518_dev_t *dev) {
    return _data_cfg_append_status(dev) == 1;
}

static inline bool _data_cfg_cpha_1(const tla2518_dev_t *dev) {
    return (dev->data_cfg & 0x01u) != 0;
}

static inline bool _osr_enabled(const tla2518_dev_t *dev) {
    return (dev->osr_cfg & TLA2518_OSR_CFG_MASK) != 0;
}

static inline uint8_t _sample_data_bits(const tla2518_dev_t *dev) {
    return _osr_enabled(dev) ? 16 : 12;
}

static inline uint8_t _sample_total_bits(const tla2518_dev_t *dev) {
    uint8_t bits = _sample_data_bits(dev);
    if (_data_cfg_append_chid(dev)) bits += 4;
    return bits;
}

static inline uint8_t _sample_nbytes(const tla2518_dev_t *dev) {
    uint8_t bits = _sample_total_bits(dev);
    return (uint8_t)((bits + 7u) / 8u);
}

static int _cache_readback_cfg(tla2518_dev_t *dev) {
    // Read back the key regs we cache. (Each reg read = 2 frames)
    uint8_t v;
    int rc;

    rc = tla2518_reg_read(dev, TLA2518_REG_DATA_CFG, &v);
    if (rc) return rc;
    dev->data_cfg = v;

    rc = tla2518_reg_read(dev, TLA2518_REG_OSR_CFG, &v);
    if (rc) return rc;
    dev->osr_cfg = v;

    rc = tla2518_reg_read(dev, TLA2518_REG_SEQUENCE_CFG, &v);
    if (rc) return rc;
    dev->seq_cfg = v;

    return TLA2518_OK;
}

// =========================
// Public: init
// =========================
int tla2518_init(tla2518_dev_t *dev) {
    if (!dev) return TLA2518_ERR_ARG;

    // Default cache values
    dev->data_cfg = 0;
    dev->osr_cfg  = 0;
    dev->seq_cfg  = 0;
    if (dev->vref_mv == 0) dev->vref_mv = 3300; // safe default; override as needed

    // Basic comms check: read SYSTEM_STATUS
    uint8_t st = 0;
    int rc = tla2518_get_system_status(dev, &st);
    if (rc) return rc;

    // Reasonable defaults:
    // - APPEND_STATUS = 1 (append channel ID)
    // - CPOL_CPHA = 0 (SPI-00). NOTE: datasheet requires first write after reset in SPI-00.
    // - OSR = none (12-bit)
    // - Manual mode, channel 0
    rc = tla2518_set_fixed_pattern(dev, false);
    if (rc) return rc;

    rc = tla2518_set_spi_mode(dev, TLA2518_SPI_00);
    if (rc) return rc;

    rc = tla2518_set_append_channel_id(dev, true);
    if (rc) return rc;

    rc = tla2518_set_osr(dev, TLA2518_OSR_NONE);
    if (rc) return rc;

    rc = tla2518_set_pin_cfg(dev, 0x00); // all analog
    if (rc) return rc;

    rc = tla2518_set_gpio_dir_outputs(dev, 0x00); // all inputs (if ever used as GPIO)
    if (rc) return rc;

    rc = tla2518_set_gpo_drive_pushpull(dev, 0x00); // open-drain default
    if (rc) return rc;

    rc = tla2518_set_sequence_mode(dev, TLA2518_SEQ_MANUAL);
    if (rc) return rc;

    rc = tla2518_set_seq_start(dev, false);
    if (rc) return rc;

    rc = tla2518_set_manual_channel(dev, 0);
    if (rc) return rc;

    // Refresh cache
    return _cache_readback_cfg(dev);
}

// =========================
// Register access
// =========================
int tla2518_reg_write(tla2518_dev_t *dev, uint8_t addr, uint8_t val) {
    if (!dev) return TLA2518_ERR_ARG;
    uint8_t tx[3] = { TLA2518_OP_WR_REG, addr, val };
    return _spi_wr(dev, tx, sizeof(tx));
}

int tla2518_reg_read(tla2518_dev_t *dev, uint8_t addr, uint8_t *val) {
    if (!dev || !val) return TLA2518_ERR_ARG;

    uint8_t tx1[3] = { TLA2518_OP_RD_REG, addr, 0x00 };
    uint8_t rx1[3] = {0};
    int rc = _spi_wrrd(dev, tx1, rx1, sizeof(tx1));
    if (rc) return rc;

    // Next frame: first 8 bits on SDO are the register data
    uint8_t tx2[3] = { TLA2518_OP_NOP, 0x00, 0x00 };
    uint8_t rx2[3] = {0};
    rc = _spi_wrrd(dev, tx2, rx2, sizeof(tx2));
    if (rc) return rc;

    *val = rx2[0];
    return TLA2518_OK;
}

int tla2518_reg_set_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask) {
    if (!dev) return TLA2518_ERR_ARG;
    uint8_t tx[3] = { TLA2518_OP_SET_BIT, addr, mask };
    return _spi_wr(dev, tx, sizeof(tx));
}

int tla2518_reg_clear_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask) {
    if (!dev) return TLA2518_ERR_ARG;
    uint8_t tx[3] = { TLA2518_OP_CLR_BIT, addr, mask };
    return _spi_wr(dev, tx, sizeof(tx));
}

int tla2518_reg_update_bits(tla2518_dev_t *dev, uint8_t addr, uint8_t mask, uint8_t value) {
    uint8_t cur;
    int rc = tla2518_reg_read(dev, addr, &cur);
    if (rc) return rc;
    cur = (uint8_t)((cur & ~mask) | (value & mask));
    return tla2518_reg_write(dev, addr, cur);
}

// =========================
// Status / reset / calibration
// =========================
int tla2518_get_system_status(tla2518_dev_t *dev, uint8_t *status) {
    return tla2518_reg_read(dev, TLA2518_REG_SYSTEM_STATUS, status);
}

bool tla2518_status_bor(uint8_t status) {
    return (status & TLA2518_SYSTEM_STATUS_BOR) != 0;
}

bool tla2518_status_osr_done(uint8_t status) {
    return (status & TLA2518_SYSTEM_STATUS_OSR_DONE) != 0;
}

int tla2518_soft_reset(tla2518_dev_t *dev) {
    if (!dev) return TLA2518_ERR_ARG;

    // Write the reset command in the current mode, then stage SPI-00 before
    // releasing the shared bus. The device defaults to SPI-00 after reset.
    uint8_t tx[3] = {
        TLA2518_OP_SET_BIT,
        TLA2518_REG_GENERAL_CFG,
        TLA2518_GENERAL_CFG_RST
    };
    int rc = _spi_wr_then_stage_mode(dev, tx, sizeof(tx), TLA2518_SPI_00);
    if (rc) return rc;

    dev->data_cfg = 0;
    dev->osr_cfg = 0;
    dev->seq_cfg = 0;
    return _cache_readback_cfg(dev);
}

int tla2518_calibrate_offset(tla2518_dev_t *dev, uint32_t timeout_reads) {
    if (!dev) return TLA2518_ERR_ARG;

    int rc = tla2518_reg_set_bits(dev, TLA2518_REG_GENERAL_CFG, TLA2518_GENERAL_CFG_CAL);
    if (rc) return rc;

    // CAL bit clears when done (read GENERAL_CFG until CAL==0)
    while (timeout_reads--) {
        uint8_t v;
        rc = tla2518_reg_read(dev, TLA2518_REG_GENERAL_CFG, &v);
        if (rc) return rc;
        if ((v & TLA2518_GENERAL_CFG_CAL) == 0) return TLA2518_OK;
    }
    return TLA2518_ERR_TIMEOUT;
}

// =========================
// Configuration helpers
// =========================
int tla2518_set_spi_mode(tla2518_dev_t *dev, tla2518_spi_mode_t mode) {
    if (!dev) return TLA2518_ERR_ARG;

    uint8_t v;
    int rc = tla2518_reg_read(dev, TLA2518_REG_DATA_CFG, &v);
    if (rc) return rc;

    v = (uint8_t)((v & ~TLA2518_DATA_CFG_CPOL_CPHA_MASK) | ((uint8_t)mode & TLA2518_DATA_CFG_CPOL_CPHA_MASK));
    uint8_t tx[3] = { TLA2518_OP_WR_REG, TLA2518_REG_DATA_CFG, v };
    rc = _spi_wr_then_stage_mode(dev, tx, sizeof(tx), mode);
    if (rc) return rc;

    dev->data_cfg = v;
    return TLA2518_OK;
}

int tla2518_set_append_channel_id(tla2518_dev_t *dev, bool enable) {
    if (!dev) return TLA2518_ERR_ARG;

    uint8_t v;
    int rc = tla2518_reg_read(dev, TLA2518_REG_DATA_CFG, &v);
    if (rc) return rc;

    v &= (uint8_t)~TLA2518_DATA_CFG_APPEND_STATUS_MASK;
    if (enable) {
        v |= (uint8_t)(1u << TLA2518_DATA_CFG_APPEND_STATUS_SHIFT); // 01b
    }
    rc = tla2518_reg_write(dev, TLA2518_REG_DATA_CFG, v);
    if (rc) return rc;

    dev->data_cfg = v;
    return TLA2518_OK;
}

int tla2518_set_fixed_pattern(tla2518_dev_t *dev, bool enable) {
    if (!dev) return TLA2518_ERR_ARG;
    int rc;
    if (enable) rc = tla2518_reg_set_bits(dev, TLA2518_REG_DATA_CFG, TLA2518_DATA_CFG_FIX_PAT);
    else        rc = tla2518_reg_clear_bits(dev, TLA2518_REG_DATA_CFG, TLA2518_DATA_CFG_FIX_PAT);
    if (rc) return rc;

    // refresh cache
    uint8_t v;
    rc = tla2518_reg_read(dev, TLA2518_REG_DATA_CFG, &v);
    if (rc) return rc;
    dev->data_cfg = v;
    return TLA2518_OK;
}

int tla2518_set_osr(tla2518_dev_t *dev, tla2518_osr_t osr) {
    if (!dev) return TLA2518_ERR_ARG;

    uint8_t v = (uint8_t)osr & TLA2518_OSR_CFG_MASK;
    int rc = tla2518_reg_write(dev, TLA2518_REG_OSR_CFG, v);
    if (rc) return rc;

    dev->osr_cfg = v;
    return TLA2518_OK;
}

int tla2518_set_opmode(tla2518_dev_t *dev, bool osc_sel, uint8_t clk_div) {
    if (!dev) return TLA2518_ERR_ARG;

    clk_div &= TLA2518_OPMODE_CFG_CLK_DIV_MASK;
    uint8_t v = clk_div;
    if (osc_sel) v |= TLA2518_OPMODE_CFG_OSC_SEL;

    return tla2518_reg_write(dev, TLA2518_REG_OPMODE_CFG, v);
}

int tla2518_set_pin_cfg(tla2518_dev_t *dev, uint8_t gpio_mask) {
    if (!dev) return TLA2518_ERR_ARG;
    return tla2518_reg_write(dev, TLA2518_REG_PIN_CFG, gpio_mask);
}

int tla2518_set_gpio_dir_outputs(tla2518_dev_t *dev, uint8_t output_mask) {
    if (!dev) return TLA2518_ERR_ARG;
    return tla2518_reg_write(dev, TLA2518_REG_GPIO_CFG, output_mask);
}

int tla2518_set_gpo_drive_pushpull(tla2518_dev_t *dev, uint8_t pushpull_mask) {
    if (!dev) return TLA2518_ERR_ARG;
    return tla2518_reg_write(dev, TLA2518_REG_GPO_DRIVE_CFG, pushpull_mask);
}

int tla2518_write_gpo(tla2518_dev_t *dev, uint8_t value) {
    if (!dev) return TLA2518_ERR_ARG;
    return tla2518_reg_write(dev, TLA2518_REG_GPO_VALUE, value);
}

int tla2518_read_gpi(tla2518_dev_t *dev, uint8_t *value) {
    if (!dev || !value) return TLA2518_ERR_ARG;
    return tla2518_reg_read(dev, TLA2518_REG_GPI_VALUE, value);
}

int tla2518_set_sequence_mode(tla2518_dev_t *dev, tla2518_seq_mode_t mode) {
    if (!dev) return TLA2518_ERR_ARG;

    uint8_t v;
    int rc = tla2518_reg_read(dev, TLA2518_REG_SEQUENCE_CFG, &v);
    if (rc) return rc;

    v &= (uint8_t)~TLA2518_SEQUENCE_CFG_SEQ_MODE_MASK;
    v |= ((uint8_t)mode & TLA2518_SEQUENCE_CFG_SEQ_MODE_MASK);

    rc = tla2518_reg_write(dev, TLA2518_REG_SEQUENCE_CFG, v);
    if (rc) return rc;

    dev->seq_cfg = v;
    return TLA2518_OK;
}

int tla2518_set_seq_start(tla2518_dev_t *dev, bool start) {
    if (!dev) return TLA2518_ERR_ARG;

    int rc;
    if (start) rc = tla2518_reg_set_bits(dev, TLA2518_REG_SEQUENCE_CFG, TLA2518_SEQUENCE_CFG_SEQ_START);
    else       rc = tla2518_reg_clear_bits(dev, TLA2518_REG_SEQUENCE_CFG, TLA2518_SEQUENCE_CFG_SEQ_START);

    if (rc) return rc;

    uint8_t v;
    rc = tla2518_reg_read(dev, TLA2518_REG_SEQUENCE_CFG, &v);
    if (rc) return rc;
    dev->seq_cfg = v;

    return TLA2518_OK;
}

int tla2518_set_manual_channel(tla2518_dev_t *dev, uint8_t chid) {
    if (!dev) return TLA2518_ERR_ARG;
    chid &= TLA2518_CHANNEL_SEL_MANUAL_CHID_MASK;
    return tla2518_reg_write(dev, TLA2518_REG_CHANNEL_SEL, chid);
}

int tla2518_set_auto_sequence_mask(tla2518_dev_t *dev, uint8_t mask) {
    if (!dev) return TLA2518_ERR_ARG;
    return tla2518_reg_write(dev, TLA2518_REG_AUTO_SEQ_CH_SEL, mask);
}

// =========================
// ADC reads
// =========================
int tla2518_read_sample(tla2518_dev_t *dev, tla2518_sample_t *out) {
    if (!dev || !out) return TLA2518_ERR_ARG;

    const uint8_t nbytes = _sample_nbytes(dev);

    uint8_t tx[3] = {0,0,0};
    uint8_t rx[3] = {0,0,0};

    // We only clock as many bytes as needed for the configured output frame size.
    int rc = _spi_wrrd(dev, tx, rx, nbytes);
    if (rc) return rc;

    const uint8_t total_bits = _sample_total_bits(dev);
    const uint8_t raw_bits   = (uint8_t)(nbytes * 8u);

    uint32_t raw = _u32_from_be(rx, nbytes);

    // If CPHA=1, the datasheet shows an initial dummy '0' bit on SDO for alignment.
    // Compensate by discarding the MSB and shifting left by 1 so parsing matches CPHA=0 alignment.
    if (_data_cfg_cpha_1(dev) && raw_bits > 0) {
        uint32_t mask = (raw_bits == 32) ? 0xFFFFFFFFu : ((1u << (raw_bits - 1u)) - 1u);
        raw = (raw & mask) << 1;
    }

    // Data is MSB-aligned; drop any pad bits at LSB.
    uint8_t pad = (uint8_t)(raw_bits - total_bits);
    uint32_t field = (pad < 32) ? (raw >> pad) : 0;

    out->has_chid = _data_cfg_append_chid(dev);
    if (out->has_chid) {
        out->chid = (uint8_t)(field & 0x0Fu);
        out->code = (uint16_t)(field >> 4);
    } else {
        out->chid = 0xFF;
        out->code = (uint16_t)field;
    }

    return TLA2518_OK;
}

int tla2518_read_sample_osr_blocking(tla2518_dev_t *dev, tla2518_sample_t *out, uint32_t timeout_reads) {
    if (!dev || !out) return TLA2518_ERR_ARG;

    if (!_osr_enabled(dev)) {
        return tla2518_read_sample(dev, out);
    }

    // Wait for OSR_DONE to be set, then read one sample (which also starts the next average).
    while (timeout_reads--) {
        uint8_t st;
        int rc = tla2518_get_system_status(dev, &st);
        if (rc) return rc;
        if (tla2518_status_osr_done(st)) {
            return tla2518_read_sample(dev, out);
        }
    }
    return TLA2518_ERR_TIMEOUT;
}

int tla2518_read_channel_manual(tla2518_dev_t *dev, uint8_t chid, uint16_t *code, uint32_t timeout_reads) {
    if (!dev || !code) return TLA2518_ERR_ARG;
    if (chid > 7) return TLA2518_ERR_ARG;

    // Force MANUAL mode + stop sequencer
    int rc = tla2518_set_sequence_mode(dev, TLA2518_SEQ_MANUAL);
    if (rc) return rc;
    rc = tla2518_set_seq_start(dev, false);
    if (rc) return rc;

    // Select channel for "next ADC conversion"
    rc = tla2518_set_manual_channel(dev, chid);
    if (rc) return rc;

    // In MANUAL mode there can be a pipeline effect (you may read the previous channel once).
    // If channel ID is appended, just keep reading until it matches.
    const bool want_match = _data_cfg_append_chid(dev);

    // For OSR enabled: kick one conversion to start the average, wait done, then read.
    if (_osr_enabled(dev)) {
        tla2518_sample_t dummy;
        (void)tla2518_read_sample(dev, &dummy); // starts average on CS rising edge
    }

    while (timeout_reads--) {
        tla2518_sample_t s;
        rc = tla2518_read_sample_osr_blocking(dev, &s, 2000);
        if (rc) return rc;

        if (!want_match || (s.has_chid && s.chid == chid)) {
            *code = s.code;
            return TLA2518_OK;
        }
    }

    return TLA2518_ERR_TIMEOUT;
}

// =========================
// Unit conversion helpers
// =========================
uint32_t tla2518_code_to_uV(const tla2518_dev_t *dev, uint16_t code, int ratio_thousands) {
    if (!dev || dev->vref_mv == 0) return 0;

    // OSR_NONE -> 12-bit codes (0..4095)
    // OSR!=NONE -> 16-bit codes (0..65535)
    const uint32_t fs = _osr_enabled(dev) ? 65535u : 4095u;

    // uV = code / fs * (vref_mv * 1000)
    // do the multiply first in 64-bit for precision
    uint64_t vref_uV = (uint64_t)dev->vref_mv * (unsigned long long)ratio_thousands;
    uint64_t uV = (uint64_t)code * vref_uV;
    uV /= fs;
    return (uint32_t)uV;
}
