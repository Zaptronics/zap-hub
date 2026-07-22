#include <string.h>

#include "pico/stdlib.h"

#include "drivers/ad7124.h"

#define AD7124_RESET_BYTES 8u
#define AD7124_RESET_SETTLE_US 1000u

static uint8_t ad7124_build_comm(ad7124_reg_t reg, bool read)
{
    return (uint8_t)((read ? (1u << 6) : 0u) | ((uint8_t)reg & 0x3Fu));
}

static bool ad7124_write_bytes(ad7124_dev_t *dev,
                               ad7124_reg_t reg,
                               const uint8_t *data,
                               size_t length)
{
    if (!dev || (!data && length != 0u) || length > 4u) {
        return false;
    }

    uint8_t frame[5] = { ad7124_build_comm(reg, false), 0u, 0u, 0u, 0u };
    if (length != 0u) {
        memcpy(&frame[1], data, length);
    }
    return bus_spi_write(&dev->spi_bus, frame, length + 1u);
}

static bool ad7124_read_bytes(ad7124_dev_t *dev,
                              ad7124_reg_t reg,
                              uint8_t *data,
                              size_t length)
{
    if (!dev || !data || length == 0u || length > 4u) {
        return false;
    }

    uint8_t command = ad7124_build_comm(reg, true);
    if (!bus_spi_begin(&dev->spi_bus)) {
        return false;
    }

    bool ok = bus_spi_transfer(&dev->spi_bus, &command, NULL, 1u);
    if (ok) {
        ok = bus_spi_transfer(&dev->spi_bus, NULL, data, length);
    }
    bool ended = bus_spi_end(&dev->spi_bus);
    return ok && ended;
}

void ad7124_init(ad7124_dev_t *dev)
{
    if (!dev) {
        return;
    }
    dev->data_status_enabled = false;
    dev->power_mode = AD7124_POWER_LOW;
    dev->mode = AD7124_MODE_CONTINUOUS;
}

bool ad7124_reset(ad7124_dev_t *dev)
{
    if (!dev) {
        return false;
    }

    uint8_t ones[AD7124_RESET_BYTES];
    memset(ones, 0xFF, sizeof(ones));
    if (!bus_spi_write(&dev->spi_bus, ones, sizeof(ones))) {
        return false;
    }

    /* Register access is ignored while the power-on/reset sequence completes. */
    busy_wait_us(AD7124_RESET_SETTLE_US);
    dev->data_status_enabled = false;
    dev->power_mode = AD7124_POWER_LOW;
    dev->mode = AD7124_MODE_CONTINUOUS;
    return true;
}

bool ad7124_read_id(ad7124_dev_t *dev, uint8_t *id_out)
{
    return id_out && ad7124_read_reg8(dev, AD7124_REG_ID, id_out);
}

bool ad7124_id_is_ad7124_8(uint8_t id)
{
    /* DEVICE_ID occupies the high nibble; the low nibble is silicon revision. */
    return (id & 0xF0u) == 0x10u;
}

bool ad7124_read_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t *value)
{
    return value && ad7124_read_bytes(dev, reg, value, 1u);
}

bool ad7124_read_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t *value)
{
    if (!value) {
        return false;
    }
    uint8_t bytes[2];
    if (!ad7124_read_bytes(dev, reg, bytes, sizeof(bytes))) {
        return false;
    }
    *value = (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
    return true;
}

bool ad7124_read_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t *value)
{
    if (!value) {
        return false;
    }
    uint8_t bytes[3];
    if (!ad7124_read_bytes(dev, reg, bytes, sizeof(bytes))) {
        return false;
    }
    *value = ((uint32_t)bytes[0] << 16) |
             ((uint32_t)bytes[1] << 8) |
             (uint32_t)bytes[2];
    return true;
}

bool ad7124_write_reg8(ad7124_dev_t *dev, ad7124_reg_t reg, uint8_t value)
{
    return ad7124_write_bytes(dev, reg, &value, 1u);
}

bool ad7124_write_reg16(ad7124_dev_t *dev, ad7124_reg_t reg, uint16_t value)
{
    uint8_t bytes[2] = { (uint8_t)(value >> 8), (uint8_t)value };
    return ad7124_write_bytes(dev, reg, bytes, sizeof(bytes));
}

bool ad7124_write_reg24(ad7124_dev_t *dev, ad7124_reg_t reg, uint32_t value)
{
    uint8_t bytes[3] = {
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value
    };
    return ad7124_write_bytes(dev, reg, bytes, sizeof(bytes));
}

bool ad7124_read_status(ad7124_dev_t *dev, uint8_t *status)
{
    return ad7124_read_reg8(dev, AD7124_REG_STATUS, status);
}

bool ad7124_read_error(ad7124_dev_t *dev, uint32_t *error)
{
    return ad7124_read_reg24(dev, AD7124_REG_ERROR, error);
}

bool ad7124_config_error_enable(ad7124_dev_t *dev, uint32_t mask)
{
    return ad7124_write_reg24(dev, AD7124_REG_ERROR_EN, mask & 0x7FFFFFu);
}

bool ad7124_config_adc_control(ad7124_dev_t *dev,
                               ad7124_power_mode_t power,
                               ad7124_operating_mode_t mode,
                               bool data_status,
                               bool internal_reference_enable,
                               bool cs_enable)
{
    if (!dev || power > AD7124_POWER_FULL || mode > AD7124_MODE_SYS_FULL_CAL) {
        return false;
    }

    uint16_t value = 0u;
    if (data_status) {
        value |= (1u << 10);
    }
    if (cs_enable) {
        value |= (1u << 9);
    }
    if (internal_reference_enable) {
        value |= (1u << 8);
    }
    value |= (uint16_t)((uint16_t)power << 6);
    value |= (uint16_t)((uint16_t)mode << 2);

    if (!ad7124_write_reg16(dev, AD7124_REG_ADC_CONTROL, value)) {
        return false;
    }

    dev->data_status_enabled = data_status;
    dev->power_mode = power;
    dev->mode = mode;
    return true;
}

bool ad7124_set_mode(ad7124_dev_t *dev, ad7124_operating_mode_t mode)
{
    if (!dev || mode > AD7124_MODE_SYS_FULL_CAL) {
        return false;
    }

    uint16_t control = 0u;
    if (!ad7124_read_reg16(dev, AD7124_REG_ADC_CONTROL, &control)) {
        return false;
    }
    control &= (uint16_t)~(0x0Fu << 2);
    control |= (uint16_t)((uint16_t)mode << 2);
    if (!ad7124_write_reg16(dev, AD7124_REG_ADC_CONTROL, control)) {
        return false;
    }
    dev->mode = mode;
    return true;
}

bool ad7124_config_setup(ad7124_dev_t *dev,
                         uint8_t setup,
                         ad7124_ref_sel_t reference,
                         bool bipolar,
                         ad7124_pga_t pga,
                         bool ain_buffer_positive,
                         bool ain_buffer_negative,
                         bool ref_buffer_positive,
                         bool ref_buffer_negative,
                         uint8_t burnout)
{
    if (!dev || setup >= AD7124_SETUP_COUNT || reference > AD7124_REF_AVDD ||
        pga > AD7124_PGA_128 || burnout > 3u) {
        return false;
    }

    uint16_t value = 0u;
    value |= bipolar ? (1u << 11) : 0u;
    value |= (uint16_t)((uint16_t)burnout << 9);
    value |= ref_buffer_positive ? (1u << 8) : 0u;
    value |= ref_buffer_negative ? (1u << 7) : 0u;
    value |= ain_buffer_positive ? (1u << 6) : 0u;
    value |= ain_buffer_negative ? (1u << 5) : 0u;
    value |= (uint16_t)((uint16_t)reference << 3);
    value |= (uint16_t)pga;
    return ad7124_write_reg16(dev, AD7124_REG_CONFIG(setup), value);
}

bool ad7124_config_filter(ad7124_dev_t *dev,
                          uint8_t setup,
                          ad7124_filter_type_t filter,
                          uint8_t post_filter,
                          bool reject_60hz,
                          bool single_cycle,
                          uint16_t fs)
{
    if (!dev || setup >= AD7124_SETUP_COUNT || post_filter > 7u ||
        fs == 0u || fs > 2047u) {
        return false;
    }

    uint32_t value = ((uint32_t)filter << 21) |
                     (reject_60hz ? (1u << 20) : 0u) |
                     ((uint32_t)post_filter << 17) |
                     (single_cycle ? (1u << 16) : 0u) |
                     (uint32_t)fs;
    return ad7124_write_reg24(dev, AD7124_REG_FILTER(setup), value);
}

bool ad7124_config_channel(ad7124_dev_t *dev,
                           uint8_t channel,
                           bool enable,
                           uint8_t setup,
                           uint8_t ain_positive,
                           uint8_t ain_negative)
{
    if (!dev || channel >= AD7124_CHANNEL_COUNT || setup >= AD7124_SETUP_COUNT ||
        ain_positive > 31u || ain_negative > 31u) {
        return false;
    }

    uint16_t value = enable ? (1u << 15) : 0u;
    value |= (uint16_t)((uint16_t)setup << 12);
    value |= (uint16_t)((uint16_t)ain_positive << 5);
    value |= (uint16_t)ain_negative;
    return ad7124_write_reg16(dev, AD7124_REG_CHANNEL(channel), value);
}

bool ad7124_set_channel_enabled(ad7124_dev_t *dev, uint8_t channel, bool enable)
{
    if (!dev || channel >= AD7124_CHANNEL_COUNT) {
        return false;
    }

    uint16_t value = 0u;
    if (!ad7124_read_reg16(dev, AD7124_REG_CHANNEL(channel), &value)) {
        return false;
    }
    if (enable) {
        value |= (1u << 15);
    } else {
        value &= (uint16_t)~(1u << 15);
    }
    return ad7124_write_reg16(dev, AD7124_REG_CHANNEL(channel), value);
}

bool ad7124_disable_all_channels(ad7124_dev_t *dev)
{
    if (!dev) {
        return false;
    }

    for (uint8_t channel = 0u; channel < AD7124_CHANNEL_COUNT; channel++) {
        if (!ad7124_set_channel_enabled(dev, channel, false)) {
            return false;
        }
    }
    return true;
}

bool ad7124_set_iout(ad7124_dev_t *dev,
                     ad7124_iout_val_t iout0,
                     uint8_t iout0_ain,
                     ad7124_iout_val_t iout1,
                     uint8_t iout1_ain,
                     bool close_power_switch)
{
    if (!dev || iout0 > AD7124_IOUT_0_1UA || iout1 > AD7124_IOUT_0_1UA ||
        iout0_ain > 15u || iout1_ain > 15u) {
        return false;
    }

    uint32_t value = close_power_switch ? (1u << 15) : 0u;
    value |= (uint32_t)iout1 << 11;
    value |= (uint32_t)iout0 << 8;
    value |= (uint32_t)iout1_ain << 4;
    value |= (uint32_t)iout0_ain;
    return ad7124_write_reg24(dev, AD7124_REG_IO_CONTROL_1, value);
}

bool ad7124_disable_iouts(ad7124_dev_t *dev)
{
    return ad7124_set_iout(dev,
                           AD7124_IOUT_OFF, 0u,
                           AD7124_IOUT_OFF, 0u,
                           false);
}

bool ad7124_set_bias_mask(ad7124_dev_t *dev, uint16_t ain_mask)
{
    return ad7124_write_reg16(dev, AD7124_REG_IO_CONTROL_2, ain_mask);
}

bool ad7124_wait_ready(ad7124_dev_t *dev, uint32_t timeout_us)
{
    if (!dev || timeout_us == 0u) {
        return false;
    }

    absolute_time_t deadline = make_timeout_time_us(timeout_us);
    do {
        uint8_t status = 0u;
        if (!ad7124_read_status(dev, &status)) {
            return false;
        }
        if ((status & AD7124_STATUS_RDY) == 0u) {
            return true;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));

    return false;
}

bool ad7124_read_sample(ad7124_dev_t *dev, uint32_t *code, uint8_t *channel_active)
{
    if (!dev || !code) {
        return false;
    }

    if (dev->data_status_enabled) {
        uint8_t command = ad7124_build_comm(AD7124_REG_DATA, true);
        uint8_t response[4] = { 0u, 0u, 0u, 0u };
        if (!bus_spi_begin(&dev->spi_bus)) {
            return false;
        }
        bool ok = bus_spi_transfer(&dev->spi_bus, &command, NULL, 1u);
        if (ok) {
            ok = bus_spi_transfer(&dev->spi_bus, NULL, response, sizeof(response));
        }
        bool ended = bus_spi_end(&dev->spi_bus);
        if (!ok || !ended) {
            return false;
        }

        *code = ((uint32_t)response[0] << 16) |
                ((uint32_t)response[1] << 8) |
                response[2];
        if (channel_active) {
            *channel_active = response[3] & AD7124_STATUS_CHANNEL_MASK;
        }
        return true;
    }

    if (!ad7124_read_reg24(dev, AD7124_REG_DATA, code)) {
        return false;
    }
    if (channel_active) {
        uint8_t status = 0u;
        if (!ad7124_read_status(dev, &status)) {
            return false;
        }
        *channel_active = status & AD7124_STATUS_CHANNEL_MASK;
    }
    return true;
}

bool ad7124_single_conversion(ad7124_dev_t *dev,
                              uint8_t channel,
                              uint32_t timeout_us,
                              uint32_t *code,
                              uint8_t *channel_active)
{
    if (!dev || !code || channel >= AD7124_CHANNEL_COUNT) {
        return false;
    }

    if (!ad7124_disable_all_channels(dev) ||
        !ad7124_set_channel_enabled(dev, channel, true) ||
        !ad7124_set_mode(dev, AD7124_MODE_SINGLE) ||
        !ad7124_wait_ready(dev, timeout_us)) {
        (void)ad7124_set_mode(dev, AD7124_MODE_STANDBY);
        return false;
    }

    uint8_t active = 0xFFu;
    bool ok = ad7124_read_sample(dev, code, &active);
    if (ok && dev->data_status_enabled && active != channel) {
        ok = false;
    }
    if (channel_active) {
        *channel_active = active;
    }
    (void)ad7124_set_mode(dev, AD7124_MODE_STANDBY);
    return ok;
}

bool ad7124_calibrate_channel(ad7124_dev_t *dev,
                              uint8_t channel,
                              bool zero_scale,
                              bool full_scale,
                              uint32_t timeout_us)
{
    if (!dev || channel >= AD7124_CHANNEL_COUNT || (!zero_scale && !full_scale)) {
        return false;
    }

    if (!ad7124_disable_all_channels(dev) ||
        !ad7124_set_channel_enabled(dev, channel, true)) {
        return false;
    }

    bool ok = true;
    if (zero_scale) {
        ok = ad7124_set_mode(dev, AD7124_MODE_INT_ZERO_CAL) &&
             ad7124_wait_ready(dev, timeout_us);
    }
    if (ok && full_scale) {
        ok = ad7124_set_mode(dev, AD7124_MODE_INT_FULL_CAL) &&
             ad7124_wait_ready(dev, timeout_us);
    }
    (void)ad7124_set_mode(dev, AD7124_MODE_STANDBY);
    return ok;
}

uint8_t ad7124_pga_gain(ad7124_pga_t pga)
{
    if (pga > AD7124_PGA_128) {
        return 1u;
    }
    return (uint8_t)(1u << (uint8_t)pga);
}

double ad7124_code_to_bipolar_voltage(uint32_t code,
                                      double reference_volts,
                                      ad7124_pga_t pga)
{
    double normalized = ((double)(code & AD7124_DATA_FULL_SCALE) /
                         (double)AD7124_DATA_MIDSCALE) - 1.0;
    return normalized * reference_volts / (double)ad7124_pga_gain(pga);
}

double ad7124_code_to_unipolar_ratio(uint32_t code, ad7124_pga_t pga)
{
    return ((double)(code & AD7124_DATA_FULL_SCALE) /
            (double)AD7124_DATA_CODE_COUNT) /
           (double)ad7124_pga_gain(pga);
}
