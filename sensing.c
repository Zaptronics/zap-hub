#include "ztmb/ztmb.h"
#include "sensing.h"

static module_entry_t *g_adc_module;

int sensing_init(void)
{
    g_adc_module = modules_find_by_short_name("ztm_adc");
    if (!g_adc_module || !g_adc_module->mod_inst) {
        log_printf("Error: ztm_adc module not found\n");
        return -1;
    }
    return 0;
}

int sensing_update(void)
{
    if (!g_adc_module) {
        return -1;
    }

    /* Sensor acquisition will be connected to the system state model next. */
    return 0;
}
