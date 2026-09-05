#include "ztmb/ztm_pwm_adapter.h"

#include "hardware/i2c.h"
#include "ztmb/bus.h"
#include "ztmb/io_exp.h"
#include "ztmb/log.h"
#include "ztmb/zap_adapter.h"

ztm_pwm_t ztm_pwm_1;

static ztmb_zap_i2c_t g_pwm_i2c;
static int8_t g_enable_pin = EX_NONE;
static int8_t g_irq_pin = EX_I2C_ALT_IO_1;

static int hub_power_set(void *context, bool enabled)
{
    (void)context;
    return g_enable_pin >= 0 && pcal6416a_write_pin(&ioexp, (uint8_t)g_enable_pin, enabled)
               ? 0
               : -1;
}

static int hub_power_get(void *context, bool *enabled)
{
    (void)context;
    return enabled && g_enable_pin >= 0 &&
           pcal6416a_read_pin(&ioexp, (uint8_t)g_enable_pin, enabled)
               ? 0
               : -1;
}

static int hub_irq_read(void *context, bool *asserted)
{
    (void)context;
    if (!asserted || g_irq_pin < 0) {
        return -1;
    }
    bool high = true;
    if (!pcal6416a_read_pin(&ioexp, (uint8_t)g_irq_pin, &high)) {
        return -1;
    }
    *asserted = !high;
    return 0;
}

static void hub_event(void *context, const ztm_pwm_event_t *event)
{
    (void)context;
    if (!event) {
        return;
    }

    switch (event->type) {
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
    (void)addr;
    if (!ctx || !i2c || en_pin < 0) {
        return -1;
    }

    g_enable_pin = en_pin;
    g_irq_pin = EX_I2C_ALT_IO_1;

    if (!pcal6416a_set_pin_direction(&ioexp, (uint8_t)g_enable_pin, false) ||
        !pcal6416a_set_pin_direction(&ioexp, (uint8_t)g_irq_pin, true)) {
        return -2;
    }

    if (!ztmb_zap_i2c_init(&g_pwm_i2c,
                           (i2c_inst_t *)i2c,
                           BUS_I2C_DEFAULT_BAUD_HZ,
                           BUS_I2C_DEFAULT_TIMEOUT_US)) {
        return -3;
    }

    const ztm_pwm_config_t config = {
        .i2c = &g_pwm_i2c.bus,
        .time = ztmb_zap_time(),
        .control_context = NULL,
        .power_set = hub_power_set,
        .power_get = hub_power_get,
        .irq_read = hub_irq_read,
        .event_context = NULL,
        .event = hub_event,
    };

    return ztm_pwm_init((ztm_pwm_t *)ctx, &config);
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
    return g_enable_pin;
}

int8_t ztmb_ztm_pwm_irq_pin(void)
{
    return g_irq_pin;
}
