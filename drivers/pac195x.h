// pac195x.h
#pragma once


#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif



// ----------------------------
// Register map (Table 7-1)
// ----------------------------
#define PAC195X_REG_REFRESH               0x00u  // SEND (client addr)
#define PAC195X_REG_CTRL                  0x01u  // R/W, 2
#define PAC195X_REG_ACC_COUNT             0x02u  // R/W, 4
#define PAC195X_REG_VACC1                 0x03u  // R,   7
#define PAC195X_REG_VACC2                 0x04u  // R,   7
#define PAC195X_REG_VACC3                 0x05u  // R,   7
#define PAC195X_REG_VACC4                 0x06u  // R,   7
#define PAC195X_REG_VBUS1                 0x07u  // R,   2
#define PAC195X_REG_VBUS2                 0x08u
#define PAC195X_REG_VBUS3                 0x09u
#define PAC195X_REG_VBUS4                 0x0Au
#define PAC195X_REG_VSENSE1               0x0Bu  // R,   2
#define PAC195X_REG_VSENSE2               0x0Cu
#define PAC195X_REG_VSENSE3               0x0Du
#define PAC195X_REG_VSENSE4               0x0Eu
#define PAC195X_REG_VBUS1_AVG             0x0Fu  // R,   2
#define PAC195X_REG_VBUS2_AVG             0x10u
#define PAC195X_REG_VBUS3_AVG             0x11u
#define PAC195X_REG_VBUS4_AVG             0x12u
#define PAC195X_REG_VSENSE1_AVG           0x13u  // R,   2
#define PAC195X_REG_VSENSE2_AVG           0x14u
#define PAC195X_REG_VSENSE3_AVG           0x15u
#define PAC195X_REG_VSENSE4_AVG           0x16u
#define PAC195X_REG_VPOWER1               0x17u  // R,   4 (bits 31:2 contain VPOWER[29:0])
#define PAC195X_REG_VPOWER2               0x18u
#define PAC195X_REG_VPOWER3               0x19u
#define PAC195X_REG_VPOWER4               0x1Au
#define PAC195X_REG_RESERVED_1B           0x1Bu  // (not used)
#define PAC195X_REG_SMBUS_SETTINGS        0x1Cu  // R/W, 1
#define PAC195X_REG_NEG_PWR_FSR           0x1Du  // R/W, 2
#define PAC195X_REG_REFRESH_G             0x1Eu  // SEND (general call addr)
#define PAC195X_REG_REFRESH_V             0x1Fu  // SEND (client addr)
#define PAC195X_REG_SLOW                  0x20u  // R/W, 1
#define PAC195X_REG_CTRL_ACT              0x21u  // R,   2
#define PAC195X_REG_NEG_PWR_FSR_ACT       0x22u  // R,   2
#define PAC195X_REG_CTRL_LAT              0x23u  // R,   2
#define PAC195X_REG_NEG_PWR_FSR_LAT       0x24u  // R,   2
#define PAC195X_REG_ACCUM_CONFIG          0x25u  // R/W, 1
#define PAC195X_REG_ALERT_STATUS          0x26u  // R/C, 3
#define PAC195X_REG_SLOW_ALERT1           0x27u  // R/W, 3
#define PAC195X_REG_GPIO_ALERT2           0x28u  // R/W, 3
#define PAC195X_REG_ACC_FULLNESS_LIMITS   0x29u  // R/W, 2
#define PAC195X_REG_OC_LIMIT1             0x30u  // R/W, 2
#define PAC195X_REG_OC_LIMIT2             0x31u
#define PAC195X_REG_OC_LIMIT3             0x32u
#define PAC195X_REG_OC_LIMIT4             0x33u
#define PAC195X_REG_UC_LIMIT1             0x34u  // R/W, 2
#define PAC195X_REG_UC_LIMIT2             0x35u
#define PAC195X_REG_UC_LIMIT3             0x36u
#define PAC195X_REG_UC_LIMIT4             0x37u
#define PAC195X_REG_OP_LIMIT1             0x38u  // R/W, 3
#define PAC195X_REG_OP_LIMIT2             0x39u
#define PAC195X_REG_OP_LIMIT3             0x3Au
#define PAC195X_REG_OP_LIMIT4             0x3Bu
#define PAC195X_REG_OV_LIMIT1             0x3Cu  // R/W, 2
#define PAC195X_REG_OV_LIMIT2             0x3Du
#define PAC195X_REG_OV_LIMIT3             0x3Eu
#define PAC195X_REG_OV_LIMIT4             0x3Fu
#define PAC195X_REG_UV_LIMIT1             0x40u  // R/W, 2
#define PAC195X_REG_UV_LIMIT2             0x41u
#define PAC195X_REG_UV_LIMIT3             0x42u
#define PAC195X_REG_UV_LIMIT4             0x43u
#define PAC195X_REG_OC_LIMIT_NSAMPLES     0x44u  // R/W, 1
#define PAC195X_REG_UC_LIMIT_NSAMPLES     0x45u
#define PAC195X_REG_OP_LIMIT_NSAMPLES     0x46u
#define PAC195X_REG_OV_LIMIT_NSAMPLES     0x47u
#define PAC195X_REG_UV_LIMIT_NSAMPLES     0x48u
#define PAC195X_REG_ALERT_ENABLE          0x49u  // R/W, 3
#define PAC195X_REG_ACCUM_CONFIG_ACT      0x4Au  // R,   1
#define PAC195X_REG_ACCUM_CONFIG_LAT      0x4Bu  // R,   1
#define PAC195X_REG_PRODUCT_ID            0xFDu  // R,   1
#define PAC195X_REG_MANUFACTURER_ID       0xFEu  // R,   1 (0x54)
#define PAC195X_REG_REVISION_ID           0xFFu  // R,   1 (0x02 initial)

// CTRL bitfields
#define PAC195X_CTRL_SAMPLE_MODE_SHIFT    12u
#define PAC195X_CTRL_SAMPLE_MODE_MASK     (0xFu << PAC195X_CTRL_SAMPLE_MODE_SHIFT)
#define PAC195X_CTRL_GPIO_ALERT2_SHIFT    10u
#define PAC195X_CTRL_GPIO_ALERT2_MASK     (0x3u << PAC195X_CTRL_GPIO_ALERT2_SHIFT)
#define PAC195X_CTRL_SLOW_ALERT1_SHIFT    8u
#define PAC195X_CTRL_SLOW_ALERT1_MASK     (0x3u << PAC195X_CTRL_SLOW_ALERT1_SHIFT)
#define PAC195X_CTRL_CH_OFF_SHIFT         4u
#define PAC195X_CTRL_CH_OFF_MASK          (0xFu << PAC195X_CTRL_CH_OFF_SHIFT)

// SMBUS_SETTINGS bits (we keep it “plain I2C”: BYTE_COUNT=0)
#define PAC195X_SMBUS_GPIO_DATA2_BIT      (1u << 7)
#define PAC195X_SMBUS_GPIO_DATA1_BIT      (1u << 6)
#define PAC195X_SMBUS_ANY_ALERT_BIT       (1u << 5)
#define PAC195X_SMBUS_POR_BIT             (1u << 4)
#define PAC195X_SMBUS_TIMEOUT_BIT         (1u << 3)
#define PAC195X_SMBUS_BYTE_COUNT_BIT      (1u << 2)
#define PAC195X_SMBUS_NO_SKIP_BIT         (1u << 1)
#define PAC195X_SMBUS_I2C_HISPEED_BIT     (1u << 0)

typedef enum {
    PAC195X_OK = 0,
    PAC195X_ERR_ARG = -1,
    PAC195X_ERR_I2C = -2,
    PAC195X_ERR_ID  = -3
} pac195x_status_t;

// Sampling modes (CTRL.SAMPLE_MODE[3:0])
typedef enum {
    PAC195X_SAMPLE_ADAPTIVE_1024 = 0x0,
    PAC195X_SAMPLE_ADAPTIVE_256  = 0x1,
    PAC195X_SAMPLE_ADAPTIVE_64   = 0x2,
    PAC195X_SAMPLE_ADAPTIVE_8    = 0x3,
    PAC195X_SAMPLE_1024          = 0x4,
    PAC195X_SAMPLE_256           = 0x5,
    PAC195X_SAMPLE_64            = 0x6,
    PAC195X_SAMPLE_8             = 0x7,
    PAC195X_SAMPLE_SINGLE_SHOT   = 0x8,
    PAC195X_SAMPLE_SINGLE_SHOT_8X= 0x9,
    PAC195X_SAMPLE_FAST          = 0xA,
    PAC195X_SAMPLE_BURST         = 0xB,
    PAC195X_SAMPLE_SLEEP         = 0xF
} pac195x_sample_mode_t;

// NEG_PWR_FSR config (2-bit fields per channel)
typedef enum {
    PAC195X_VSENSE_UNIPOLAR_100mV = 0x0, // +100mV..0
    PAC195X_VSENSE_BIPOLAR_100mV  = 0x1, // +100mV..-100mV
    PAC195X_VSENSE_BIPOLAR_50mV   = 0x2  // +50mV..-50mV
} pac195x_vsense_range_t;

typedef enum {
    PAC195X_VBUS_UNIPOLAR_32V = 0x0, // +32V..0
    PAC195X_VBUS_BIPOLAR_32V  = 0x1, // +32V..-32V
    PAC195X_VBUS_BIPOLAR_16V  = 0x2  // +16V..-16V
} pac195x_vbus_range_t;

// What to send before reading results
typedef enum {
    PAC195X_REFRESH_RESET_ACCUM = 0,  // REFRESH (0x00)
    PAC195X_REFRESH_NO_RESET    = 1   // REFRESH_V (0x1F)
} pac195x_refresh_kind_t;

typedef struct {
    // --- required ---
    bus_i2c_t i2c_bus;

    // --- desired configuration (applied in pac195x_init if the flags are set) ---
    bool set_smbus_settings;
    bool no_skip;           // SMBUS_SETTINGS.NO_SKIP (useful if you read disabled channels and expect 0xFF)
    bool byte_count;        // keep false for plain I2C
    bool i2c_hispeed;       // 3.4MHz mode (usually false)

    bool set_ctrl;
    pac195x_sample_mode_t sample_mode; // if set_ctrl
    uint8_t enabled_mask;  // bit0=CH1 .. bit3=CH4, 1=enabled
    uint8_t slow_alert1_cfg; // CTRL.SLOW_ALERT1 (0..3)
    uint8_t gpio_alert2_cfg; // CTRL.GPIO_ALERT2 (0..3)

    bool set_ranges;
    pac195x_vbus_range_t   vbus_range[4];
    pac195x_vsense_range_t vsense_range[4];

    bool set_accum_config;
    uint8_t accum_config;  // ACCUM_CONFIG (2 bits per channel)

    // Shunt resistors for conversion helpers (micro-ohms). 0 => conversion disabled for that channel
    uint32_t shunt_uohm[4];

    // On init, whether to REFRESH (reset accumulators) or REFRESH_V (don’t reset)
    pac195x_refresh_kind_t init_refresh_kind;

    // --- runtime (filled by driver) ---
    uint8_t product_id;
    uint8_t manufacturer_id;
    uint8_t revision_id;
    uint8_t channels; // 1..4

    // cached “active” config (read back)
    uint16_t ctrl_act;
    uint16_t neg_pwr_fsr_act;
    uint8_t  accum_config_act;
} pac195x_t;

typedef struct {
    // Raw reads
    uint16_t vbus_raw;
    uint16_t vsense_raw;
    uint32_t vpower_raw; // full 32b as read; (vpower_raw >> 2) contains VPOWER[29:0]
    // Scaled (derived)
    int32_t  vbus_uV;
    int32_t  vsense_uV;
    int32_t  current_uA; // needs shunt_uohm[ch] != 0
    int64_t  power_uW;   // computed from vbus_uV and current_uA
} pac195x_channel_reading_t;

// ----------------------------
// Public API
// ----------------------------
int pac195x_init(pac195x_t *dev);

// Send REFRESH or REFRESH_V and (optionally) wait the required latch time.
int pac195x_refresh(pac195x_t *dev, pac195x_refresh_kind_t kind, bool wait_1ms);

// Refresh the cached active registers (CTRL_ACT, NEG_PWR_FSR_ACT, ACCUM_CONFIG_ACT)
int pac195x_read_active_config(pac195x_t *dev);

// Read one channel. If averaged=true, reads VBUSn_AVG/VSENSEn_AVG.
int pac195x_read_channel(pac195x_t *dev, uint8_t ch_1based, bool averaged, pac195x_channel_reading_t *out);

// Convenience: latch with REFRESH_V, wait, then read all channels.
int pac195x_read_all_snapshot(pac195x_t *dev, bool averaged, pac195x_channel_reading_t out[4]);

// Accumulator reads
int pac195x_read_acc_count(pac195x_t *dev, uint32_t *acc_count);
int pac195x_read_vacc(pac195x_t *dev, uint8_t ch_1based, int64_t *vacc_signed_56);

// ----------------------------
// Full register access helpers
// ----------------------------
int pac195x_reg_read(pac195x_t *dev, uint8_t reg, uint8_t *buf, uint32_t len);
int pac195x_reg_write(pac195x_t *dev, uint8_t reg, const uint8_t *buf, uint32_t len);

int pac195x_reg_read_u8 (pac195x_t *dev, uint8_t reg, uint8_t  *val);
int pac195x_reg_write_u8(pac195x_t *dev, uint8_t reg, uint8_t   val);

int pac195x_reg_read_u16 (pac195x_t *dev, uint8_t reg, uint16_t *val);
int pac195x_reg_write_u16(pac195x_t *dev, uint8_t reg, uint16_t  val);

int pac195x_reg_read_u24 (pac195x_t *dev, uint8_t reg, uint32_t *val24);
int pac195x_reg_write_u24(pac195x_t *dev, uint8_t reg, uint32_t  val24);

int pac195x_reg_read_u32 (pac195x_t *dev, uint8_t reg, uint32_t *val);
int pac195x_reg_write_u32(pac195x_t *dev, uint8_t reg, uint32_t  val);

#ifdef __cplusplus
}
#endif
