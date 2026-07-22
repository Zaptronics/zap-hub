#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AD7124_CHANNEL_COUNT          16u
#define AD7124_SETUP_COUNT             8u
#define AD7124_DATA_FULL_SCALE  0xFFFFFFu
#define AD7124_DATA_CODE_COUNT 0x1000000u
#define AD7124_DATA_MIDSCALE    0x800000u

/* Status register bits. */
#define AD7124_STATUS_RDY              (1u << 7)
#define AD7124_STATUS_ERROR_FLAG       (1u << 6)
#define AD7124_STATUS_POR_FLAG         (1u << 4)
#define AD7124_STATUS_CHANNEL_MASK      0x0Fu

/* ERROR / ERROR_EN register masks. */
#define AD7124_ERROR_LDO_CAP           (1u << 19)
#define AD7124_ERROR_ADC_CAL           (1u << 18)
#define AD7124_ERROR_ADC_CONV          (1u << 17)
#define AD7124_ERROR_ADC_SAT           (1u << 16)
#define AD7124_ERROR_AINP_OV           (1u << 15)
#define AD7124_ERROR_AINP_UV           (1u << 14)
#define AD7124_ERROR_AINM_OV           (1u << 13)
#define AD7124_ERROR_AINM_UV           (1u << 12)
#define AD7124_ERROR_REF_DET           (1u << 11)
#define AD7124_ERROR_DLDO_PSM          (1u << 9)
#define AD7124_ERROR_ALDO_PSM          (1u << 7)
#define AD7124_ERROR_SPI_IGNORE        (1u << 6)
#define AD7124_ERROR_SPI_SCLK_CNT      (1u << 5)
#define AD7124_ERROR_SPI_READ          (1u << 4)
#define AD7124_ERROR_SPI_WRITE         (1u << 3)
#define AD7124_ERROR_SPI_CRC           (1u << 2)
#define AD7124_ERROR_MM_CRC            (1u << 1)
#define AD7124_ERROR_ROM_CRC           (1u << 0)

/* Register addresses. */
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
    AD7124_REG_CHANNEL_0     = 0x09,
    AD7124_REG_CONFIG_0      = 0x19,
    AD7124_REG_FILTER_0      = 0x21,
    AD7124_REG_OFFSET_0      = 0x29,
    AD7124_REG_GAIN_0        = 0x31
} ad7124_reg_t;

static inline ad7124_reg_t AD7124_REG_CHANNEL(uint8_t channel)
{
    return (ad7124_reg_t)(AD7124_REG_CHANNEL_0 + channel);
}

static inline ad7124_reg_t AD7124_REG_CONFIG(uint8_t setup)
{
    return (ad7124_reg_t)(AD7124_REG_CONFIG_0 + setup);
}

static inline ad7124_reg_t AD7124_REG_FILTER(uint8_t setup)
{
    return (ad7124_reg_t)(AD7124_REG_FILTER_0 + setup);
}

static inline ad7124_reg_t AD7124_REG_OFFSET(uint8_t setup)
{
    return (ad7124_reg_t)(AD7124_REG_OFFSET_0 + setup);
}

static inline ad7124_reg_t AD7124_REG_GAIN(uint8_t setup)
{
    return (ad7124_reg_t)(AD7124_REG_GAIN_0 + setup);
}

typedef enum {
    AD7124_POWER_LOW  = 0,
    AD7124_POWER_MID  = 1,
    AD7124_POWER_FULL = 2
} ad7124_power_mode_t;

typedef enum {
    AD7124_MODE_CONTINUOUS       = 0x0,
    AD7124_MODE_SINGLE           = 0x1,
    AD7124_MODE_STANDBY          = 0x2,
    AD7124_MODE_POWERDOWN        = 0x3,
    AD7124_MODE_IDLE             = 0x4,
    AD7124_MODE_INT_ZERO_CAL     = 0x5,
    AD7124_MODE_INT_FULL_CAL     = 0x6,
    AD7124_MODE_SYS_ZERO_CAL     = 0x7,
    AD7124_MODE_SYS_FULL_CAL     = 0x8
} ad7124_operating_mode_t;

typedef enum {
    AD7124_REF_REFIN1   = 0,
    AD7124_REF_REFIN2   = 1,
    AD7124_REF_INTERNAL = 2,
    AD7124_REF_AVDD     = 3
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
    AD7124_FILTER_SINC4      = 0,
    AD7124_FILTER_SINC3      = 2,
    AD7124_FILTER_FAST_SINC4 = 4,
    AD7124_FILTER_FAST_SINC3 = 5,
    AD7124_FILTER_POST       = 7
} ad7124_filter_type_t;

typedef enum {
    AD7124_IOUT_OFF    = 0,
    AD7124_IOUT_50UA   = 1,
    AD7124_IOUT_100UA  = 2,
    AD7124_IOUT_250UA  = 3,
    AD7124_IOUT_500UA  = 4,
    AD7124_IOUT_750UA  = 5,
    AD7124_IOUT_1000UA = 6,
    AD7124_IOUT_0_1UA  = 7
} ad7124_iout_val_t;

typedef struct {
    bus_spi_t spi_bus;
    bool data_status_enabled;
    ad7124_power_mode_t power_mode;
    ad7124_operating_mode_t mode;
} ad7124_dev_t;

void ad7124_init(ad7124_dev_t *dev);

bool ad7124_reset(ad7124_dev_t *dev);
bool ad7124_read_id(ad7124_dev_t *dev, uint8_t *id_out);
bool ad7124_id_is_ad7124_8(uint8_t id);

bool ad7124_read_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t *value);
bool ad7124_read_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t *value);
bool ad7124_read_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t *value);
bool ad7124_write_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t value);
bool ad7124_write_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t value);
bool ad7124_write_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t value);

bool ad7124_read_status(ad7124_dev_t *dev, uint8_t *status);
bool ad7124_read_error(ad7124_dev_t *dev, uint32_t *error);
bool ad7124_config_error_enable(ad7124_dev_t *dev, uint32_t mask);

bool ad7124_config_adc_control(ad7124_dev_t *dev,
                               ad7124_power_mode_t power,
                               ad7124_operating_mode_t mode,
                               bool data_status,
                               bool internal_reference_enable,
                               bool cs_enable);
bool ad7124_set_mode(ad7124_dev_t *dev, ad7124_operating_mode_t mode);

bool ad7124_config_setup(ad7124_dev_t *dev,
                         uint8_t setup,
                         ad7124_ref_sel_t reference,
                         bool bipolar,
                         ad7124_pga_t pga,
                         bool ain_buffer_positive,
                         bool ain_buffer_negative,
                         bool ref_buffer_positive,
                         bool ref_buffer_negative,
                         uint8_t burnout);

bool ad7124_config_filter(ad7124_dev_t *dev,
                          uint8_t setup,
                          ad7124_filter_type_t filter,
                          uint8_t post_filter,
                          bool reject_60hz,
                          bool single_cycle,
                          uint16_t fs);

bool ad7124_config_channel(ad7124_dev_t *dev,
                           uint8_t channel,
                           bool enable,
                           uint8_t setup,
                           uint8_t ain_positive,
                           uint8_t ain_negative);
bool ad7124_set_channel_enabled(ad7124_dev_t *dev, uint8_t channel, bool enable);
bool ad7124_disable_all_channels(ad7124_dev_t *dev);

bool ad7124_set_iout(ad7124_dev_t *dev,
                     ad7124_iout_val_t iout0,
                     uint8_t iout0_ain,
                     ad7124_iout_val_t iout1,
                     uint8_t iout1_ain,
                     bool close_power_switch);
bool ad7124_disable_iouts(ad7124_dev_t *dev);
bool ad7124_set_bias_mask(ad7124_dev_t *dev, uint16_t ain_mask);

bool ad7124_wait_ready(ad7124_dev_t *dev, uint32_t timeout_us);
bool ad7124_read_sample(ad7124_dev_t *dev, uint32_t *code, uint8_t *channel_active);
bool ad7124_single_conversion(ad7124_dev_t *dev,
                              uint8_t channel,
                              uint32_t timeout_us,
                              uint32_t *code,
                              uint8_t *channel_active);
bool ad7124_calibrate_channel(ad7124_dev_t *dev,
                              uint8_t channel,
                              bool zero_scale,
                              bool full_scale,
                              uint32_t timeout_us);

uint8_t ad7124_pga_gain(ad7124_pga_t pga);
double ad7124_code_to_bipolar_voltage(uint32_t code,
                                      double reference_volts,
                                      ad7124_pga_t pga);
double ad7124_code_to_unipolar_ratio(uint32_t code, ad7124_pga_t pga);

#ifdef __cplusplus
}
#endif
