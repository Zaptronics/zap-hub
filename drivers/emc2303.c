/* emc2303.c */

#include "pico/stdlib.h"
#include "emc2303.h"
#include "ztmb/log.h"

/* ---------- internal helpers ---------- */

static inline int set_last(emc2303_t *dev, int rc) {
    if (dev) dev->last_error = rc;
    return rc;
}

static inline bool fan_valid(uint8_t fan) {
    return (fan >= 1u) && (fan <= (uint8_t)EMC2303_MAX_FANS);
}

static int i2c_read8(emc2303_t *dev, uint8_t reg, uint8_t *val) {
    if (!val) return false;
    return bus_i2c_read(&dev->i2c_bus, reg, val);
}

static int i2c_write8(emc2303_t *dev, uint8_t reg, uint8_t val) {
    return bus_i2c_write(&dev->i2c_bus, reg, val);
}

/* 13-bit pack/unpack:
 * High byte holds bits [12:5]
 * Low  byte holds bits [4:0] in bits [7:3] (bits [2:0] are unimplemented)
 */
static inline uint16_t unpack13(uint8_t hi, uint8_t lo) {
    return (uint16_t)(((uint16_t)hi << 5) | ((uint16_t)(lo >> 3) & 0x1Fu)) & 0x1FFFu;
}

static inline void pack13(uint16_t v13, uint8_t *hi, uint8_t *lo) {
    v13 &= 0x1FFFu;
    *hi = (uint8_t)((v13 >> 5) & 0xFFu);
    *lo = (uint8_t)((v13 & 0x1Fu) << 3);
}

static uint8_t rng_to_multiplier(uint8_t rng_bits) {
    /* RNG bits: 00->1, 01->2, 10->4, 11->8 */
    switch (rng_bits & 0x3u) {
        default:
        case 0: return 1;
        case 1: return 2;
        case 2: return 4;
        case 3: return 8;
    }
}

static void edg_to_poles_and_n(uint8_t edg_bits, uint8_t *poles, uint8_t *n_edges) {
    /* EDG bits map to (poles, edges-measured n):
       00 -> (1,3), 01 -> (2,5), 10 -> (3,7), 11 -> (4,9)
     */
    switch (edg_bits & 0x3u) {
        default:
        case 0: *poles = 1; *n_edges = 3; break;
        case 1: *poles = 2; *n_edges = 5; break;
        case 2: *poles = 3; *n_edges = 7; break;
        case 3: *poles = 4; *n_edges = 9; break;
    }
}

/* ---------- public: low-level ---------- */

int emc2303_reg_read8(emc2303_t *dev, uint8_t reg, uint8_t *val) {
    int rc = i2c_read8(dev, reg, val);
    return set_last(dev, rc);
}

int emc2303_reg_write8(emc2303_t *dev, uint8_t reg, uint8_t val) {
    int rc = i2c_write8(dev, reg, val);
    return set_last(dev, rc);
}

int emc2303_reg_update8(emc2303_t *dev, uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t v = 0;
    int rc = i2c_read8(dev, reg, &v);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    v = (uint8_t)((v & ~mask) | (value & mask));
    rc = i2c_write8(dev, reg, v);
    return set_last(dev, rc);
}

int emc2303_reg_read13(emc2303_t *dev, uint8_t reg_high, uint8_t reg_low, uint16_t *val13) {
    if (!dev || !val13) return set_last(dev, EMC2303_E_BAD_ARG);

    /* For interlocked pairs, read HIGH first, then LOW */
    uint8_t hi = 0, lo = 0;
    int rc = i2c_read8(dev, reg_high, &hi);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = i2c_read8(dev, reg_low, &lo);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    *val13 = unpack13(hi, lo);
    return set_last(dev, EMC2303_OK);
}

int emc2303_reg_write13(emc2303_t *dev, uint8_t reg_high, uint8_t reg_low, uint16_t val13, bool low_then_high) {
    if (!dev) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t hi = 0, lo = 0;
    pack13(val13, &hi, &lo);

    /* Some registers (e.g., TACH TARGET) want low byte first then high byte to latch */
    int rc;
    if (low_then_high) {
        rc = i2c_write8(dev, reg_low, lo);
        if (rc != EMC2303_OK) return set_last(dev, rc);
        rc = i2c_write8(dev, reg_high, hi);
        if (rc != EMC2303_OK) return set_last(dev, rc);
    } else {
        rc = i2c_write8(dev, reg_high, hi);
        if (rc != EMC2303_OK) return set_last(dev, rc);
        rc = i2c_write8(dev, reg_low, lo);
        if (rc != EMC2303_OK) return set_last(dev, rc);
    }

    return set_last(dev, EMC2303_OK);
}

/* ---------- public: IDs ---------- */

int emc2303_read_ids(emc2303_t *dev, uint8_t *product_id, uint8_t *mfg_id, uint8_t *revision) {
    if (!dev) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t pid = 0, mid = 0, rev = 0;
    int rc = i2c_read8(dev, EMC2303_REG_PRODUCT_ID, &pid);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = i2c_read8(dev, EMC2303_REG_MANUFACTURER_ID, &mid);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = i2c_read8(dev, EMC2303_REG_REVISION, &rev);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    if (product_id) *product_id = pid;
    if (mfg_id)     *mfg_id     = mid;
    if (revision)   *revision   = rev;

    return set_last(dev, EMC2303_OK);
}


int emc2303_pwm1_toggle_test(emc2303_t *dev)
{
    /* Now you should see PWM on the PWM1 pin (with a pull-up present) */
    int rc = emc2303_reg_write8(dev, EMC2303_REG_FAN_DRIVE_SETTING(2), 0x00);
    if (rc != EMC2303_OK) return rc;
    busy_wait_ms(500);

    rc = emc2303_reg_write8(dev, EMC2303_REG_FAN_DRIVE_SETTING(2), 0xFF);
    if (rc != EMC2303_OK) return rc;
    busy_wait_ms(500);

    rc = emc2303_reg_write8(dev, EMC2303_REG_FAN_DRIVE_SETTING(2), 0x80);
    if (rc != EMC2303_OK) return rc;
    busy_wait_ms(500);

    return EMC2303_OK;
}

/* ---------- public: init ---------- */
int emc2303_init(emc2303_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit) {
    log_printf("emc2303_init start\n");

    static const bus_i2c_config_t i2c_config = {
        .baud_hz = 400000u,
        .timeout_us = 5000u,
    };
    if (!dev || !bus_i2c_init(&dev->i2c_bus,
                              i2c,
                              addr_7bit,
                              -1,
                              &i2c_config)) {
        return EMC2303_E_BAD_ARG;
    }

    if (dev->tach_clk_hz == 0) {
        dev->tach_clk_hz = 32768u;
    }

    /* Check software lock state */
    uint8_t lock = 0;
    int rc = i2c_read8(dev, EMC2303_REG_SOFTWARE_LOCK, &lock);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    if (lock & EMC2303_SWLOCK_LOCK) {
        return set_last(dev, EMC2303_E_LOCKED);
    }

    /* Verify IDs */
    uint8_t pid = 0, mid = 0, rev = 0;
    rc = emc2303_read_ids(dev, &pid, &mid, &rev);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    if (mid != EMC2303_MANUFACTURER_ID_VALUE) return set_last(dev, EMC2303_E_ID);
    if (pid != EMC2303_PRODUCT_ID_EMC2303)    return set_last(dev, EMC2303_E_ID);

    /* Make device fully I2C-compatible: set DIS_TO bit */
    rc = emc2303_reg_update8(dev, EMC2303_REG_CONFIG, EMC2303_CONFIG_DIS_TO, EMC2303_CONFIG_DIS_TO);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    /* Reasonable defaults that keep behavior simple/predictable:
     * - PWM base for 1..3: 26kHz (code 00)
     * - PWM divide for 1..3: 1
     * - FanConfig1: Direct mode (ENAG=0), RNG=00 (m=1), EDG=01 (2 poles), UDT=100ms
     * - FanConfig2: leave default (no ramp control, no glitch filter)
     */
    /* 1) CONFIG: set DIS_TO (disable SMBus timeout) */
    rc = emc2303_reg_update8(dev, EMC2303_REG_CONFIG, EMC2303_CONFIG_DIS_TO, EMC2303_CONFIG_DIS_TO);
    if (rc != EMC2303_OK) return rc;

    /* 2) PWM_OUTPUT_CONFIG (0x2B): PWM1 open-drain (bit0 = 0) */
    rc = emc2303_reg_write8(dev, EMC2303_REG_PWM_OUTPUT_CONFIG, 0x00);
    if (rc != EMC2303_OK) return rc;

    /* 3) PWM_POLARITY_CONFIG (0x2A): PWM1 normal polarity (bit0 = 0) */
    rc = emc2303_reg_write8(dev, EMC2303_REG_PWM_POLARITY_CONFIG, 0x00);
    if (rc != EMC2303_OK) return rc;

    rc = emc2303_reg_write8(dev, EMC2303_REG_PWM_BASEF123, 0x00);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    rc = emc2303_reg_write8(dev, EMC2303_REG_PWM_BASEF45, 0x00);
    if (rc != EMC2303_OK) return set_last(dev, rc);


    for (uint8_t fan = 1; fan <= (uint8_t)EMC2303_MAX_FANS; fan++) {
        (void)emc2303_set_pwm_divide(dev, fan, 1);

        /* ENAG=0, RNG=00, EDG=01, UDT=000 => 0b00001000 = 0x08 */
        (void)emc2303_reg_write8(dev, EMC2303_REG_FAN_CONFIG1(fan), 0x08);
        (void)emc2303_reg_write8(dev, EMC2303_REG_FAN_CONFIG2(fan), 0x00);
    }

    return set_last(dev, EMC2303_OK);
}

/* ---------- public: simple PWM ---------- */

int emc2303_fan_set_pwm_raw(emc2303_t *dev, uint8_t fan, uint8_t setting) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);
    int rc = emc2303_reg_write8(dev, EMC2303_REG_FAN_DRIVE_SETTING(fan), setting);
    return set_last(dev, rc);
}

int emc2303_fan_set_pwm_percent_x100(emc2303_t *dev, uint8_t fan, uint16_t percent_x100) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);
    if (percent_x100 > 10000u) percent_x100 = 10000u;

    /* Map 0..100.00% to 0..255 with rounding */
    uint32_t raw = (uint32_t)percent_x100 * 255u + 5000u;
    raw /= 10000u;
    if (raw > 255u) raw = 255u;

    return emc2303_fan_set_pwm_raw(dev, fan, (uint8_t)raw);
}

/* ---------- public: tach / RPM ---------- */

int emc2303_fan_get_tach_counts(emc2303_t *dev, uint8_t fan, uint16_t *tach_count13) {
    if (!dev || !tach_count13 || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);
    return emc2303_reg_read13(dev,
                              EMC2303_REG_TACH_READING_H(fan),
                              EMC2303_REG_TACH_READING_L(fan),
                              tach_count13);
}

int emc2303_fan_get_rpm(emc2303_t *dev, uint8_t fan, uint32_t *rpm) {
    if (!dev || !rpm || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    uint16_t count13 = 0;
    int rc = emc2303_fan_get_tach_counts(dev, fan, &count13);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    if (count13 == 0) { /* avoid divide-by-zero */
        *rpm = 0;
        return set_last(dev, EMC2303_OK);
    }

    /* Read FanConfig1 to get RNG (multiplier m) and EDG (poles/edges mapping) */
    uint8_t cfg1 = 0;
    rc = emc2303_reg_read8(dev, EMC2303_REG_FAN_CONFIG1(fan), &cfg1);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    uint8_t rng = (uint8_t)((cfg1 & EMC2303_FANCFG1_RNG_MASK) >> EMC2303_FANCFG1_RNG_SHIFT);
    uint8_t edg = (uint8_t)((cfg1 & EMC2303_FANCFG1_EDG_MASK) >> EMC2303_FANCFG1_EDG_SHIFT);

    uint8_t m = rng_to_multiplier(rng);
    uint8_t poles = 2, n_edges = 5;
    edg_to_poles_and_n(edg, &poles, &n_edges);

    /* General RPM formula:
       RPM = ((n_edges - 1) * m * f_tach * 60) / (poles * count)
     */
    uint64_t numerator = (uint64_t)(n_edges - 1u) * (uint64_t)m * (uint64_t)dev->tach_clk_hz * 60ull;
    uint64_t denom     = (uint64_t)poles * (uint64_t)count13;

    *rpm = (uint32_t)(numerator / denom);
    return set_last(dev, EMC2303_OK);
}

/* ---------- public: closed-loop target RPM ---------- */

int emc2303_fan_enable_closed_loop(emc2303_t *dev, uint8_t fan, bool enable) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t mask = EMC2303_FANCFG1_ENAG;
    uint8_t val  = enable ? EMC2303_FANCFG1_ENAG : 0u;
    int rc = emc2303_reg_update8(dev, EMC2303_REG_FAN_CONFIG1(fan), mask, val);
    return set_last(dev, rc);
}

int emc2303_fan_set_target_rpm(emc2303_t *dev, uint8_t fan, uint32_t rpm) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    if (rpm == 0u) {
        /* Treat rpm=0 as a simple “stop”: disable closed-loop and set PWM=0 */
        (void)emc2303_fan_enable_closed_loop(dev, fan, false);
        return emc2303_fan_set_pwm_raw(dev, fan, 0);
    }

    /* Read FanConfig1 for RNG/EDG to compute count target */
    uint8_t cfg1 = 0;
    int rc = emc2303_reg_read8(dev, EMC2303_REG_FAN_CONFIG1(fan), &cfg1);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    uint8_t rng = (uint8_t)((cfg1 & EMC2303_FANCFG1_RNG_MASK) >> EMC2303_FANCFG1_RNG_SHIFT);
    uint8_t edg = (uint8_t)((cfg1 & EMC2303_FANCFG1_EDG_MASK) >> EMC2303_FANCFG1_EDG_SHIFT);

    uint8_t m = rng_to_multiplier(rng);
    uint8_t poles = 2, n_edges = 5;
    edg_to_poles_and_n(edg, &poles, &n_edges);

    /* Invert RPM formula to get count:
       count = ((n_edges - 1) * m * f_tach * 60) / (poles * rpm)
     */
    uint64_t numerator = (uint64_t)(n_edges - 1u) * (uint64_t)m * (uint64_t)dev->tach_clk_hz * 60ull;
    uint64_t denom     = (uint64_t)poles * (uint64_t)rpm;
    uint64_t count     = (denom == 0) ? 0 : (numerator / denom);

    if (count > 0x1FFFu) count = 0x1FFFu;
    if (count == 0) count = 1; /* avoid “0” */

    /* Write TACH TARGET (low then high to latch) */
    rc = emc2303_reg_write13(dev,
                             EMC2303_REG_TACH_TARGET_H(fan),
                             EMC2303_REG_TACH_TARGET_L(fan),
                             (uint16_t)count,
                             true /*low_then_high*/);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    /* Enable closed-loop algorithm */
    rc = emc2303_fan_enable_closed_loop(dev, fan, true);
    return set_last(dev, rc);
}

/* ---------- public: config helpers ---------- */

int emc2303_fan_set_range(emc2303_t *dev, uint8_t fan, emc2303_range_t range) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t v = (uint8_t)(((uint8_t)range << EMC2303_FANCFG1_RNG_SHIFT) & EMC2303_FANCFG1_RNG_MASK);
    int rc = emc2303_reg_update8(dev, EMC2303_REG_FAN_CONFIG1(fan), EMC2303_FANCFG1_RNG_MASK, v);
    return set_last(dev, rc);
}

int emc2303_fan_set_poles(emc2303_t *dev, uint8_t fan, emc2303_poles_t poles) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t edg_bits;
    switch (poles) {
        default:
        case EMC2303_POLES_1: edg_bits = 0; break;
        case EMC2303_POLES_2: edg_bits = 1; break;
        case EMC2303_POLES_3: edg_bits = 2; break;
        case EMC2303_POLES_4: edg_bits = 3; break;
    }

    uint8_t v = (uint8_t)((edg_bits << EMC2303_FANCFG1_EDG_SHIFT) & EMC2303_FANCFG1_EDG_MASK);
    int rc = emc2303_reg_update8(dev, EMC2303_REG_FAN_CONFIG1(fan), EMC2303_FANCFG1_EDG_MASK, v);
    return set_last(dev, rc);
}

int emc2303_fan_set_update_interval(emc2303_t *dev, uint8_t fan, emc2303_update_t update) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t v = (uint8_t)(((uint8_t)update << EMC2303_FANCFG1_UDT_SHIFT) & EMC2303_FANCFG1_UDT_MASK);
    int rc = emc2303_reg_update8(dev, EMC2303_REG_FAN_CONFIG1(fan), EMC2303_FANCFG1_UDT_MASK, v);
    return set_last(dev, rc);
}

int emc2303_set_pwm_base(emc2303_t *dev, uint8_t fan, emc2303_pwm_base_t base) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    /* PWM_BASEF123 encodes:
       PMB1 bits1:0, PMB2 bits3:2, PMB3 bits5:4
     */
    uint8_t shift;
    if (fan == 1) shift = 0;
    else if (fan == 2) shift = 2;
    else /* fan == 3 */ shift = 4;

    uint8_t mask = (uint8_t)(0x3u << shift);
    uint8_t val  = (uint8_t)(((uint8_t)base & 0x3u) << shift);

    int rc = emc2303_reg_update8(dev, EMC2303_REG_PWM_BASEF123, mask, val);
    return set_last(dev, rc);
}

int emc2303_set_pwm_divide(emc2303_t *dev, uint8_t fan, uint8_t divider) {
    if (!dev || !fan_valid(fan)) return set_last(dev, EMC2303_E_BAD_ARG);

    /* 0 decodes to 1 internally; still, write 1 for clarity */
    if (divider == 0) divider = 1;

    int rc = emc2303_reg_write8(dev, EMC2303_REG_PWM_DIVIDE(fan), divider);
    return set_last(dev, rc);
}

/* ---------- public: status ---------- */

int emc2303_read_status(emc2303_t *dev,
                        uint8_t *fan_status,
                        uint8_t *stall_status,
                        uint8_t *spin_status,
                        uint8_t *drive_fail_status)
{
    if (!dev) return set_last(dev, EMC2303_E_BAD_ARG);

    uint8_t fs = 0, stl = 0, spn = 0, df = 0;
    int rc = emc2303_reg_read8(dev, EMC2303_REG_FAN_STATUS, &fs);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = emc2303_reg_read8(dev, EMC2303_REG_FAN_STALL_STATUS, &stl);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = emc2303_reg_read8(dev, EMC2303_REG_FAN_SPIN_STATUS, &spn);
    if (rc != EMC2303_OK) return set_last(dev, rc);
    rc = emc2303_reg_read8(dev, EMC2303_REG_DRIVE_FAIL_STATUS, &df);
    if (rc != EMC2303_OK) return set_last(dev, rc);

    /* Mask to implemented fans */
    uint8_t fan_mask = (uint8_t)((1u << EMC2303_MAX_FANS) - 1u);
    stl &= fan_mask;
    spn &= fan_mask;
    df  &= fan_mask;

    if (fan_status)       *fan_status       = fs;
    if (stall_status)     *stall_status     = stl;
    if (spin_status)      *spin_status      = spn;
    if (drive_fail_status)*drive_fail_status= df;

    return set_last(dev, EMC2303_OK);
}
