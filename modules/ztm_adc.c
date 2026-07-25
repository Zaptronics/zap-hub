#include "modules/ztm_adc.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "ztmb/log.h"

#define ZTM_ADC_TC_PGA AD7124_PGA_32

/*
 * Diagnostics useful during bench testing without enabling intrusive LDO tests.
 *
 * Do not enable REF_DET for this board. The RTD reference is generated only
 * while an excitation current is active. The board uses 500 uA through the
 * fitted 2 kohm resistor, producing about 1.0 V during a measurement. The
 * reference detector is still unsuitable because it continuously reports the
 * intentionally absent reference whenever the one-shot excitation is off.
 */
#define ZTM_ADC_ERROR_ENABLE_MASK \
    (AD7124_ERROR_ADC_CAL       | AD7124_ERROR_ADC_CONV     | \
     AD7124_ERROR_ADC_SAT       | \
     AD7124_ERROR_SPI_IGNORE    | AD7124_ERROR_SPI_SCLK_CNT | \
     AD7124_ERROR_SPI_READ      | AD7124_ERROR_SPI_WRITE    | \
     AD7124_ERROR_MM_CRC        | AD7124_ERROR_ROM_CRC)

static const uint8_t g_tc_ain_positive[ZTM_ADC_TC_COUNT] = { 0u, 2u, 4u };
static const uint8_t g_tc_ain_negative[ZTM_ADC_TC_COUNT] = { 1u, 3u, 5u };

ztm_adc_t ztm_adc_1;

static bool ztm_adc_try_lock(ztm_adc_t *module)
{
    if (!module) {
        return false;
    }
    return !__atomic_test_and_set(&module->busy, __ATOMIC_ACQUIRE);
}

static void ztm_adc_unlock(ztm_adc_t *module)
{
    if (module) {
        __atomic_clear(&module->busy, __ATOMIC_RELEASE);
    }
}

static bool ztm_adc_configure_channels(ztm_adc_t *module)
{
    ad7124_dev_t *adc = &module->adc;

    /* No channel is left continuously enabled; measurements select one safely. */
    for (uint8_t channel = 0u; channel < AD7124_CHANNEL_COUNT; channel++) {
        if (!ad7124_config_channel(adc, channel, false, 0u, 0u, 1u)) {
            return false;
        }
    }

    for (uint8_t tc = 0u; tc < ZTM_ADC_TC_COUNT; tc++) {
        if (!ad7124_config_channel(adc,
                                   (uint8_t)(ZTM_ADC_CHANNEL_TC_FIRST + tc),
                                   false,
                                   ZTM_ADC_SETUP_TC,
                                   g_tc_ain_positive[tc],
                                   g_tc_ain_negative[tc])) {
            return false;
        }
    }

    if (!ad7124_config_channel(adc,
                               ZTM_ADC_CHANNEL_CJC,
                               false,
                               ZTM_ADC_SETUP_RTD,
                               ZTM_ADC_AIN_CJC,
                               ZTM_ADC_AIN_RTD_RETURN)) {
        return false;
    }

    for (uint8_t rtd = 0u; rtd < ZTM_ADC_RTD_COUNT; rtd++) {
        if (!ad7124_config_channel(adc,
                                   (uint8_t)(ZTM_ADC_CHANNEL_RTD_FIRST + rtd),
                                   false,
                                   ZTM_ADC_SETUP_RTD,
                                   (uint8_t)(ZTM_ADC_AIN_RTD_FIRST + rtd),
                                   ZTM_ADC_AIN_RTD_RETURN)) {
            return false;
        }
    }

    return true;
}

static void ztm_adc_capture_failure_registers(ztm_adc_t *module)
{
    if (!module) {
        return;
    }
    (void)ad7124_read_error(&module->adc, &module->last_measurement.error_mask);
}

static bool ztm_adc_measure_channel_unlocked(ztm_adc_t *module,
                                              uint8_t channel,
                                              uint32_t *code)
{
    if (!module || !code || channel >= AD7124_CHANNEL_COUNT) {
        return false;
    }

    ad7124_dev_t *adc = &module->adc;
    ztm_adc_measure_diag_t *diag = &module->last_measurement;
    memset(diag, 0, sizeof(*diag));
    diag->stage = ZTM_ADC_MEASURE_NONE;
    diag->expected_channel = channel;
    diag->active_channel = 0xFFu;
    (void)ad7124_read_status(adc, &diag->status_before);

    if (!ad7124_set_channel_enabled(adc, channel, true)) {
        diag->stage = ZTM_ADC_MEASURE_CHANNEL_ENABLE;
        ztm_adc_capture_failure_registers(module);
        return false;
    }
    (void)ad7124_read_reg16(adc, AD7124_REG_CHANNEL(channel),
                            &diag->channel_register);
    uint8_t setup = (uint8_t)((diag->channel_register >> 12) & 0x07u);
    (void)ad7124_read_reg24(adc, AD7124_REG_FILTER(setup),
                            &diag->filter_register);

    if (!ad7124_set_mode(adc, AD7124_MODE_SINGLE)) {
        diag->stage = ZTM_ADC_MEASURE_MODE_START;
        ztm_adc_capture_failure_registers(module);
        (void)ad7124_set_channel_enabled(adc, channel, false);
        return false;
    }

    if (!ad7124_read_reg16(adc, AD7124_REG_ADC_CONTROL,
                            &diag->adc_control_after_start) ||
        ((diag->adc_control_after_start >> 2) & 0x0Fu) != AD7124_MODE_SINGLE) {
        diag->stage = ZTM_ADC_MEASURE_MODE_VERIFY;
        ztm_adc_capture_failure_registers(module);
        (void)ad7124_set_mode(adc, AD7124_MODE_STANDBY);
        (void)ad7124_set_channel_enabled(adc, channel, false);
        return false;
    }

    uint64_t started_us = time_us_64();
    uint8_t status = AD7124_STATUS_RDY;
    bool ready = false;
    do {
        if (!ad7124_read_status(adc, &status)) {
            diag->stage = ZTM_ADC_MEASURE_READY_TIMEOUT;
            ztm_adc_capture_failure_registers(module);
            break;
        }
        diag->ready_polls++;
        if ((status & AD7124_STATUS_RDY) == 0u) {
            ready = true;
            break;
        }
        /* A 1 ms cadence is ample for a low-rate sigma-delta converter and
         * avoids hammering the shared SPI fabric while the filter settles. */
        busy_wait_us_32(1000u);
    } while ((time_us_64() - started_us) < ZTM_ADC_CONVERSION_TIMEOUT_US);

    diag->elapsed_us = (uint32_t)(time_us_64() - started_us);
    diag->status_ready = status;
    (void)ad7124_read_reg16(adc, AD7124_REG_ADC_CONTROL,
                            &diag->adc_control_at_result);

    if (!ready) {
        diag->stage = ZTM_ADC_MEASURE_READY_TIMEOUT;
        ztm_adc_capture_failure_registers(module);
        (void)ad7124_set_mode(adc, AD7124_MODE_STANDBY);
        (void)ad7124_set_channel_enabled(adc, channel, false);
        (void)ad7124_read_status(adc, &diag->status_after_cleanup);
        (void)ad7124_read_reg16(adc, AD7124_REG_ADC_CONTROL,
                                &diag->adc_control_after_cleanup);
        return false;
    }

    uint8_t active_channel = 0xFFu;
    if (!ad7124_read_sample(adc, code, &active_channel)) {
        diag->stage = ZTM_ADC_MEASURE_DATA_READ;
        ztm_adc_capture_failure_registers(module);
        (void)ad7124_set_mode(adc, AD7124_MODE_STANDBY);
        (void)ad7124_set_channel_enabled(adc, channel, false);
        (void)ad7124_read_status(adc, &diag->status_after_cleanup);
        (void)ad7124_read_reg16(adc, AD7124_REG_ADC_CONTROL,
                                &diag->adc_control_after_cleanup);
        return false;
    }

    diag->raw_code = *code;
    diag->active_channel = active_channel;
    bool ok = true;
    if (adc->data_status_enabled && active_channel != channel) {
        diag->stage = ZTM_ADC_MEASURE_CHANNEL_MISMATCH;
        ztm_adc_capture_failure_registers(module);
        ok = false;
    }

    if (!ad7124_set_mode(adc, AD7124_MODE_STANDBY)) {
        if (ok) {
            diag->stage = ZTM_ADC_MEASURE_STANDBY;
            ztm_adc_capture_failure_registers(module);
        }
        ok = false;
    }
    if (!ad7124_set_channel_enabled(adc, channel, false)) {
        if (ok) {
            diag->stage = ZTM_ADC_MEASURE_CHANNEL_DISABLE;
            ztm_adc_capture_failure_registers(module);
        }
        ok = false;
    }
    (void)ad7124_read_status(adc, &diag->status_after_cleanup);
    (void)ad7124_read_reg16(adc, AD7124_REG_ADC_CONTROL,
                            &diag->adc_control_after_cleanup);

    if (ok) {
        diag->stage = ZTM_ADC_MEASURE_OK;
    }
    return ok;
}

static bool ztm_adc_measure_rtd_unlocked(ztm_adc_t *module,
                                         uint8_t channel,
                                         uint8_t ain_positive,
                                         uint8_t logical_index,
                                         ztm_adc_rtd_sample_t *sample)
{
    if (!module || !sample || ain_positive > 15u) {
        return false;
    }

    memset(sample, 0, sizeof(*sample));

    bool current_on = ad7124_set_iout(&module->adc,
                                      module->rtd_excitation,
                                      ain_positive,
                                      AD7124_IOUT_OFF,
                                      0u,
                                      false);
    if (!current_on) {
        memset(&module->last_measurement, 0, sizeof(module->last_measurement));
        module->last_measurement.stage = ZTM_ADC_MEASURE_IOUT_ENABLE;
        module->last_measurement.expected_channel = channel;
        ztm_adc_capture_failure_registers(module);
        return false;
    }
    /* Allow the current source, input RC network, and reference node to settle. */
    busy_wait_us_32(ZTM_ADC_IOUT_SETTLE_US);

    uint32_t code = 0u;
    bool ok = ztm_adc_measure_channel_unlocked(module, channel, &code);
    (void)ad7124_read_reg24(&module->adc, AD7124_REG_IO_CONTROL_1,
                            &module->last_measurement.io_control_1);

    /* Excitation must never remain enabled after a one-shot read, even on error. */
    if (!ad7124_disable_iouts(&module->adc)) {
        if (ok) {
            module->last_measurement.stage = ZTM_ADC_MEASURE_IOUT_DISABLE;
            ztm_adc_capture_failure_registers(module);
        }
        ok = false;
    }

    if (!ok) {
        return false;
    }

    sample->valid = true;
    sample->logical_index = logical_index;
    sample->ad7124_channel = channel;
    sample->ain_positive = ain_positive;
    sample->raw_code = code;

    if (code == 0u || code == AD7124_DATA_FULL_SCALE) {
        sample->status = ZTM_ADC_SENSOR_DISCONNECTED;
        return true;
    }

    double resistance = ztm_adc_rtd_resistance_from_code(
        code,
        module->reference_resistor_ohms);
    module->last_measurement.resistance_ohms = resistance;
    sample->resistance_ohms = resistance;
    if (!ztm_adc_pt1000_temperature(resistance, &sample->temperature_c)) {
        module->last_measurement.stage = ZTM_ADC_MEASURE_SENSOR_RANGE;
        sample->status = ZTM_ADC_SENSOR_OUT_OF_RANGE;
        return true;
    }

    sample->status = ZTM_ADC_SENSOR_OK;
    return true;
}

static bool ztm_adc_measure_cjc_unlocked(ztm_adc_t *module,
                                         ztm_adc_rtd_sample_t *sample)
{
    return ztm_adc_measure_rtd_unlocked(module,
                                        ZTM_ADC_CHANNEL_CJC,
                                        ZTM_ADC_AIN_CJC,
                                        0u,
                                        sample);
}

static bool ztm_adc_measure_external_rtd_unlocked(ztm_adc_t *module,
                                                   uint8_t rtd_1based,
                                                   ztm_adc_rtd_sample_t *sample)
{
    if (rtd_1based < 1u || rtd_1based > ZTM_ADC_RTD_COUNT) {
        return false;
    }

    uint8_t index = (uint8_t)(rtd_1based - 1u);
    return ztm_adc_measure_rtd_unlocked(module,
                                        (uint8_t)(ZTM_ADC_CHANNEL_RTD_FIRST + index),
                                        (uint8_t)(ZTM_ADC_AIN_RTD_FIRST + index),
                                        rtd_1based,
                                        sample);
}

static bool ztm_adc_measure_tc_unlocked(ztm_adc_t *module,
                                        uint8_t tc_1based,
                                        bool cold_junction_valid,
                                        double cold_junction_c,
                                        ztm_adc_tc_sample_t *sample)
{
    if (!module || !sample || tc_1based < 1u || tc_1based > ZTM_ADC_TC_COUNT) {
        return false;
    }

    uint8_t index = (uint8_t)(tc_1based - 1u);
    uint8_t channel = (uint8_t)(ZTM_ADC_CHANNEL_TC_FIRST + index);
    uint32_t code = 0u;
    memset(sample, 0, sizeof(*sample));

    /* Ensure no RTD excitation contaminates a thermocouple measurement. */
    if (!ad7124_disable_iouts(&module->adc) ||
        !ztm_adc_measure_channel_unlocked(module, channel, &code)) {
        return false;
    }

    sample->valid = true;
    sample->logical_index = tc_1based;
    sample->ad7124_channel = channel;
    sample->ain_positive = g_tc_ain_positive[index];
    sample->ain_negative = g_tc_ain_negative[index];
    sample->raw_code = code;
    sample->type = module->thermocouple_types[index];

    if (code == 0u || code == AD7124_DATA_FULL_SCALE) {
        sample->status = ZTM_ADC_SENSOR_DISCONNECTED;
        return true;
    }

    double volts = ad7124_code_to_bipolar_voltage(code,
                                                   ZTM_ADC_INTERNAL_REF_VOLTS,
                                                   ZTM_ADC_TC_PGA);
    sample->differential_mv = volts * 1000.0;
    sample->cold_junction_valid = cold_junction_valid;
    sample->cold_junction_c = cold_junction_c;
    if (!cold_junction_valid) {
        sample->status = ZTM_ADC_SENSOR_CJC_UNAVAILABLE;
        return true;
    }

    if (!thermocouple_compensated_temperature_c(sample->type,
                                                  sample->differential_mv,
                                                  cold_junction_c,
                                                  &sample->temperature_c,
                                                  &sample->compensated_mv)) {
        sample->status = ZTM_ADC_SENSOR_OUT_OF_RANGE;
        return true;
    }

    sample->status = ZTM_ADC_SENSOR_OK;
    return true;
}

static void ztm_adc_log_error_bit(uint32_t mask, const char *message)
{
    if (mask != 0u) {
        log_printf("ERROR: AD7124 %s\n", message);
    }
}

static void ztm_adc_log_new_errors(uint32_t new_errors)
{
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_LDO_CAP, "LDO Capacitor Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_ADC_CAL, "Calibration Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_ADC_CONV, "Conversion Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_ADC_SAT, "Input Saturation");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_AINP_OV, "AIN+ Overvoltage");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_AINP_UV, "AIN+ Undervoltage");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_AINM_OV, "AIN- Overvoltage");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_AINM_UV, "AIN- Undervoltage");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_REF_DET, "Reference Missing/Low");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_DLDO_PSM, "Digital LDO Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_ALDO_PSM, "Analog LDO Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_SPI_IGNORE, "SPI Command Ignored");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_SPI_SCLK_CNT, "SPI Clock Count Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_SPI_READ, "Invalid SPI Read");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_SPI_WRITE, "Invalid SPI Write");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_SPI_CRC, "SPI CRC Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_MM_CRC, "Register Memory CRC Fault");
    ztm_adc_log_error_bit(new_errors & AD7124_ERROR_ROM_CRC, "ROM CRC Fault");
}

static uint16_t ztm_adc_expected_channel_value(uint8_t channel)
{
    uint8_t setup = 0u;
    uint8_t ain_positive = 0u;
    uint8_t ain_negative = 1u;

    if (channel < ZTM_ADC_TC_COUNT) {
        setup = ZTM_ADC_SETUP_TC;
        ain_positive = g_tc_ain_positive[channel];
        ain_negative = g_tc_ain_negative[channel];
    } else if (channel == ZTM_ADC_CHANNEL_CJC) {
        setup = ZTM_ADC_SETUP_RTD;
        ain_positive = ZTM_ADC_AIN_CJC;
        ain_negative = ZTM_ADC_AIN_RTD_RETURN;
    } else if (channel >= ZTM_ADC_CHANNEL_RTD_FIRST &&
               channel < ZTM_ADC_CHANNEL_RTD_FIRST + ZTM_ADC_RTD_COUNT) {
        setup = ZTM_ADC_SETUP_RTD;
        ain_positive = (uint8_t)(ZTM_ADC_AIN_RTD_FIRST +
                                 channel - ZTM_ADC_CHANNEL_RTD_FIRST);
        ain_negative = ZTM_ADC_AIN_RTD_RETURN;
    }

    return (uint16_t)(((uint16_t)setup << 12) |
                      ((uint16_t)ain_positive << 5) |
                      (uint16_t)ain_negative);
}

static bool ztm_adc_verify_reg16(ad7124_dev_t *adc,
                                 ad7124_reg_t reg,
                                 uint16_t expected,
                                 const char *name,
                                 bool log_mismatch)
{
    uint16_t actual = 0u;
    if (!ad7124_read_reg16(adc, reg, &actual)) {
        if (log_mismatch) {
            log_printf("ERROR: AD7124 failed to read %s during configuration verification\n",
                       name);
        }
        return false;
    }
    if (actual != expected) {
        if (log_mismatch) {
            log_printf("ERROR: AD7124 %s mismatch: expected 0x%04X, read 0x%04X\n",
                       name, expected, actual);
        }
        return false;
    }
    return true;
}

static bool ztm_adc_verify_reg24(ad7124_dev_t *adc,
                                 ad7124_reg_t reg,
                                 uint32_t expected,
                                 const char *name,
                                 bool log_mismatch)
{
    uint32_t actual = 0u;
    if (!ad7124_read_reg24(adc, reg, &actual)) {
        if (log_mismatch) {
            log_printf("ERROR: AD7124 failed to read %s during configuration verification\n",
                       name);
        }
        return false;
    }
    actual &= 0xFFFFFFu;
    expected &= 0xFFFFFFu;
    if (actual != expected) {
        if (log_mismatch) {
            log_printf("ERROR: AD7124 %s mismatch: expected 0x%06lX, read 0x%06lX\n",
                       name, (unsigned long)expected, (unsigned long)actual);
        }
        return false;
    }
    return true;
}

static bool ztm_adc_verify_configuration_unlocked(ztm_adc_t *module,
                                                   bool log_mismatch)
{
    if (!module) {
        return false;
    }

    const uint16_t expected_adc_control =
        (uint16_t)((1u << 10) | (1u << 9) | (1u << 8) |
                   ((uint16_t)AD7124_POWER_MID << 6) |
                   ((uint16_t)AD7124_MODE_STANDBY << 2));
    const uint16_t expected_tc_config =
        (uint16_t)((1u << 11) | (1u << 6) | (1u << 5) |
                   ((uint16_t)AD7124_REF_INTERNAL << 3) |
                   (uint16_t)ZTM_ADC_TC_PGA);
    const uint16_t expected_rtd_config =
        (uint16_t)((1u << 6) | (1u << 5) |
                   ((uint16_t)AD7124_REF_REFIN1 << 3) |
                   (uint16_t)AD7124_PGA_1);
    const uint32_t expected_filter = (uint32_t)ZTM_ADC_FILTER_FS;

    bool ok = true;
    ok = ztm_adc_verify_reg16(&module->adc, AD7124_REG_ADC_CONTROL,
                              expected_adc_control, "ADC_CONTROL", log_mismatch) && ok;
    ok = ztm_adc_verify_reg24(&module->adc, AD7124_REG_ERROR_EN,
                              ZTM_ADC_ERROR_ENABLE_MASK, "ERROR_EN", log_mismatch) && ok;
    ok = ztm_adc_verify_reg16(&module->adc, AD7124_REG_CONFIG(ZTM_ADC_SETUP_TC),
                              expected_tc_config, "CONFIG_TC", log_mismatch) && ok;
    ok = ztm_adc_verify_reg16(&module->adc, AD7124_REG_CONFIG(ZTM_ADC_SETUP_RTD),
                              expected_rtd_config, "CONFIG_RTD", log_mismatch) && ok;
    ok = ztm_adc_verify_reg24(&module->adc, AD7124_REG_FILTER(ZTM_ADC_SETUP_TC),
                              expected_filter, "FILTER_TC", log_mismatch) && ok;
    ok = ztm_adc_verify_reg24(&module->adc, AD7124_REG_FILTER(ZTM_ADC_SETUP_RTD),
                              expected_filter, "FILTER_RTD", log_mismatch) && ok;
    ok = ztm_adc_verify_reg24(&module->adc, AD7124_REG_IO_CONTROL_1,
                              0u, "IO_CONTROL_1", log_mismatch) && ok;
    ok = ztm_adc_verify_reg16(&module->adc, AD7124_REG_IO_CONTROL_2,
                              ZTM_ADC_TC_VBIAS_MASK, "IO_CONTROL_2", log_mismatch) && ok;

    for (uint8_t channel = 0u; channel < AD7124_CHANNEL_COUNT; channel++) {
        char name[16];
        (void)snprintf(name, sizeof(name), "CHANNEL_%u", (unsigned)channel);
        ok = ztm_adc_verify_reg16(&module->adc,
                                  AD7124_REG_CHANNEL(channel),
                                  ztm_adc_expected_channel_value(channel),
                                  name,
                                  log_mismatch) && ok;
    }

    module->configuration_verified = ok;
    if (!ok) {
        module->configuration_error_count++;
    }
    return ok;
}

int ztm_adc_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ctx || !spi || cs_pin < 0) {
        return -1;
    }

    ztm_adc_t *module = (ztm_adc_t *)ctx;
    memset(module, 0, sizeof(*module));
    module->reference_resistor_ohms = ZTM_ADC_RREF_OHMS;
    module->rtd_excitation = ZTM_ADC_RTD_EXCITATION;
    for (uint8_t tc = 0u; tc < ZTM_ADC_TC_COUNT; tc++) {
        module->thermocouple_types[tc] = THERMOCOUPLE_TYPE_K;
    }

    static const bus_spi_config_t spi_config = {
        .baud_hz = 1000000u,
        .data_bits = 8u,
        .cpol = SPI_CPOL_1,
        .cpha = SPI_CPHA_1,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0u,
        .cs_hold_us = 0u,
        .cs_high_us = 1u,
    };

    if (!bus_spi_init(&module->adc.spi_bus,
                      (spi_inst_t *)spi,
                      (uint)cs_pin,
                      en_pin,
                      &spi_config)) {
        return -2;
    }

    ad7124_init(&module->adc);
    if (!ad7124_reset(&module->adc)) {
        return -3;
    }

    if (!ad7124_read_id(&module->adc, &module->device_id) ||
        !ad7124_id_is_ad7124_8(module->device_id)) {
        return -4;
    }

    if (!ad7124_config_adc_control(&module->adc,
                                   AD7124_POWER_MID,
                                   AD7124_MODE_STANDBY,
                                   true,
                                   true,
                                   true)) {
        return -5;
    }

    if (!ad7124_config_error_enable(&module->adc,
                                    ZTM_ADC_ERROR_ENABLE_MASK)) {
        return -6;
    }

    /* Setup 0: floating thermocouples, internal 2.5 V reference, ±78 mV range. */
    if (!ad7124_config_setup(&module->adc,
                             ZTM_ADC_SETUP_TC,
                             AD7124_REF_INTERNAL,
                             true,
                             ZTM_ADC_TC_PGA,
                             true, true,
                             false, false,
                             0u) ||
        !ad7124_config_filter(&module->adc,
                              ZTM_ADC_SETUP_TC,
                              AD7124_FILTER_SINC4,
                              0u,
                              false,
                              false,
                              ZTM_ADC_FILTER_FS)) {
        return -7;
    }

    /* Setup 1: Pt1000s, unipolar and ratiometric against REFIN1. */
    if (!ad7124_config_setup(&module->adc,
                             ZTM_ADC_SETUP_RTD,
                             AD7124_REF_REFIN1,
                             false,
                             AD7124_PGA_1,
                             true, true,
                             false, false,
                             0u) ||
        !ad7124_config_filter(&module->adc,
                              ZTM_ADC_SETUP_RTD,
                              AD7124_FILTER_SINC4,
                              0u,
                              false,
                              false,
                              ZTM_ADC_FILTER_FS)) {
        return -8;
    }

    if (!ztm_adc_configure_channels(module) ||
        !ad7124_set_bias_mask(&module->adc, ZTM_ADC_TC_VBIAS_MASK) ||
        !ad7124_disable_iouts(&module->adc)) {
        return -9;
    }

    /* Clear the expected reset/POR indication and any stale reset diagnostics. */
    uint8_t status = 0u;
    uint32_t errors = 0u;
    if (!ad7124_read_status(&module->adc, &status) ||
        !ad7124_read_error(&module->adc, &errors)) {
        return -10;
    }

    if (!ztm_adc_verify_configuration_unlocked(module, true)) {
        return -11;
    }

    module->ready = true;
    log_printf("AD7124-8 ready: ID=0x%02X, 3 TC + CJC Pt1000 + 8 RTD\n",
               module->device_id);
    return 0;
}

int ztm_adc_poll(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready) {
        return -1;
    }

    /* A console measurement may own a multi-register sequence. Skip, don't block. */
    if (!ztm_adc_try_lock(module)) {
        return 0;
    }

    uint8_t status = 0u;
    if (!ad7124_read_status(&module->adc, &status)) {
        module->communication_error_count++;
        ztm_adc_unlock(module);
        return -2;
    }

    bool por_fault = (status & AD7124_STATUS_POR_FLAG) != 0u;
    if (por_fault && !module->por_fault_active) {
        module->error_event_count++;
        module->configuration_verified = false;
        log_printf("ERROR: AD7124 Unexpected Reset!\n");
    }
    module->por_fault_active = por_fault;

    if ((status & AD7124_STATUS_ERROR_FLAG) != 0u) {
        uint32_t errors = 0u;
        if (!ad7124_read_error(&module->adc, &errors)) {
            module->communication_error_count++;
            ztm_adc_unlock(module);
            return -3;
        }

        uint32_t new_errors = errors & ~module->last_error_mask;
        if (new_errors != 0u) {
            module->error_event_count++;
            ztm_adc_log_new_errors(new_errors);
        }
        module->last_error_mask = errors;
    } else {
        module->last_error_mask = 0u;
    }

    ztm_adc_unlock(module);
    return 0;
}

int ztm_adc_deinit(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (module->ready && ztm_adc_try_lock(module)) {
        (void)ad7124_disable_iouts(&module->adc);
        (void)ad7124_set_mode(&module->adc, AD7124_MODE_POWERDOWN);
        ztm_adc_unlock(module);
    }
    module->ready = false;
    return 0;
}

bool ztm_adc_ready(void)
{
    return ztm_adc_1.ready;
}

double ztm_adc_rtd_resistance_from_code(uint32_t code,
                                         double reference_resistor_ohms)
{
    if (reference_resistor_ohms <= 0.0) {
        return NAN;
    }
    return reference_resistor_ohms *
           ((double)(code & AD7124_DATA_FULL_SCALE) /
            (double)AD7124_DATA_CODE_COUNT);
}

bool ztm_adc_pt1000_temperature(double resistance_ohms, double *temperature_c)
{
    if (!temperature_c || !isfinite(resistance_ohms) ||
        resistance_ohms < 150.0 || resistance_ohms > 5000.0) {
        return false;
    }

    const double r0 = ZTM_ADC_RTD_R0_OHMS;
    const double a = 3.9083e-3;
    const double b = -5.775e-7;
    const double c = -4.183e-12;
    const double ratio = resistance_ohms / r0;
    double temperature;

    if (resistance_ohms >= r0) {
        double discriminant = a * a - 4.0 * b * (1.0 - ratio);
        if (discriminant < 0.0) {
            return false;
        }
        temperature = (-a + sqrt(discriminant)) / (2.0 * b);
    } else {
        temperature = (ratio - 1.0) / a;
        for (unsigned iteration = 0u; iteration < 12u; iteration++) {
            double t2 = temperature * temperature;
            double t3 = t2 * temperature;
            double calculated = 1.0 + a * temperature + b * t2 +
                                c * (temperature - 100.0) * t3;
            double derivative = a + 2.0 * b * temperature +
                                c * (4.0 * t3 - 300.0 * t2);
            if (fabs(derivative) < 1e-12) {
                return false;
            }
            double correction = (calculated - ratio) / derivative;
            temperature -= correction;
            if (fabs(correction) < 1e-9) {
                break;
            }
        }
    }

    if (!isfinite(temperature) || temperature < -250.0 || temperature > 850.0) {
        return false;
    }
    *temperature_c = temperature;
    return true;
}

bool ztm_adc_read_cold_junction(ztm_adc_rtd_sample_t *sample)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !sample || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_measure_cjc_unlocked(module, sample);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_rtd(uint8_t rtd_1based, ztm_adc_rtd_sample_t *sample)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !sample || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_measure_external_rtd_unlocked(module, rtd_1based, sample);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_all_rtds(ztm_adc_rtd_sample_t *cold_junction,
                            ztm_adc_rtd_sample_t samples[ZTM_ADC_RTD_COUNT])
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !cold_junction || !samples || !ztm_adc_try_lock(module)) {
        return false;
    }

    bool ok = ztm_adc_measure_cjc_unlocked(module, cold_junction);
    for (uint8_t rtd = 1u; ok && rtd <= ZTM_ADC_RTD_COUNT; rtd++) {
        ok = ztm_adc_measure_external_rtd_unlocked(module,
                                                   rtd,
                                                   &samples[rtd - 1u]);
    }
    (void)ad7124_disable_iouts(&module->adc);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_set_thermocouple_type(uint8_t tc_1based, thermocouple_type_t type)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || tc_1based < 1u || tc_1based > ZTM_ADC_TC_COUNT ||
        type < THERMOCOUPLE_TYPE_B || type >= THERMOCOUPLE_TYPE_COUNT ||
        !ztm_adc_try_lock(module)) {
        return false;
    }

    module->thermocouple_types[tc_1based - 1u] = type;
    ztm_adc_unlock(module);
    return true;
}

bool ztm_adc_get_thermocouple_type(uint8_t tc_1based, thermocouple_type_t *type)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !type || tc_1based < 1u ||
        tc_1based > ZTM_ADC_TC_COUNT || !ztm_adc_try_lock(module)) {
        return false;
    }

    *type = module->thermocouple_types[tc_1based - 1u];
    ztm_adc_unlock(module);
    return true;
}

bool ztm_adc_read_thermocouple(uint8_t tc_1based, ztm_adc_tc_sample_t *sample)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !sample || !ztm_adc_try_lock(module)) {
        return false;
    }

    ztm_adc_rtd_sample_t cjc;
    bool cjc_read_ok = ztm_adc_measure_cjc_unlocked(module, &cjc);
    bool cjc_valid = cjc_read_ok && cjc.status == ZTM_ADC_SENSOR_OK;
    bool ok = ztm_adc_measure_tc_unlocked(module,
                                           tc_1based,
                                           cjc_valid,
                                           cjc_valid ? cjc.temperature_c : NAN,
                                           sample);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_all_thermocouples(ztm_adc_tc_sample_t samples[ZTM_ADC_TC_COUNT])
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !samples || !ztm_adc_try_lock(module)) {
        return false;
    }

    ztm_adc_rtd_sample_t cjc;
    bool cjc_read_ok = ztm_adc_measure_cjc_unlocked(module, &cjc);
    bool cjc_valid = cjc_read_ok && cjc.status == ZTM_ADC_SENSOR_OK;
    bool ok = true;
    for (uint8_t tc = 1u; ok && tc <= ZTM_ADC_TC_COUNT; tc++) {
        ok = ztm_adc_measure_tc_unlocked(module,
                                         tc,
                                         cjc_valid,
                                         cjc_valid ? cjc.temperature_c : NAN,
                                         &samples[tc - 1u]);
    }
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_all(ztm_adc_rtd_sample_t *cold_junction,
                      ztm_adc_rtd_sample_t rtds[ZTM_ADC_RTD_COUNT],
                      ztm_adc_tc_sample_t thermocouples[ZTM_ADC_TC_COUNT])
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !cold_junction || !rtds || !thermocouples ||
        !ztm_adc_try_lock(module)) {
        return false;
    }

    bool ok = ztm_adc_measure_cjc_unlocked(module, cold_junction);
    for (uint8_t rtd = 1u; ok && rtd <= ZTM_ADC_RTD_COUNT; rtd++) {
        ok = ztm_adc_measure_external_rtd_unlocked(module,
                                                   rtd,
                                                   &rtds[rtd - 1u]);
    }
    bool cjc_valid = cold_junction->status == ZTM_ADC_SENSOR_OK;
    for (uint8_t tc = 1u; ok && tc <= ZTM_ADC_TC_COUNT; tc++) {
        ok = ztm_adc_measure_tc_unlocked(module,
                                         tc,
                                         cjc_valid,
                                         cjc_valid ? cold_junction->temperature_c : NAN,
                                         &thermocouples[tc - 1u]);
    }
    (void)ad7124_disable_iouts(&module->adc);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_error_status(uint8_t *status, uint32_t *error_mask)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !status || !error_mask || !ztm_adc_try_lock(module)) {
        return false;
    }

    bool ok = ad7124_read_status(&module->adc, status) &&
              ad7124_read_error(&module->adc, error_mask);
    if (ok) {
        module->last_error_mask = *error_mask;
    } else {
        module->communication_error_count++;
    }
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_verify_configuration(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_verify_configuration_unlocked(module, true);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_read_register(uint8_t address, uint8_t width_bytes, uint32_t *value)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !value || address > 0x38u ||
        width_bytes < 1u || width_bytes > 3u || !ztm_adc_try_lock(module)) {
        return false;
    }

    bool ok = false;
    if (width_bytes == 1u) {
        uint8_t v = 0u;
        ok = ad7124_read_reg8(&module->adc, (ad7124_reg_t)address, &v);
        *value = v;
    } else if (width_bytes == 2u) {
        uint16_t v = 0u;
        ok = ad7124_read_reg16(&module->adc, (ad7124_reg_t)address, &v);
        *value = v;
    } else {
        ok = ad7124_read_reg24(&module->adc, (ad7124_reg_t)address, value);
    }

    ztm_adc_unlock(module);
    return ok;
}

static bool ztm_adc_calibrate_tc_unlocked(ztm_adc_t *module)
{
    bool ok = ad7124_disable_iouts(&module->adc) &&
              ad7124_calibrate_channel(&module->adc,
                                        ZTM_ADC_CHANNEL_TC_FIRST,
                                        true,
                                        true,
                                        ZTM_ADC_CONVERSION_TIMEOUT_US);
    (void)ad7124_disable_all_channels(&module->adc);
    return ok;
}

static bool ztm_adc_calibrate_rtd_unlocked(ztm_adc_t *module)
{
    bool ok = ad7124_set_iout(&module->adc,
                              module->rtd_excitation,
                              ZTM_ADC_AIN_CJC,
                              AD7124_IOUT_OFF,
                              0u,
                              false);
    if (ok) {
        busy_wait_us_32(ZTM_ADC_IOUT_SETTLE_US);
        ok = ad7124_calibrate_channel(&module->adc,
                                      ZTM_ADC_CHANNEL_CJC,
                                      true,
                                      true,
                                      ZTM_ADC_CONVERSION_TIMEOUT_US);
    }
    if (!ad7124_disable_iouts(&module->adc)) {
        ok = false;
    }
    (void)ad7124_disable_all_channels(&module->adc);
    return ok;
}

bool ztm_adc_calibrate_tc_setup(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_calibrate_tc_unlocked(module);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_calibrate_rtd_setup(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_calibrate_rtd_unlocked(module);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_calibrate_all(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ztm_adc_calibrate_tc_unlocked(module) &&
              ztm_adc_calibrate_rtd_unlocked(module);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_iout_off(void)
{
    ztm_adc_t *module = &ztm_adc_1;
    if (!module->ready || !ztm_adc_try_lock(module)) {
        return false;
    }
    bool ok = ad7124_disable_iouts(&module->adc);
    ztm_adc_unlock(module);
    return ok;
}

bool ztm_adc_get_last_measurement(ztm_adc_measure_diag_t *diag)
{
    if (!diag) {
        return false;
    }
    *diag = ztm_adc_1.last_measurement;
    return true;
}

bool ztm_adc_read_test(ztm_adc_t *module)
{
    if (!module || module != &ztm_adc_1) {
        return false;
    }

    ztm_adc_rtd_sample_t sample;
    if (!ztm_adc_read_cold_junction(&sample)) {
        return false;
    }

    if (sample.status == ZTM_ADC_SENSOR_DISCONNECTED) {
        log_printf("CJC Pt1000: raw=0x%06lX DISCONNECTED\n",
                   (unsigned long)sample.raw_code);
    } else if (sample.status == ZTM_ADC_SENSOR_OUT_OF_RANGE) {
        log_printf("CJC Pt1000: raw=0x%06lX R=%.3f ohm OUT OF RANGE\n",
                   (unsigned long)sample.raw_code,
                   sample.resistance_ohms);
    } else {
        log_printf("CJC Pt1000: raw=0x%06lX R=%.3f ohm T=%.3f C\n",
                   (unsigned long)sample.raw_code,
                   sample.resistance_ohms,
                   sample.temperature_c);
    }
    return true;
}
