// pac195x.c
#include "pac195x.h"
#include "pico/stdlib.h"


#define PAC195X_MID_EXPECTED 0x54u

static inline uint8_t reg_vacc(uint8_t ch1)   { return (uint8_t)(PAC195X_REG_VACC1   + (ch1 - 1u)); }
static inline uint8_t reg_vbus(uint8_t ch1)   { return (uint8_t)(PAC195X_REG_VBUS1   + (ch1 - 1u)); }
static inline uint8_t reg_vsense(uint8_t ch1) { return (uint8_t)(PAC195X_REG_VSENSE1 + (ch1 - 1u)); }
static inline uint8_t reg_vbus_avg(uint8_t ch1)   { return (uint8_t)(PAC195X_REG_VBUS1_AVG   + (ch1 - 1u)); }
static inline uint8_t reg_vsense_avg(uint8_t ch1) { return (uint8_t)(PAC195X_REG_VSENSE1_AVG + (ch1 - 1u)); }
static inline uint8_t reg_vpower(uint8_t ch1) { return (uint8_t)(PAC195X_REG_VPOWER1 + (ch1 - 1u)); }

static uint8_t channels_from_pid(uint8_t pid) {
    // From datasheet Product ID list (PAC1951..4, -1 and -2 variants).
    switch (pid) {
        case 0x78: case 0x7C: return 1; // 1951-1 / 1951-2
        case 0x79: case 0x7D: return 2; // 1952-1 / 1952-2
        case 0x7A: case 0x7E: return 3; // 1953-1 / 1953-2
        case 0x7B: case 0x7F: return 4; // 1954-1 / 1954-2
        default: return 0;
    }
}

static uint8_t ch_off_nibble_from_enabled_mask(uint8_t enabled_mask /*bit0=CH1..bit3=CH4*/) {
    uint8_t off = 0;
    if ((enabled_mask & 0x01u) == 0) off |= (1u << 3); // CH1 off bit3
    if ((enabled_mask & 0x02u) == 0) off |= (1u << 2); // CH2 off bit2
    if ((enabled_mask & 0x04u) == 0) off |= (1u << 1); // CH3 off bit1
    if ((enabled_mask & 0x08u) == 0) off |= (1u << 0); // CH4 off bit0
    return off;
}

static uint16_t be16(const uint8_t b[2]) {
    return (uint16_t)((uint16_t)b[0] << 8) | (uint16_t)b[1];
}
static uint32_t be24(const uint8_t b[3]) {
    return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
}
static uint32_t be32(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

// Extract range cfg for channel from NEG_PWR_FSR_ACT (same layout as NEG_PWR_FSR)
static pac195x_vsense_range_t get_vs_cfg(uint16_t neg_pwr_fsr_act, uint8_t ch1) {
    uint8_t shift = (uint8_t)(14u - 2u * (ch1 - 1u)); // ch1->14, ch2->12, ch3->10, ch4->8
    return (pac195x_vsense_range_t)((neg_pwr_fsr_act >> shift) & 0x3u);
}
static pac195x_vbus_range_t get_vb_cfg(uint16_t neg_pwr_fsr_act, uint8_t ch1) {
    uint8_t shift = (uint8_t)(6u - 2u * (ch1 - 1u)); // ch1->6, ch2->4, ch3->2, ch4->0
    return (pac195x_vbus_range_t)((neg_pwr_fsr_act >> shift) & 0x3u);
}

static int32_t vbus_raw_to_uV(pac195x_vbus_range_t cfg, uint16_t raw) {
    // Uses the LSB/denominators described in the datasheet tables:
    // - UNIPOLAR_32V: 32V / 65536, unsigned
    // - BIPOLAR_32V : 32V / 32768, signed
    // - BIPOLAR_16V : 32V / 65536, signed  (same LSB as unipolar; ±16V range)
    switch (cfg) {
        default:
        case PAC195X_VBUS_UNIPOLAR_32V: {
            // uV = raw * 32e6 / 65536
            return (int32_t)(((int64_t)raw * 32000000ll) / 65536ll);
        }
        case PAC195X_VBUS_BIPOLAR_32V: {
            int16_t s = (int16_t)raw;
            return (int32_t)(((int64_t)s * 32000000ll) / 32768ll);
        }
        case PAC195X_VBUS_BIPOLAR_16V: {
            int16_t s = (int16_t)raw;
            return (int32_t)(((int64_t)s * 32000000ll) / 65536ll);
        }
    }
}

static int32_t vsense_raw_to_uV(pac195x_vsense_range_t cfg, uint16_t raw) {
    // - UNIPOLAR_100mV: 0.1V / 65536, unsigned
    // - BIPOLAR_100mV : 0.1V / 32768, signed
    // - BIPOLAR_50mV  : 0.1V / 65536, signed (same LSB as unipolar; ±50mV range)
    switch (cfg) {
        default:
        case PAC195X_VSENSE_UNIPOLAR_100mV: {
            return (int32_t)(((int64_t)raw * 100000ll) / 65536ll);
        }
        case PAC195X_VSENSE_BIPOLAR_100mV: {
            int16_t s = (int16_t)raw;
            return (int32_t)(((int64_t)s * 100000ll) / 32768ll);
        }
        case PAC195X_VSENSE_BIPOLAR_50mV: {
            int16_t s = (int16_t)raw;
            return (int32_t)(((int64_t)s * 100000ll) / 65536ll);
        }
    }
}

static int32_t current_uA_from_shunt_uV(int32_t shunt_uV, uint32_t shunt_uohm) {
    if (shunt_uohm == 0) return 0;
    // I[A] = uV/uΩ. Convert to uA => *1e6.
    return (int32_t)(((int64_t)shunt_uV * 1000000ll) / (int64_t)shunt_uohm);
}

static int32_t current_uA_from_vbus_raw(pac195x_vbus_range_t cfg,
                                        uint16_t raw,
                                        uint32_t shunt_uohm) {
    if (shunt_uohm == 0u) return 0;

    int64_t code = raw;
    int64_t fsr_uV = 32000000ll;
    int64_t denominator = 65536ll;
    if (cfg == PAC195X_VBUS_BIPOLAR_32V) {
        code = (int16_t)raw;
        denominator = 32768ll;
    } else if (cfg == PAC195X_VBUS_BIPOLAR_16V) {
        code = (int16_t)raw;
    }

    return (int32_t)((code * fsr_uV * 1000000ll) /
                     (denominator * (int64_t)shunt_uohm));
}

static int32_t current_uA_from_vsense_raw(pac195x_vsense_range_t cfg,
                                          uint16_t raw,
                                          uint32_t shunt_uohm) {
    if (shunt_uohm == 0u) return 0;

    int64_t code = raw;
    int64_t fsr_uV = 100000ll;
    int64_t denominator = 65536ll;
    if (cfg == PAC195X_VSENSE_BIPOLAR_100mV) {
        code = (int16_t)raw;
        denominator = 32768ll;
    } else if (cfg == PAC195X_VSENSE_BIPOLAR_50mV) {
        code = (int16_t)raw;
    }

    return (int32_t)((code * fsr_uV * 1000000ll) /
                     (denominator * (int64_t)shunt_uohm));
}

static int64_t power_uW_from_uV_uA(int32_t vbus_uV, int32_t current_uA) {
    // uW = (uV * uA) / 1e6
    return ((int64_t)vbus_uV * (int64_t)current_uA) / 1000000ll;
}

int pac195x_reg_read(pac195x_t *dev, uint8_t reg, uint8_t *buf, uint32_t len) {
    if (!dev || !buf || len == 0) return PAC195X_ERR_ARG;
    return bus_i2c_read_n(&dev->i2c_bus, reg, buf, len) ? PAC195X_OK : PAC195X_ERR_I2C;
}

int pac195x_reg_write(pac195x_t *dev, uint8_t reg, const uint8_t *buf, uint32_t len) {
    if (!dev || (!buf && len != 0)) return PAC195X_ERR_ARG;
    return bus_i2c_write_n(&dev->i2c_bus, reg, buf, len) ? PAC195X_OK : PAC195X_ERR_I2C;
}

int pac195x_reg_read_u8(pac195x_t *dev, uint8_t reg, uint8_t *val) {
    if (!val) return PAC195X_ERR_ARG;
    return pac195x_reg_read(dev, reg, val, 1);
}

int pac195x_reg_write_u8(pac195x_t *dev, uint8_t reg, uint8_t val) {
    return pac195x_reg_write(dev, reg, &val, 1);
}

int pac195x_reg_read_u16(pac195x_t *dev, uint8_t reg, uint16_t *val) {
    if (!val) return PAC195X_ERR_ARG;
    uint8_t b[2];
    int rc = pac195x_reg_read(dev, reg, b, 2);
    if (rc != PAC195X_OK) return rc;
    *val = be16(b);
    return PAC195X_OK;
}

int pac195x_reg_write_u16(pac195x_t *dev, uint8_t reg, uint16_t val) {
    uint8_t b[2] = {(uint8_t)(val >> 8), (uint8_t)(val & 0xFFu)};
    return pac195x_reg_write(dev, reg, b, 2);
}

int pac195x_reg_read_u24(pac195x_t *dev, uint8_t reg, uint32_t *val24) {
    if (!val24) return PAC195X_ERR_ARG;
    uint8_t b[3];
    int rc = pac195x_reg_read(dev, reg, b, 3);
    if (rc != PAC195X_OK) return rc;
    *val24 = be24(b) & 0x00FFFFFFu;
    return PAC195X_OK;
}

int pac195x_reg_write_u24(pac195x_t *dev, uint8_t reg, uint32_t val24) {
    uint8_t b[3] = {
        (uint8_t)((val24 >> 16) & 0xFFu),
        (uint8_t)((val24 >> 8) & 0xFFu),
        (uint8_t)(val24 & 0xFFu)
    };
    return pac195x_reg_write(dev, reg, b, 3);
}

int pac195x_reg_read_u32(pac195x_t *dev, uint8_t reg, uint32_t *val) {
    if (!val) return PAC195X_ERR_ARG;
    uint8_t b[4];
    int rc = pac195x_reg_read(dev, reg, b, 4);
    if (rc != PAC195X_OK) return rc;
    *val = be32(b);
    return PAC195X_OK;
}

int pac195x_reg_write_u32(pac195x_t *dev, uint8_t reg, uint32_t val) {
    uint8_t b[4] = {
        (uint8_t)((val >> 24) & 0xFFu),
        (uint8_t)((val >> 16) & 0xFFu),
        (uint8_t)((val >> 8) & 0xFFu),
        (uint8_t)(val & 0xFFu),
    };
    return pac195x_reg_write(dev, reg, b, 4);
}

int pac195x_refresh(pac195x_t *dev, pac195x_refresh_kind_t kind, bool wait_1ms) {
    if (!dev) return PAC195X_ERR_ARG;
    uint8_t cmd = (kind == PAC195X_REFRESH_NO_RESET) ? PAC195X_REG_REFRESH_V : PAC195X_REG_REFRESH;

    if (!bus_i2c_send_byte(&dev->i2c_bus, cmd)) return PAC195X_ERR_I2C;

    if (wait_1ms) busy_wait_ms(1);
    return PAC195X_OK;
}

int pac195x_read_active_config(pac195x_t *dev) {
    if (!dev) return PAC195X_ERR_ARG;
    int rc;

    rc = pac195x_reg_read_u16(dev, PAC195X_REG_CTRL_ACT, &dev->ctrl_act);
    if (rc != PAC195X_OK) return rc;

    rc = pac195x_reg_read_u16(dev, PAC195X_REG_NEG_PWR_FSR_ACT, &dev->neg_pwr_fsr_act);
    if (rc != PAC195X_OK) return rc;

    rc = pac195x_reg_read_u8(dev, PAC195X_REG_ACCUM_CONFIG_ACT, &dev->accum_config_act);
    return rc;
}

static int apply_smbus_settings(pac195x_t *dev) {
    uint8_t v = 0;
    if (dev->no_skip)      v |= PAC195X_SMBUS_NO_SKIP_BIT;
    if (dev->byte_count)   v |= PAC195X_SMBUS_BYTE_COUNT_BIT;   // keep false for plain I2C
    if (dev->i2c_hispeed)  v |= PAC195X_SMBUS_I2C_HISPEED_BIT;  // usually false
    // TIMEOUT stays disabled by default
    return pac195x_reg_write_u8(dev, PAC195X_REG_SMBUS_SETTINGS, v);
}

static int apply_ctrl(pac195x_t *dev) {
    uint16_t ctrl;
    int rc = pac195x_reg_read_u16(dev, PAC195X_REG_CTRL, &ctrl);
    if (rc != PAC195X_OK) return rc;

    // Update fields, preserving any unrelated bits (lower 4 are unimplemented).
    ctrl &= ~(PAC195X_CTRL_SAMPLE_MODE_MASK | PAC195X_CTRL_GPIO_ALERT2_MASK | PAC195X_CTRL_SLOW_ALERT1_MASK | PAC195X_CTRL_CH_OFF_MASK);

    ctrl |= ((uint16_t)(dev->sample_mode & 0xFu) << PAC195X_CTRL_SAMPLE_MODE_SHIFT);
    ctrl |= ((uint16_t)(dev->gpio_alert2_cfg & 0x3u) << PAC195X_CTRL_GPIO_ALERT2_SHIFT);
    ctrl |= ((uint16_t)(dev->slow_alert1_cfg & 0x3u) << PAC195X_CTRL_SLOW_ALERT1_SHIFT);

    uint8_t off_nib = ch_off_nibble_from_enabled_mask(dev->enabled_mask);
    ctrl |= ((uint16_t)(off_nib & 0xFu) << PAC195X_CTRL_CH_OFF_SHIFT);

    return pac195x_reg_write_u16(dev, PAC195X_REG_CTRL, ctrl);
}

static int apply_ranges(pac195x_t *dev) {
    // NEG_PWR_FSR layout:
    // [15:14] VS1, [13:12] VS2, [11:10] VS3, [9:8] VS4,
    // [7:6] VB1, [5:4] VB2, [3:2] VB3, [1:0] VB4
    uint16_t v = 0;

    v |= ((uint16_t)(dev->vsense_range[0] & 0x3u) << 14);
    v |= ((uint16_t)(dev->vsense_range[1] & 0x3u) << 12);
    v |= ((uint16_t)(dev->vsense_range[2] & 0x3u) << 10);
    v |= ((uint16_t)(dev->vsense_range[3] & 0x3u) << 8);

    v |= ((uint16_t)(dev->vbus_range[0] & 0x3u) << 6);
    v |= ((uint16_t)(dev->vbus_range[1] & 0x3u) << 4);
    v |= ((uint16_t)(dev->vbus_range[2] & 0x3u) << 2);
    v |= ((uint16_t)(dev->vbus_range[3] & 0x3u) << 0);

    return pac195x_reg_write_u16(dev, PAC195X_REG_NEG_PWR_FSR, v);
}

int pac195x_init(pac195x_t *dev) {
    if (!dev) return PAC195X_ERR_ARG;


    // Read IDs
    int rc = pac195x_reg_read_u8(dev, PAC195X_REG_MANUFACTURER_ID, &dev->manufacturer_id);
    if (rc != PAC195X_OK) return rc;

    rc = pac195x_reg_read_u8(dev, PAC195X_REG_PRODUCT_ID, &dev->product_id);
    if (rc != PAC195X_OK) return rc;

    rc = pac195x_reg_read_u8(dev, PAC195X_REG_REVISION_ID, &dev->revision_id);
    if (rc != PAC195X_OK) return rc;

    if (dev->manufacturer_id != PAC195X_MID_EXPECTED) return PAC195X_ERR_ID;

    dev->channels = channels_from_pid(dev->product_id);
    if (dev->channels == 0u) return PAC195X_ERR_ID;

    // Apply optional settings
    if (dev->set_smbus_settings) {
        rc = apply_smbus_settings(dev);
        if (rc != PAC195X_OK) return rc;
    }

    if (dev->set_ctrl) {
        // Clamp the request to channels physically implemented by this product.
        uint8_t present_mask = (uint8_t)((1u << dev->channels) - 1u);
        dev->enabled_mask &= present_mask;
        // If the caller did not select any channel, enable every implemented channel.
        if (dev->enabled_mask == 0u) {
            dev->enabled_mask = present_mask;
        }
        rc = apply_ctrl(dev);
        if (rc != PAC195X_OK) return rc;
    }

    if (dev->set_ranges) {
        rc = apply_ranges(dev);
        if (rc != PAC195X_OK) return rc;
    }

    if (dev->set_accum_config) {
        rc = pac195x_reg_write_u8(dev, PAC195X_REG_ACCUM_CONFIG, dev->accum_config);
        if (rc != PAC195X_OK) return rc;
    }

    // Latch changes
    rc = pac195x_refresh(dev, dev->init_refresh_kind, true);
    if (rc != PAC195X_OK) return rc;

    // Cache active values
    return pac195x_read_active_config(dev);
}

int pac195x_read_vbus_shunt(pac195x_t *dev,
                            uint8_t ch_1based,
                            bool averaged,
                            uint32_t shunt_uohm,
                            pac195x_shunt_reading_t *out) {
    if (!dev || !out || shunt_uohm == 0u ||
        ch_1based < 1u || ch_1based > dev->channels) {
        return PAC195X_ERR_ARG;
    }

    uint16_t raw = 0;
    int rc = pac195x_reg_read_u16(dev,
                                  averaged ? reg_vbus_avg(ch_1based) : reg_vbus(ch_1based),
                                  &raw);
    if (rc != PAC195X_OK) return rc;

    pac195x_vbus_range_t cfg = get_vb_cfg(dev->neg_pwr_fsr_act, ch_1based);
    out->raw = raw;
    out->shunt_uV = vbus_raw_to_uV(cfg, raw);
    out->current_uA = current_uA_from_vbus_raw(cfg, raw, shunt_uohm);
    return PAC195X_OK;
}

int pac195x_read_vsense_shunt(pac195x_t *dev,
                              uint8_t ch_1based,
                              bool averaged,
                              uint32_t shunt_uohm,
                              pac195x_shunt_reading_t *out) {
    if (!dev || !out || shunt_uohm == 0u ||
        ch_1based < 1u || ch_1based > dev->channels) {
        return PAC195X_ERR_ARG;
    }

    uint16_t raw = 0;
    int rc = pac195x_reg_read_u16(dev,
                                  averaged ? reg_vsense_avg(ch_1based) : reg_vsense(ch_1based),
                                  &raw);
    if (rc != PAC195X_OK) return rc;

    pac195x_vsense_range_t cfg = get_vs_cfg(dev->neg_pwr_fsr_act, ch_1based);
    out->raw = raw;
    out->shunt_uV = vsense_raw_to_uV(cfg, raw);
    out->current_uA = current_uA_from_vsense_raw(cfg, raw, shunt_uohm);
    return PAC195X_OK;
}

int pac195x_read_channel(pac195x_t *dev, uint8_t ch_1based, bool averaged, pac195x_channel_reading_t *out) {
    if (!dev || !out) return PAC195X_ERR_ARG;
    if (ch_1based < 1 || ch_1based > 4 || ch_1based > dev->channels) return PAC195X_ERR_ARG;

    uint16_t vbus_raw = 0, vsense_raw = 0;
    uint32_t vpower_raw = 0;

    int rc = pac195x_reg_read_u16(dev, averaged ? reg_vbus_avg(ch_1based) : reg_vbus(ch_1based), &vbus_raw);
    if (rc != PAC195X_OK) return rc;

    rc = pac195x_reg_read_u16(dev, averaged ? reg_vsense_avg(ch_1based) : reg_vsense(ch_1based), &vsense_raw);
    if (rc != PAC195X_OK) return rc;

    // VPOWER is optional but cheap; keep it for completeness.
    rc = pac195x_reg_read_u32(dev, reg_vpower(ch_1based), &vpower_raw);
    if (rc != PAC195X_OK) return rc;

    // Ensure we have latest active config cached (init does this; user can call pac195x_read_active_config later).
    pac195x_vbus_range_t vb_cfg = get_vb_cfg(dev->neg_pwr_fsr_act, ch_1based);
    pac195x_vsense_range_t vs_cfg = get_vs_cfg(dev->neg_pwr_fsr_act, ch_1based);

    int32_t vbus_uV = vbus_raw_to_uV(vb_cfg, vbus_raw);
    int32_t vsense_uV = vsense_raw_to_uV(vs_cfg, vsense_raw);

    uint32_t shunt = dev->shunt_uohm[ch_1based - 1u];
    int32_t current_uA = current_uA_from_vsense_raw(vs_cfg, vsense_raw, shunt);
    int64_t power_uW = power_uW_from_uV_uA(vbus_uV, current_uA);

    out->vbus_raw   = vbus_raw;
    out->vsense_raw = vsense_raw;
    out->vpower_raw = vpower_raw;
    out->vbus_uV    = vbus_uV;
    out->vsense_uV  = vsense_uV;
    out->current_uA = current_uA;
    out->power_uW   = power_uW;

    return PAC195X_OK;
}

int pac195x_read_all_snapshot(pac195x_t *dev, bool averaged, pac195x_channel_reading_t out[4]) {
    if (!dev || !out) return PAC195X_ERR_ARG;

    int rc = pac195x_refresh(dev, PAC195X_REFRESH_NO_RESET, true);
    if (rc != PAC195X_OK) return rc;

    // refresh cached actives too (useful if host changed configuration elsewhere)
    rc = pac195x_read_active_config(dev);
    if (rc != PAC195X_OK) return rc;

    for (uint8_t ch = 1; ch <= dev->channels; ch++) {
        rc = pac195x_read_channel(dev, ch, averaged, &out[ch - 1u]);
        if (rc != PAC195X_OK) return rc;
    }
    return PAC195X_OK;
}

int pac195x_read_alert_status(pac195x_t *dev, uint32_t *status24) {
    if (!dev || !status24) return PAC195X_ERR_ARG;
    return pac195x_reg_read_u24(dev, PAC195X_REG_ALERT_STATUS, status24);
}

int pac195x_read_acc_count(pac195x_t *dev, uint32_t *acc_count) {
    if (!dev || !acc_count) return PAC195X_ERR_ARG;
    return pac195x_reg_read_u32(dev, PAC195X_REG_ACC_COUNT, acc_count);
}

int pac195x_read_vacc(pac195x_t *dev, uint8_t ch_1based, int64_t *vacc_signed_56) {
    if (!dev || !vacc_signed_56) return PAC195X_ERR_ARG;
    if (ch_1based < 1 || ch_1based > 4 || ch_1based > dev->channels) return PAC195X_ERR_ARG;

    uint8_t b[7];
    int rc = pac195x_reg_read(dev, reg_vacc(ch_1based), b, 7);
    if (rc != PAC195X_OK) return rc;

    // Big-endian 56-bit number.
    int64_t v = 0;
    for (int i = 0; i < 7; i++) {
        v = (v << 8) | (int64_t)b[i];
    }

    // Sign-extend if power can be signed (any bipolar VSENSE or VBUS range => signed accumulator per datasheet).
    pac195x_vbus_range_t vb_cfg = get_vb_cfg(dev->neg_pwr_fsr_act, ch_1based);
    pac195x_vsense_range_t vs_cfg = get_vs_cfg(dev->neg_pwr_fsr_act, ch_1based);
    bool signed_acc = (vb_cfg != PAC195X_VBUS_UNIPOLAR_32V) || (vs_cfg != PAC195X_VSENSE_UNIPOLAR_100mV);

    if (signed_acc) {
        // 56-bit two's complement: sign is bit55.
        if (v & (1ll << 55)) {
            v |= ~((1ll << 56) - 1ll);
        }
    }

    *vacc_signed_56 = v;
    return PAC195X_OK;
}