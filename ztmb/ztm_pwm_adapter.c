#include "ztmb/ztm_pwm_adapter.h"

#include "zaptronics/boards/ztpico2/ztm_pwm.h"
#include "zaptronics/boards/ztpico2/pins.h"
#include "ztmb/log.h"

ztm_pwm_t ztm_pwm_1;

static ztpico2_ztm_pwm_t g_pwm_adapter;

static void hub_event(void *context, const ztm_pwm_event_t *event)
{
    (void)context;
    if (!event) {
        return;
    }

    switch (event->type) {
        case ZTM_PWM_EVENT_POWER_SAFE_ERROR:
            log_printf("ERROR: PWM POWER_SAFE %s; enable OFF requested; reinitialize after recovery\n",
                       event->detail == 40u ? "startup timeout" :
                       event->detail == 41u ? "read failed" : "went LOW");
            break;
        case ZTM_PWM_EVENT_SX1509_INTERRUPT:
            log_printf("ERROR: PWM SX1509 IRQ source 0x%04lX\n",
                       (unsigned long)(event->detail & 0xffffu));
            break;
        case ZTM_PWM_EVENT_PAC_ALERT:
            log_printf("ERROR: PWM PAC%u alert 0x%06lX\n",
                       (unsigned)event->device_1based,
                       (unsigned long)(event->detail & 0xffffffu));
            break;
        case ZTM_PWM_EVENT_FAN_WATCHDOG:
            log_printf("ERROR: PWM fan controller %u Watchdog\n",
                       (unsigned)event->device_1based);
            break;
        case ZTM_PWM_EVENT_FAN_SPINUP_FAILURE:
            log_printf("ERROR: PWM Fan %u Spin-up Failure\n",
                       (unsigned)event->channel_1based);
            break;
        case ZTM_PWM_EVENT_FAN_DRIVE_FAILURE:
            log_printf("ERROR: PWM Fan %u Drive Failure\n",
                       (unsigned)event->channel_1based);
            break;
        case ZTM_PWM_EVENT_IRQ_UNRESOLVED:
            log_printf("ERROR: PWM module IRQ asserted but no source was latched\n");
            break;
        default:
            break;
    }
}

int ztmb_ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin)
{
    (void)addr; /* The PWM module's device addresses are fixed. */
    if (ctx != &ztm_pwm_1 || !i2c || en_pin < 0) {
        return -1;
    }

    const ztpico2_ztm_pwm_config_t config = {
        .board = ztpico2_active(),
        .i2c = (i2c_inst_t *)i2c,
        .enable_pin = en_pin,
        .irq_pin = EX_I2C_ALT_IO_1,
        .irq_active_low = true,
        .event_context = NULL,
        .event = hub_event,
    };
    return ztpico2_ztm_pwm_init(&g_pwm_adapter, &ztm_pwm_1, &config);
}

int ztmb_ztm_pwm_poll(void)
{
    return ztm_pwm_poll(&ztm_pwm_1);
}

int ztmb_ztm_pwm_deinit(void)
{
    return ztm_pwm_deinit(&ztm_pwm_1);
}

int8_t ztmb_ztm_pwm_enable_pin(void)
{
    return g_pwm_adapter.connector.enable_pin;
}

int8_t ztmb_ztm_pwm_irq_pin(void)
{
    return g_pwm_adapter.connector.input_pin;
}
