#include "ztmb/ztm_adc_adapter.h"

#include "hardware/spi.h"
#include "ztmb/log.h"
#include "ztmb/zap_adapter.h"

ztm_adc_t ztm_adc_1;

static ztmb_zap_spi_t g_adc_spi;
static ztmb_zap_lock_t g_adc_lock;

static void log_error_bit(uint32_t mask, const char *message)
{
    if (mask != 0u) {
        log_printf("ERROR: AD7124 %s\n", message);
    }
}

static void log_error_mask(uint32_t errors)
{
    log_error_bit(errors & AD7124_ERROR_LDO_CAP, "LDO Capacitor Fault");
    log_error_bit(errors & AD7124_ERROR_ADC_CAL, "Calibration Fault");
    log_error_bit(errors & AD7124_ERROR_ADC_CONV, "Conversion Fault");
    log_error_bit(errors & AD7124_ERROR_ADC_SAT, "Input Saturation");
    log_error_bit(errors & AD7124_ERROR_AINP_OV, "AIN+ Overvoltage");
    log_error_bit(errors & AD7124_ERROR_AINP_UV, "AIN+ Undervoltage");
    log_error_bit(errors & AD7124_ERROR_AINM_OV, "AIN- Overvoltage");
    log_error_bit(errors & AD7124_ERROR_AINM_UV, "AIN- Undervoltage");
    log_error_bit(errors & AD7124_ERROR_REF_DET, "Reference Missing/Low");
    log_error_bit(errors & AD7124_ERROR_DLDO_PSM, "Digital LDO Fault");
    log_error_bit(errors & AD7124_ERROR_ALDO_PSM, "Analog LDO Fault");
    log_error_bit(errors & AD7124_ERROR_SPI_IGNORE, "SPI Command Ignored");
    log_error_bit(errors & AD7124_ERROR_SPI_SCLK_CNT, "SPI Clock Count Fault");
    log_error_bit(errors & AD7124_ERROR_SPI_READ, "Invalid SPI Read");
    log_error_bit(errors & AD7124_ERROR_SPI_WRITE, "Invalid SPI Write");
    log_error_bit(errors & AD7124_ERROR_SPI_CRC, "SPI CRC Fault");
    log_error_bit(errors & AD7124_ERROR_MM_CRC, "Register Memory CRC Fault");
    log_error_bit(errors & AD7124_ERROR_ROM_CRC, "ROM CRC Fault");
}

static void adc_event(void *context, const ztm_adc_event_t *event)
{
    (void)context;
    if (!event) {
        return;
    }

    switch (event->type) {
        case ZTM_ADC_EVENT_READY:
            log_printf("AD7124-8 ready: 3 TC + CJC Pt1000 + 8 RTD\n");
            break;
        case ZTM_ADC_EVENT_UNEXPECTED_RESET:
            log_printf("ERROR: AD7124 Unexpected Reset! status=0x%02X\n",
                       event->status);
            break;
        case ZTM_ADC_EVENT_ADC_ERROR:
            log_error_mask(event->error_mask);
            break;
        case ZTM_ADC_EVENT_COMMUNICATION_ERROR:
            log_printf("ERROR: AD7124 communication failure\n");
            break;
        case ZTM_ADC_EVENT_CONFIGURATION_MISMATCH:
            if (event->actual == UINT32_MAX) {
                log_printf("ERROR: AD7124 failed to read reg 0x%02X during configuration verification\n",
                           event->register_address);
            } else {
                log_printf("ERROR: AD7124 reg 0x%02X mismatch: expected 0x%06lX, read 0x%06lX\n",
                           event->register_address,
                           (unsigned long)event->expected,
                           (unsigned long)event->actual);
            }
            break;
        default:
            break;
    }
}

int ztmb_ztm_adc_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ctx || ctx != &ztm_adc_1 || !spi || cs_pin < 0) {
        return -1;
    }

    const bus_spi_config_t spi_config = {
        .baud_hz = 1000000u,
        .data_bits = 8u,
        .cpol = SPI_CPOL_1,
        .cpha = SPI_CPHA_1,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0u,
        .cs_hold_us = 0u,
        .cs_high_us = 1u,
    };

    if (!ztmb_zap_spi_init(&g_adc_spi,
                           (spi_inst_t *)spi,
                           (uint)cs_pin,
                           en_pin,
                           &spi_config,
                           ZAP_SPI_DEFAULT_TIMEOUT_US,
                           0u)) {
        return -2;
    }

    ztmb_zap_lock_init(&g_adc_lock, ZTM_ADC_OPERATION_TIMEOUT_US);
    const ztm_adc_config_t config = {
        .spi = ztmb_zap_spi_device(&g_adc_spi),
        .time = ztmb_zap_time(),
        .lock = ztmb_zap_lock(&g_adc_lock),
        .event_context = NULL,
        .event = adc_event,
    };
    return ztm_adc_init(&ztm_adc_1, &config);
}

int ztmb_ztm_adc_poll(void)
{
    return ztm_adc_poll(&ztm_adc_1);
}

int ztmb_ztm_adc_deinit(void)
{
    return ztm_adc_deinit(&ztm_adc_1);
}

bool ztmb_ztm_adc_read_test(ztm_adc_t *module)
{
    if (!module || module != &ztm_adc_1) {
        return false;
    }

    ztm_adc_rtd_sample_t sample;
    if (!ztm_adc_read_cold_junction(module, &sample)) {
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

const bus_spi_t *ztmb_ztm_adc_bus(void)
{
    return &g_adc_spi.bus;
}
