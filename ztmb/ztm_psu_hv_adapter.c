#include "ztmb/ztm_psu_hv_adapter.h"

#include "hardware/i2c.h"
#include "ztmb/adc.h"
#include "ztmb/bus.h"
#include "ztmb/io_exp.h"
#include "ztmb/log.h"
#include "ztmb/zap_adapter.h"

ztm_psu_hv_t ztm_psu_hv_1;

static ztmb_zap_i2c_t g_hv_i2c;
static ztmb_zap_lock_t g_hv_lock;
static int8_t g_enable_pin = EX_HV_EN_1;
static int8_t g_pgood_pin = EX_HV_PGOOD_1;

static int hub_prepare(void *context)
{
    (void)context;
    if (g_enable_pin < 0 || g_pgood_pin < 0) {
        return -1;
    }

    /* Program the output latch low before selecting output mode. */
    return pcal6416a_write_pin(&ioexp, (uint8_t)g_enable_pin, false) &&
                   pcal6416a_set_pin_direction(&ioexp, (uint8_t)g_enable_pin, false) &&
                   pcal6416a_set_pin_direction(&ioexp, (uint8_t)g_pgood_pin, true)
               ? 0
               : -1;
}

static int hub_enable_set(void *context, bool enabled)
{
    (void)context;
    return g_enable_pin >= 0 &&
                   pcal6416a_write_pin(&ioexp, (uint8_t)g_enable_pin, enabled)
               ? 0
               : -1;
}

static int hub_enable_get(void *context, bool *enabled)
{
    (void)context;
    return enabled != NULL && g_enable_pin >= 0 &&
                   pcal6416a_read_pin(&ioexp, (uint8_t)g_enable_pin, enabled)
               ? 0
               : -1;
}

static int hub_pgood_read(void *context, bool *pgood)
{
    (void)context;
    return pgood != NULL && g_pgood_pin >= 0 &&
                   pcal6416a_read_pin(&ioexp, (uint8_t)g_pgood_pin, pgood)
               ? 0
               : -1;
}

static int hub_vsense_read(void *context, uint16_t *adc_code, uint32_t *drive_uV)
{
    (void)context;
    return drive_uV != NULL &&
                   ztmb_adc_read_channel(ZTM_PSU_HV_VSENSE_CHANNEL,
                                         ZTM_PSU_HV_DIVIDER_RATIO_THOUSANDS,
                                         adc_code,
                                         drive_uV)
               ? 0
               : -1;
}

static void hub_event(void *context, const ztm_psu_hv_event_t *event)
{
    (void)context;
    if (event == NULL) {
        return;
    }

    switch (event->type) {
        case ZTM_PSU_HV_EVENT_READY:
            log_printf("HV PSU ready and OFF: AD5282=0x%02lX, RDACs=255/255, E60 drive VSENSE=AIN%u\n",
                       (unsigned long)event->detail,
                       (unsigned)ZTM_PSU_HV_VSENSE_CHANNEL);
            break;
        case ZTM_PSU_HV_EVENT_PGOOD_FAULT:
            log_printf("ERROR: LM5164 Power Good lost; E60 drive disabled\n");
            break;
        case ZTM_PSU_HV_EVENT_VSENSE_FAULT:
            log_printf("ERROR: E60 drive VSENSE read failed; output disabled\n");
            break;
        case ZTM_PSU_HV_EVENT_E60_OVERVOLTAGE:
            log_printf("ERROR: E60 input exceeds 15 V (%lu uV); output disabled\n",
                       (unsigned long)event->detail);
            break;
        default:
            break;
    }
}

int ztmb_ztm_psu_hv_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    if (ctx != &ztm_psu_hv_1 || i2c == NULL || en_pin < 0) {
        return -1;
    }

    g_enable_pin = en_pin;
    g_pgood_pin = EX_HV_PGOOD_1;

    if (!ztmb_zap_i2c_init(&g_hv_i2c,
                           (i2c_inst_t *)i2c,
                           400000u,
                           5000u)) {
        return -2;
    }
    ztmb_zap_lock_init(&g_hv_lock, 1000000u);

    const ztm_psu_hv_config_t config = {
        .i2c = &g_hv_i2c.bus,
        .time = ztmb_zap_time(),
        .lock = ztmb_zap_lock(&g_hv_lock),
        .address_straps = (uint8_t)addr & 0x03u,
        .control_context = NULL,
        .prepare = hub_prepare,
        .enable_set = hub_enable_set,
        .enable_get = hub_enable_get,
        .pgood_read = hub_pgood_read,
        .vsense_read = hub_vsense_read,
        .event_context = NULL,
        .event = hub_event,
    };
    return ztm_psu_hv_init(&ztm_psu_hv_1, &config);
}

int ztmb_ztm_psu_hv_poll(void)
{
    return ztm_psu_hv_poll(&ztm_psu_hv_1);
}

int ztmb_ztm_psu_hv_deinit(void)
{
    return ztm_psu_hv_deinit(&ztm_psu_hv_1);
}
