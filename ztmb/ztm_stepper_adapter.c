#include "ztmb/ztm_stepper_adapter.h"

#include "hardware/spi.h"
#include "ztmb/log.h"
#include "ztmb/zap_adapter.h"

ztm_stepper_t ztm_stepper_1;
static ztmb_zap_framed_spi_t g_stepper_spi;
static ztmb_zap_lock_t g_stepper_lock;

static int hub_power_set(void *context, bool enabled)
{
    (void)context;
    return bus_spi_en(&g_stepper_spi.bus, enabled) ? 0 : -1;
}

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
    const bus_spi_config_t cfg = { .baud_hz=1000000u,.data_bits=8u,.cpol=SPI_CPOL_1,.cpha=SPI_CPHA_1,.order=SPI_MSB_FIRST,.cs_setup_us=0u,.cs_hold_us=0u,.cs_high_us=1u };
    if (!ztmb_zap_framed_spi_init(&g_stepper_spi,(spi_inst_t*)spi,(uint)cs_pin,en_pin,&cfg,ZAP_SPI_DEFAULT_TIMEOUT_US,0u)) return -2;
    ztmb_zap_lock_init(&g_stepper_lock,ZTM_STEPPER_OPERATION_TIMEOUT_US);
    const ztm_stepper_config_t zcfg = { .spi=ztmb_zap_framed_spi_device(&g_stepper_spi),.time=ztmb_zap_time(),.lock=ztmb_zap_lock(&g_stepper_lock),.control_context=NULL,.power_set=hub_power_set,.event_context=NULL,.event=hub_event };
    int rc=ztm_stepper_init(&ztm_stepper_1,&zcfg);
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
    return &g_stepper_spi.bus;
}
