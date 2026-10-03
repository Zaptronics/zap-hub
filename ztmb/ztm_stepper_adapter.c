#include "ztmb/ztm_stepper_adapter.h"

#include "hardware/spi.h"
#include "ztmb/log.h"
#include "zaptronics/boards/ztpico2/ztm_stepper.h"

ztm_stepper_t ztm_stepper_1;
static ztpico2_ztm_stepper_t g_stepper_adapter;

static void hub_event(void *context, const ztm_stepper_event_t *event)
{
    (void)context;
    if (!event) return;
    switch (event->type) {
        case ZTM_STEPPER_EVENT_STALL_ASSERTED: log_printf("ERROR: Stepper %u Stall!\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_STALL_CLEARED: log_printf("Stepper %u stall cleared\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_THERMAL_WARNING_ASSERTED: log_printf("ERROR: Stepper %u Thermal Warning\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_THERMAL_WARNING_CLEARED: log_printf("Stepper %u thermal warning cleared\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_THERMAL_SHUTDOWN_ASSERTED: log_printf("ERROR: Stepper %u Thermal Shutdown\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_THERMAL_SHUTDOWN_CLEARED: log_printf("Stepper %u thermal shutdown cleared\n", event->driver_1based); break;
        case ZTM_STEPPER_EVENT_COMMUNICATION_ERROR: log_printf("ERROR: Stepper communication failure\n"); break;
        default: break;
    }
}

int ztmb_stepper_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ctx || ctx != &ztm_stepper_1 || !spi || cs_pin < 0) return -1;
    const ztpico2_ztm_stepper_config_t config = {
        .board = ztpico2_active(),
        .spi = (spi_inst_t *)spi,
        .cs_mux_id = (uint)cs_pin,
        .enable_pin = en_pin,
        .event_context = NULL,
        .event = hub_event,
    };
    int rc = ztpico2_ztm_stepper_init(&g_stepper_adapter, &ztm_stepper_1, &config);
    if(rc==0) log_printf("Stepper defaults applied; motor %u held in hard HiZ\n",ZTM_STEPPER_UNUSED_DRIVER_ID);
    return rc;
}
int ztmb_stepper_poll(void)
{
    return ztm_stepper_poll(&ztm_stepper_1);
}

int ztmb_stepper_deinit(void)
{
    return ztm_stepper_deinit(&ztm_stepper_1);
}

const bus_spi_t *ztmb_stepper_bus(void)
{
    return &g_stepper_adapter.spi.bus;
}
