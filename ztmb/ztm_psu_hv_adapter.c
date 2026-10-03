#include "zaptronics/boards/ztpico2/connector.h"
#include "ztmb/ztm_psu_hv_adapter.h"

#include "hardware/i2c.h"
#include "ztmb/adc.h"
#include "zaptronics/boards/ztpico2/bus.h"
#include "zaptronics/boards/ztpico2/io_exp.h"
#include "ztmb/log.h"
#include "zaptronics/boards/ztpico2/zap_adapter.h"

ztm_psu_hv_t ztm_psu_hv_1;

static ztmb_zap_i2c_t g_hv_i2c;
static ztmb_zap_lock_t g_hv_lock;
static ztpico2_connector_t g_connector;









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

    g_connector=(ztpico2_connector_t){
        .board=ztpico2_active(), .enable_pin=en_pin, .input_pin=EX_HV_PGOOD_1
    };

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
        .control_context = &g_connector,
        .prepare = ztpico2_connector_prepare,
        .enable_set = ztpico2_connector_enable_set,
        .enable_get = ztpico2_connector_enable_get,
        .pgood_read = ztpico2_connector_input_read,
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
