#include <pico/stdlib.h>
#include <stdatomic.h>

#include "hardware/timer.h"

#include "ztmb/ztmb.h"
#include "ztmb/cli_cmds.h"
#include "sensing.h"
#include "cooling.h"
#include "pumping.h"
#include "sweeping.h"
#include "firmware_version.h"

static atomic_bool g_tick_1s = false;
static bool g_sensing_ready;
static bool g_cooling_ready;
static bool g_pumping_ready;
static bool g_sweeping_ready;

static bool tick_cb(struct repeating_timer *timer)
{
    (void)timer;
    g_tick_1s = true;
    return true;
}

static void subsystem_init(void)
{
    g_sensing_ready = sensing_init() == 0;
    g_cooling_ready = cooling_init() == 0;
    g_pumping_ready = pumping_init() == 0;
    g_sweeping_ready = sweeping_init() == 0;

    log_printf("Subsystems: sensing=%s cooling=%s pumping=%s sweeping=%s\n",
               g_sensing_ready ? "ready" : "failed",
               g_cooling_ready ? "ready" : "failed",
               g_pumping_ready ? "ready" : "failed",
               g_sweeping_ready ? "ready" : "failed");
}

int main(void)
{
    stdio_init_all();
    log_init();

    static struct repeating_timer timer;
    if (!add_repeating_timer_ms(1000, tick_cb, NULL, &timer)) {
        log_printf("Failed to start 1-second scheduler timer\n");
    }

    log_printf("%s firmware %s\n", HUB_FIRMWARE_NAME, HUB_FIRMWARE_VERSION);
    int hub_result = ztmb_init();
    if (hub_result == 0) {
        int module_result = modules_init_all(false);
        if (module_result != 0) {
            log_printf("One or more module initializations failed, first rc=%d\n", module_result);
        }

        /* Each subsystem resolves its own dependencies and may still be usable. */
        subsystem_init();
    } else {
        log_printf("Hub base initialization stopped, rc=%d\n", hub_result);
    }

    /* Keep the console available even when hardware initialization fails. */
    cli_init();

    while (true) {
        if (atomic_exchange(&g_tick_1s, false)) {
            (void)modules_poll_all();
            if (g_sensing_ready) (void)sensing_update();
            if (g_cooling_ready) (void)cooling_update();
            if (g_sweeping_ready) (void)sweeping_update();
        }

        /* Pump process deadlines need finer resolution than the 1-second sensor tick. */
        if (g_pumping_ready) (void)pumping_update();

        log_poll();
        tight_loop_contents();
    }

    return 0;
}
