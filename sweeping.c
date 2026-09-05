#include "ztmb/ztmb.h"
#include "modules/ztm_psu_hv.h"
#include "sweeping.h"

static module_entry_t *g_hv_module;
static int g_hv_state_requested = -1;
static int g_hv_state_actual = -1;

static psu_hv_t *hv_device(void)
{
    return g_hv_module ? (psu_hv_t *)g_hv_module->mod_inst : NULL;
}

int sweeping_init(void)
{
    g_hv_module = modules_find_by_short_name("ztm_psu_hv");
    psu_hv_t *device = hv_device();
    if (!device) {
        log_printf("Error: ztm_psu_hv module not found\n");
        return -1;
    }

    g_hv_state_requested = -1;
    g_hv_state_actual = -1;
    return ztm_psu_hv_stop(device) ? 0 : -2;
}

int sweeping_set_state(int new_state)
{
    if (new_state > 255) {
        new_state = 255;
    }
    if (new_state < -1) {
        new_state = -1;
    }
    g_hv_state_requested = new_state;
    return g_hv_state_requested;
}

int sweeping_update(void)
{
    psu_hv_t *device = hv_device();
    if (!device) {
        return -1;
    }

    if (g_hv_state_requested == g_hv_state_actual) {
        return 0;
    }

    if (g_hv_state_requested < 0) {
        if (!ztm_psu_hv_stop(device)) {
            return -2;
        }
        g_hv_state_actual = -1;
        return 0;
    }

    if (!ztm_psu_hv_start(device,
                          (uint8_t)g_hv_state_requested,
                          ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS)) {
        g_hv_state_actual = -1;
        return -3;
    }

    g_hv_state_actual = g_hv_state_requested;
    return 0;
}
