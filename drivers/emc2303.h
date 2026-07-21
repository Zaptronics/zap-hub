/* emc2303.h
 *
 * Minimal-but-complete EMC2303 (EMC2301/2/3/5 family) driver for Raspberry Pi Pico SDK.
 *
 * - Uses plain I2C register read/write (no SMBus block ops required)
 * - Provides full register map + bit definitions
 * - Simple high-level helpers (PWM %, RPM read, closed-loop RPM target)
 *
 * Typical use:
 *   emc2303_t dev = {
 *     .i2c = i2c0,
 *     .i2c_addr = 0x2E,      // common default (depends on ADDR_SEL)
 *     .i2c_baud_hz = 400000, // optional; 0 = don't touch
 *     .tach_clk_hz = 32768,  // internal/external tach clock rate used for RPM math
 *   };
 *   emc2303_init(&dev);
 *   emc2303_fan_set_pwm_percent_x100(&dev, 1, 5000); // 50.00%
 *   uint32_t rpm;
 *   emc2303_fan_get_rpm(&dev, 1, &rpm);
 */

#pragma once

#include "ztmb/bus.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Compile-time configuration ---------- */
#ifndef EMC2303_MAX_FANS
#define EMC2303_MAX_FANS 3u
#endif

/* ---------- Error codes ---------- */
typedef enum {
    EMC2303_OK          = true,
    EMC2303_E_BAD_ARG   = -1,
    EMC2303_E_I2C       = -2,
    EMC2303_E_ID        = -3,
    EMC2303_E_LOCKED    = -4
} emc2303_result_t;

/* ---------- Device context (one per controller instance) ---------- */
typedef struct {
    bus_i2c_t   i2c_bus;
    uint32_t    tach_clk_hz;    /* used for RPM conversions; set to 32768 if unsure */
    int         last_error;     /* last driver error (emc2303_result_t) */
} emc2303_t;

/* ---------- Register map (complete set used by EMC2303) ---------- */
#define EMC2303_REG_CONFIG                  0x20
#define EMC2303_REG_FAN_STATUS              0x24
#define EMC2303_REG_FAN_STALL_STATUS        0x25
#define EMC2303_REG_FAN_SPIN_STATUS         0x26
#define EMC2303_REG_DRIVE_FAIL_STATUS       0x27
#define EMC2303_REG_FAN_INT_ENABLE          0x29
#define EMC2303_REG_PWM_POLARITY_CONFIG     0x2A
#define EMC2303_REG_PWM_OUTPUT_CONFIG       0x2B
#define EMC2303_REG_PWM_BASEF45             0x2C
#define EMC2303_REG_PWM_BASEF123            0x2D

/* Per-fan register base (fan = 1..5 in datasheet, but EMC2303 has 1..3) */
#define EMC2303_FAN_BASE(fan)               (uint8_t)(0x30u + (uint8_t)(((fan) - 1u) * 0x10u))

#define EMC2303_REG_FAN_DRIVE_SETTING(fan)  (uint8_t)(EMC2303_FAN_BASE(fan) + 0x00u) /* 30/40/50 */
#define EMC2303_REG_PWM_DIVIDE(fan)         (uint8_t)(EMC2303_FAN_BASE(fan) + 0x01u) /* 31/41/51 */
#define EMC2303_REG_FAN_CONFIG1(fan)        (uint8_t)(EMC2303_FAN_BASE(fan) + 0x02u) /* 32/42/52 */
#define EMC2303_REG_FAN_CONFIG2(fan)        (uint8_t)(EMC2303_FAN_BASE(fan) + 0x03u) /* 33/43/53 */
#define EMC2303_REG_PID_GAIN(fan)           (uint8_t)(EMC2303_FAN_BASE(fan) + 0x05u) /* 35/45/55 */
#define EMC2303_REG_SPINUP_CONFIG(fan)      (uint8_t)(EMC2303_FAN_BASE(fan) + 0x06u) /* 36/46/56 */
#define EMC2303_REG_MAX_STEP(fan)           (uint8_t)(EMC2303_FAN_BASE(fan) + 0x07u) /* 37/47/57 */
#define EMC2303_REG_MIN_DRIVE(fan)          (uint8_t)(EMC2303_FAN_BASE(fan) + 0x08u) /* 38/48/58 */
#define EMC2303_REG_VALID_TACH(fan)         (uint8_t)(EMC2303_FAN_BASE(fan) + 0x09u) /* 39/49/59 */
#define EMC2303_REG_DRIVE_FAIL_BAND_L(fan)  (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Au) /* 3A/4A/5A */
#define EMC2303_REG_DRIVE_FAIL_BAND_H(fan)  (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Bu) /* 3B/4B/5B */
#define EMC2303_REG_TACH_TARGET_L(fan)      (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Cu) /* 3C/4C/5C */
#define EMC2303_REG_TACH_TARGET_H(fan)      (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Du) /* 3D/4D/5D */
#define EMC2303_REG_TACH_READING_H(fan)     (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Eu) /* 3E/4E/5E */
#define EMC2303_REG_TACH_READING_L(fan)     (uint8_t)(EMC2303_FAN_BASE(fan) + 0x0Fu) /* 3F/4F/5F */

/* ID / lock registers */
#define EMC2303_REG_SOFTWARE_LOCK           0xEF
#define EMC2303_REG_PRODUCT_FEATURES        0xFC /* present on EMC2303/5 */
#define EMC2303_REG_PRODUCT_ID              0xFD
#define EMC2303_REG_MANUFACTURER_ID         0xFE
#define EMC2303_REG_REVISION                0xFF

/* ---------- Bit definitions ---------- */
/* CONFIG (0x20) */
#define EMC2303_CONFIG_MASK_ALERT           (1u << 7)
#define EMC2303_CONFIG_DIS_TO               (1u << 6) /* disable SMBus timeout (I2C compatible) */
#define EMC2303_CONFIG_WD_EN                (1u << 5)
#define EMC2303_CONFIG_DRECK                (1u << 1)
#define EMC2303_CONFIG_USECK                (1u << 0)

/* FAN_STATUS (0x24) */
#define EMC2303_FAN_STATUS_WATCH            (1u << 7) /* RC */
#define EMC2303_FAN_STATUS_DVFAIL           (1u << 2) /* RC */
#define EMC2303_FAN_STATUS_FNSPIN           (1u << 1) /* RC */
#define EMC2303_FAN_STATUS_FNSTL            (1u << 0) /* RC */

/* FAN stall/spin/drivefail bits: bit0=fan1, bit1=fan2, bit2=fan3 ... */
#define EMC2303_FAN_BIT(fan)                (1u << ((fan) - 1u))

/* FAN_INT_ENABLE (0x29) */
#define EMC2303_FAN_INT_EN_BIT(fan)         (1u << ((fan) - 1u))

/* PWM_POLARITY_CONFIG (0x2A) */
#define EMC2303_PWM_POLARITY_BIT(fan)       (1u << ((fan) - 1u))

/* PWM_OUTPUT_CONFIG (0x2B) */
#define EMC2303_PWM_OUTPUT_TYPE_BIT(fan)    (1u << ((fan) - 1u)) /* 1=push-pull, 0=open-drain */

/* FAN_CONFIG1 */
#define EMC2303_FANCFG1_ENAG                (1u << 7)
#define EMC2303_FANCFG1_RNG_SHIFT           5u
#define EMC2303_FANCFG1_RNG_MASK            (3u << EMC2303_FANCFG1_RNG_SHIFT)
#define EMC2303_FANCFG1_EDG_SHIFT           3u
#define EMC2303_FANCFG1_EDG_MASK            (3u << EMC2303_FANCFG1_EDG_SHIFT)
#define EMC2303_FANCFG1_UDT_SHIFT           0u
#define EMC2303_FANCFG1_UDT_MASK            (7u << EMC2303_FANCFG1_UDT_SHIFT)

/* FAN_CONFIG2 */
#define EMC2303_FANCFG2_ENRC                (1u << 6) /* only when ENAG=0 */
#define EMC2303_FANCFG2_GHEN                (1u << 5)
#define EMC2303_FANCFG2_DPT_SHIFT           3u
#define EMC2303_FANCFG2_DPT_MASK            (3u << EMC2303_FANCFG2_DPT_SHIFT)
#define EMC2303_FANCFG2_ERG_SHIFT           1u
#define EMC2303_FANCFG2_ERG_MASK            (3u << EMC2303_FANCFG2_ERG_SHIFT)

/* SOFTWARE_LOCK (0xEF) */
#define EMC2303_SWLOCK_LOCK                 (1u << 0)

/* PRODUCT_ID (0xFD) */
#define EMC2303_PRODUCT_ID_FIXED_MASK       0xFCu
#define EMC2303_PRODUCT_ID_FIXED_VALUE      0x34u /* bits[7:2] fixed, bits[1:0]=device */
#define EMC2303_PRODUCT_ID_EMC2303          0x35u /* full byte for EMC2303 */
#define EMC2303_MANUFACTURER_ID_VALUE       0x5Du

/* ---------- Helpful enums (simple configuration) ---------- */
typedef enum {
    EMC2303_PWM_BASE_26KHZ   = 0, /* register code 00 */
    EMC2303_PWM_BASE_19K53HZ = 1, /* register code 01 */
    EMC2303_PWM_BASE_4K882HZ = 2, /* register code 10 */
    EMC2303_PWM_BASE_2K441HZ = 3  /* register code 11 */
} emc2303_pwm_base_t;

typedef enum {
    EMC2303_RANGE_MIN_500RPM_M1  = 0, /* RNG=00 */
    EMC2303_RANGE_MIN_1000RPM_M2 = 1, /* RNG=01 */
    EMC2303_RANGE_MIN_2000RPM_M4 = 2, /* RNG=10 */
    EMC2303_RANGE_MIN_4000RPM_M8 = 3  /* RNG=11 */
} emc2303_range_t;

/* For typical 4-wire PC fans, "2 poles" / EDG=01 is the common default */
typedef enum {
    EMC2303_POLES_1 = 1,
    EMC2303_POLES_2 = 2,
    EMC2303_POLES_3 = 3,
    EMC2303_POLES_4 = 4
} emc2303_poles_t;

typedef enum {
    EMC2303_UPDATE_100MS  = 0,
    EMC2303_UPDATE_200MS  = 1,
    EMC2303_UPDATE_300MS  = 2,
    EMC2303_UPDATE_400MS  = 3,
    EMC2303_UPDATE_500MS  = 4,
    EMC2303_UPDATE_800MS  = 5,
    EMC2303_UPDATE_1200MS = 6,
    EMC2303_UPDATE_1600MS = 7
} emc2303_update_t;

/* ---------- Core / low-level API (full register access) ---------- */
int emc2303_reg_read8(emc2303_t *dev, uint8_t reg, uint8_t *val);
int emc2303_reg_write8(emc2303_t *dev, uint8_t reg, uint8_t val);
int emc2303_reg_update8(emc2303_t *dev, uint8_t reg, uint8_t mask, uint8_t value);

/* 13-bit packed values used by tach target/reading and drive-fail band registers */
int emc2303_reg_read13(emc2303_t *dev, uint8_t reg_high, uint8_t reg_low, uint16_t *val13);
int emc2303_reg_write13(emc2303_t *dev, uint8_t reg_high, uint8_t reg_low, uint16_t val13, bool low_then_high);

/* ---------- High-level “simple to use” API ---------- */
int emc2303_pwm1_toggle_test(emc2303_t *dev);
int emc2303_init(emc2303_t *dev, i2c_inst_t *i2c, uint8_t addr_7bit);

/* IDs */
int emc2303_read_ids(emc2303_t *dev, uint8_t *product_id, uint8_t *mfg_id, uint8_t *revision);

/* Direct PWM drive (ENAG=0) */
int emc2303_fan_set_pwm_raw(emc2303_t *dev, uint8_t fan, uint8_t setting);
int emc2303_fan_set_pwm_percent_x100(emc2303_t *dev, uint8_t fan, uint16_t percent_x100);

/* Tach read */
int emc2303_fan_get_tach_counts(emc2303_t *dev, uint8_t fan, uint16_t *tach_count13);
int emc2303_fan_get_rpm(emc2303_t *dev, uint8_t fan, uint32_t *rpm);

/* Closed-loop RPM target (ENAG=1) */
int emc2303_fan_enable_closed_loop(emc2303_t *dev, uint8_t fan, bool enable);
int emc2303_fan_set_target_rpm(emc2303_t *dev, uint8_t fan, uint32_t rpm);

/* Common config helpers */
int emc2303_fan_set_range(emc2303_t *dev, uint8_t fan, emc2303_range_t range);
int emc2303_fan_set_poles(emc2303_t *dev, uint8_t fan, emc2303_poles_t poles);
int emc2303_fan_set_update_interval(emc2303_t *dev, uint8_t fan, emc2303_update_t update);

int emc2303_set_pwm_base(emc2303_t *dev, uint8_t fan, emc2303_pwm_base_t base);
int emc2303_set_pwm_divide(emc2303_t *dev, uint8_t fan, uint8_t divider);

/* Status */
int emc2303_read_status(emc2303_t *dev,
                        uint8_t *fan_status,
                        uint8_t *stall_status,
                        uint8_t *spin_status,
                        uint8_t *drive_fail_status);

#ifdef __cplusplus
}
#endif
