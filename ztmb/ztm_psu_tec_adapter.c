#include "ztmb/ztm_psu_tec_adapter.h"

#include <stddef.h>
#include "pico/mutex.h"

#include "zaptronics/boards/ztpico2/ztm_psu_tec.h"

ztm_psu_tec_t ztm_psu_tec_1;
ztm_psu_tec_t ztm_psu_tec_2;
ztm_psu_tec_t ztm_psu_tec_3;
ztm_psu_tec_t ztm_psu_tec_4;

typedef struct {
    ztm_psu_tec_t *module;
    ztpico2_ztm_psu_tec_t adapter;
} tec_binding_t;

static tec_binding_t g_tec[] = {
    { .module = &ztm_psu_tec_1 },
    { .module = &ztm_psu_tec_2 },
    { .module = &ztm_psu_tec_3 },
    { .module = &ztm_psu_tec_4 },
};

/* Serialize permission changes with the final EN write, including direct
 * ztm_psu_tec_start/set_enable calls that bypass cooling.c. */
auto_init_mutex(g_tec_permission_mutex);
static bool g_tec_permission; /* RAM only; boot is locked. */

static int guarded_enable_set(void *context, bool enabled)
{
    mutex_enter_blocking(&g_tec_permission_mutex);
    int rc;
    if (enabled && !g_tec_permission) {
        /* Never assert EN while locked; attempt to force it low as well. */
        (void)ztpico2_connector_enable_set(context, false);
        rc = -1;
    } else {
        rc = ztpico2_connector_enable_set(context, enabled);
    }
    mutex_exit(&g_tec_permission_mutex);
    return rc;
}

bool ztmb_tec_permission_granted(void)
{
    mutex_enter_blocking(&g_tec_permission_mutex);
    bool allowed = g_tec_permission;
    mutex_exit(&g_tec_permission_mutex);
    return allowed;
}

bool ztmb_tec_grant_permission(void)
{
    mutex_enter_blocking(&g_tec_permission_mutex);
    bool ok = true;
    for (size_t i = 0; i < sizeof(g_tec) / sizeof(g_tec[0]); ++i) {
        ztm_psu_tec_t *m = g_tec[i].module;
        bool enabled = true;
        if (!ztm_psu_tec_ready(m) || m->config.enable_set != guarded_enable_set ||
            ztpico2_connector_enable_get(&g_tec[i].adapter.connector, &enabled) != 0 ||
            (!g_tec_permission && enabled)) ok = false;
    }
    if (ok) g_tec_permission = true;
    mutex_exit(&g_tec_permission_mutex);
    return ok;
}

bool ztmb_tec_revoke_permission(void)
{
    mutex_enter_blocking(&g_tec_permission_mutex);
    g_tec_permission = false; /* Relock even when an OFF write/readback fails. */
    bool ok = true;
    for (size_t i = 0; i < sizeof(g_tec) / sizeof(g_tec[0]); ++i) {
        ztpico2_connector_t *c = &g_tec[i].adapter.connector;
        if (!c->board) { ok = false; continue; }
        bool enabled = true;
        int written = ztpico2_connector_enable_set(c, false);
        int read = ztpico2_connector_enable_get(c, &enabled);
        if (written != 0 || read != 0 || enabled) ok = false;
    }
    mutex_exit(&g_tec_permission_mutex);
    return ok;
}

static tec_binding_t *binding_for(ztm_psu_tec_t *module)
{
    for (size_t i = 0u; i < sizeof(g_tec) / sizeof(g_tec[0]); ++i) {
        if (g_tec[i].module == module) {
            return &g_tec[i];
        }
    }
    return NULL;
}

int ztmb_ztm_psu_tec_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin)
{
    ztm_psu_tec_t *module = (ztm_psu_tec_t *)ctx;
    tec_binding_t *binding = binding_for(module);
    if (binding == NULL || spi == NULL || cs_pin < 0 || en_pin < 0) {
        return -1;
    }

    /* Reinitialization invalidates permission for the whole chamber. */
    (void)ztmb_tec_revoke_permission();
    const ztpico2_ztm_psu_tec_config_t config = {
        .board = ztpico2_active(),
        .spi = (spi_inst_t *)spi,
        .cs_mux_id = (uint)cs_pin,
        .enable_pin = en_pin,
        .ref_buffered = true,
        .gain_1x = true,
    };
    int rc = ztpico2_ztm_psu_tec_init(&binding->adapter, module, &config);
    /* Library initialization only disables EN. Install the application gate
     * before exposing the initialized instance to any application caller. */
    if (module->config.control_context == &binding->adapter.connector)
        module->config.enable_set = guarded_enable_set;
    return rc;
}

static int deinit_instance(ztm_psu_tec_t *module)
{
    (void)ztmb_tec_revoke_permission();
    return ztm_psu_tec_deinit(module);
}

int ztmb_ztm_psu_tec_deinit_1(void) { return deinit_instance(&ztm_psu_tec_1); }
int ztmb_ztm_psu_tec_deinit_2(void) { return deinit_instance(&ztm_psu_tec_2); }
int ztmb_ztm_psu_tec_deinit_3(void) { return deinit_instance(&ztm_psu_tec_3); }
int ztmb_ztm_psu_tec_deinit_4(void) { return deinit_instance(&ztm_psu_tec_4); }
