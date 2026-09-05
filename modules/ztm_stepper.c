#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "modules/ztm_stepper.h"
#include "ztmb/log.h"

ztm_stepper_t ztm_stepper_1;

static ztm_stepper_kval_config_t stepper_default_kval_config(void)
{
    return (ztm_stepper_kval_config_t) {
        .hold = ZTM_STEPPER_DEFAULT_KVAL_HOLD,
        .run = ZTM_STEPPER_DEFAULT_KVAL_RUN,
        .acc = ZTM_STEPPER_DEFAULT_KVAL_ACC,
        .dec = ZTM_STEPPER_DEFAULT_KVAL_DEC,
    };
}

static ztm_stepper_microstep_t stepper_default_microstep_mode(void)
{
    return ZTM_STEPPER_DEFAULT_MICROSTEP;
}

static bool stepper_microstep_valid(ztm_stepper_microstep_t mode)
{
    return mode >= ZTM_STEPPER_MICROSTEP_FULL &&
           mode <= ZTM_STEPPER_MICROSTEP_ONE_TWENTY_EIGHTH;
}

static bool stepper_module_ready(const ztm_stepper_t *module, bool report)
{
    if (!module || !module->ready || !module->spi_bus.spi ||
        module->chain.slot_count != ZTM_STEPPER_DRIVER_COUNT ||
        !module->chain.finalized) {
        if (report) {
            log_printf("Stepper module is not initialized\n");
        }
        return false;
    }

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        if (!l6470_ready(&module->drivers[i])) {
            if (report) {
                log_printf("Stepper driver %u is not initialized\n", (unsigned)(i + 1u));
            }
            return false;
        }
    }
    return true;
}

bool ztm6470_ready(void)
{
    return stepper_module_ready(&ztm_stepper_1, true);
}

static bool device_id_to_index(uint8_t device_id, uint8_t *device_index)
{
    if (!device_index || device_id < 1 || device_id > ZTM_STEPPER_DRIVER_COUNT) {
        log_printf("Invalid stepper device id %u; expected 1..%u\n",
                   device_id,
                   ZTM_STEPPER_DRIVER_COUNT);
        return false;
    }

    *device_index = (uint8_t)(device_id - 1u);
    return true;
}

static bool stepper_device_is_active(uint8_t device_id)
{
    return device_id >= 1u && device_id <= ZTM_STEPPER_ACTIVE_DRIVER_COUNT;
}

static l6470_t *stepper_driver(uint8_t device_id)
{
    uint8_t device_index;
    if (!stepper_module_ready(&ztm_stepper_1, true) ||
        !device_id_to_index(device_id, &device_index)) {
        return NULL;
    }
    return &ztm_stepper_1.drivers[device_index];
}

static bool stepper_broadcast_byte(ztm_stepper_t *module, uint8_t value)
{
    if (!stepper_module_ready(module, true) ||
        !bus_spi_chain_begin(&module->chain)) {
        return false;
    }

    bus_spi_chain_transfer_t transfers[ZTM_STEPPER_DRIVER_COUNT];
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        transfers[i] = (bus_spi_chain_transfer_t) {
            .slot = &module->slots[i],
            .tx = &value,
            .rx = NULL,
        };
    }

    bool ok = bus_spi_chain_frame(&module->chain,
                                  transfers,
                                  ZTM_STEPPER_DRIVER_COUNT);
    bool ended = bus_spi_chain_end(&module->chain);
    return ok && ended;
}

static bool stepper_active_byte(ztm_stepper_t *module, uint8_t value)
{
    if (!stepper_module_ready(module, true) ||
        !bus_spi_chain_begin(&module->chain)) {
        return false;
    }

    bus_spi_chain_transfer_t transfers[ZTM_STEPPER_ACTIVE_DRIVER_COUNT];
    for (size_t i = 0; i < ZTM_STEPPER_ACTIVE_DRIVER_COUNT; i++) {
        transfers[i] = (bus_spi_chain_transfer_t) {
            .slot = &module->slots[i],
            .tx = &value,
            .rx = NULL,
        };
    }

    bool ok = bus_spi_chain_frame(&module->chain,
                                  transfers,
                                  ZTM_STEPPER_ACTIVE_DRIVER_COUNT);
    bool ended = bus_spi_chain_end(&module->chain);
    return ok && ended;
}

static bool stepper_run_all_encoded(ztm_stepper_t *module,
                                    bool dir_fwd,
                                    uint32_t speed)
{
    if (!stepper_module_ready(module, true) ||
        !bus_spi_chain_begin(&module->chain)) {
        return false;
    }

    uint8_t frames[L6470_RUN_FRAME_COUNT];
    l6470_encode_run(dir_fwd, speed, frames);

    bool ok = true;
    for (size_t frame_index = 0; frame_index < L6470_RUN_FRAME_COUNT; frame_index++) {
        bus_spi_chain_transfer_t transfers[ZTM_STEPPER_ACTIVE_DRIVER_COUNT];
        for (size_t device = 0; device < ZTM_STEPPER_ACTIVE_DRIVER_COUNT; device++) {
            transfers[device] = (bus_spi_chain_transfer_t) {
                .slot = &module->slots[device],
                .tx = &frames[frame_index],
                .rx = NULL,
            };
        }

        if (!bus_spi_chain_frame(&module->chain,
                                 transfers,
                                 ZTM_STEPPER_ACTIVE_DRIVER_COUNT)) {
            ok = false;
            break;
        }
    }

    bool ended = bus_spi_chain_end(&module->chain);
    return ok && ended;
}

static bool stepper_apply_kval_config(l6470_t *driver,
                                      const ztm_stepper_kval_config_t *config)
{
    if (!driver || !l6470_ready(driver) || !config) {
        return false;
    }

    return l6470_set_param(driver, L6470_REG_KVAL_HOLD, config->hold, 1) &&
           l6470_set_param(driver, L6470_REG_KVAL_RUN, config->run, 1) &&
           l6470_set_param(driver, L6470_REG_KVAL_ACC, config->acc, 1) &&
           l6470_set_param(driver, L6470_REG_KVAL_DEC, config->dec, 1);
}

static bool stepper_apply_verified_config(l6470_t *driver,
                                          const ztm_stepper_kval_config_t *config,
                                          ztm_stepper_microstep_t microstep_mode)
{
    if (!driver || !l6470_ready(driver) || !config ||
        !stepper_microstep_valid(microstep_mode)) {
        return false;
    }

    const uint16_t acc = l6470_acc_from_sps2(25.0f);
    const uint16_t dec = l6470_acc_from_sps2(25.0f);
    const uint16_t max_speed = (uint16_t)(500.0f * 0.065536f + 0.5f);

    return l6470_set_param(driver,
                           L6470_REG_STEP_MODE,
                           (uint8_t)microstep_mode,
                           1) &&
           l6470_set_param(driver, L6470_REG_ACC, acc, 2) &&
           l6470_set_param(driver, L6470_REG_DEC, dec, 2) &&
           l6470_set_param(driver, L6470_REG_MAX_SPEED, max_speed, 2) &&
           stepper_apply_kval_config(driver, config);
}

static bool stepper_read_status_all(ztm_stepper_t *module,
                                    uint16_t status[ZTM_STEPPER_DRIVER_COUNT])
{
    if (!module || !status || !stepper_module_ready(module, false) ||
        !bus_spi_chain_begin(&module->chain)) {
        return false;
    }

    uint8_t command = L6470_CMD_GET_STATUS;
    uint8_t status_msb[ZTM_STEPPER_DRIVER_COUNT] = {0};
    uint8_t status_lsb[ZTM_STEPPER_DRIVER_COUNT] = {0};
    bus_spi_chain_transfer_t transfers[ZTM_STEPPER_DRIVER_COUNT];

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        transfers[i] = (bus_spi_chain_transfer_t) {
            .slot = &module->slots[i],
            .tx = &command,
            .rx = NULL,
        };
    }

    bool ok = bus_spi_chain_frame(&module->chain,
                                  transfers,
                                  ZTM_STEPPER_DRIVER_COUNT);

    for (size_t i = 0; ok && i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        transfers[i].tx = NULL;
        transfers[i].rx = &status_msb[i];
    }
    if (ok) {
        ok = bus_spi_chain_frame(&module->chain,
                                 transfers,
                                 ZTM_STEPPER_DRIVER_COUNT);
    }

    for (size_t i = 0; ok && i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        transfers[i].rx = &status_lsb[i];
    }
    if (ok) {
        ok = bus_spi_chain_frame(&module->chain,
                                 transfers,
                                 ZTM_STEPPER_DRIVER_COUNT);
    }

    bool ended = bus_spi_chain_end(&module->chain);
    if (!ok || !ended) {
        return false;
    }

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        status[i] = (uint16_t)(((uint16_t)status_msb[i] << 8) | status_lsb[i]);
    }
    return true;
}

static void stepper_process_fault_status(ztm_stepper_t *module,
                                         size_t device_index,
                                         uint16_t status)
{
    if (!module || device_index >= ZTM_STEPPER_DRIVER_COUNT) {
        return;
    }

    const bool stalled = l6470_status_has_stall(status);
    const bool over_temp = l6470_status_has_over_temp(status);

    if (stalled && !module->stall_reported[device_index]) {
        log_printf("ERROR: Stepper %u Stall!", (unsigned)(device_index + 1u));
    }
    if (over_temp && !module->over_temp_reported[device_index]) {
        log_printf("ERROR: Stepper %u Over Temp", (unsigned)(device_index + 1u));
    }

    module->stall_reported[device_index] = stalled;
    module->over_temp_reported[device_index] = over_temp;
}

static bool stepper_apply_verified_config_all(ztm_stepper_t *module)
{
    if (!stepper_module_ready(module, true)) {
        return false;
    }

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        if (!stepper_apply_verified_config(&module->drivers[i],
                                           &module->kval_config[i],
                                           module->microstep_mode[i])) {
            return false;
        }
    }

    return l6470_hard_hiz(&module->drivers[ZTM_STEPPER_UNUSED_DRIVER_ID - 1u]);
}

int ztm_stepper_init(void *ztm_stepper, void *spi, int8_t cs_pin, int8_t en_pin)
{
    if (!ztm_stepper || !spi || cs_pin < 0) {
        return -1;
    }

    log_printf("ztm_stepper_init\n");
    ztm_stepper_t *module = (ztm_stepper_t *)ztm_stepper;
    memset(module, 0, sizeof(*module));
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        module->kval_config[i] = stepper_default_kval_config();
        module->microstep_mode[i] = stepper_default_microstep_mode();
    }

    int result = -2;
    if (!bus_spi_init(&module->spi_bus,
                      (spi_inst_t *)spi,
                      (uint)cs_pin,
                      en_pin,
                      &l6470_spi_config)) {
        goto fail;
    }

    result = -3;
    if (!bus_spi_en(&module->spi_bus, true)) {
        goto fail;
    }
    busy_wait_ms(1);

    result = -4;
    if (!bus_spi_chain_init(&module->chain, &module->spi_bus)) {
        goto fail;
    }

    /* Physical order: slot 0 is the L6470 closest to the Pico. */
    result = -5;
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        if (!bus_spi_chain_add_slot(&module->chain,
                                    &module->slots[i],
                                    &l6470_chain_slot_config)) {
            goto fail;
        }
    }

    result = -6;
    if (!bus_spi_chain_finalize(&module->chain)) {
        goto fail;
    }

    result = -7;
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        if (!bus_spi_target_init_chain_slot(&module->targets[i], &module->slots[i]) ||
            !l6470_init(&module->drivers[i], &module->targets[i])) {
            goto fail;
        }
    }

    /* Mark ready before using the normal checked broadcast helper. */
    module->ready = true;
    result = -8;
    if (!stepper_broadcast_byte(module, L6470_CMD_RESET_DEVICE)) {
        goto fail;
    }
    busy_wait_ms(1);

    result = -9;
    if (!stepper_apply_verified_config_all(module)) {
        goto fail;
    }

    log_printf("Stepper defaults applied; motor %u held in hard HiZ\n",
               ZTM_STEPPER_UNUSED_DRIVER_ID);
    return 0;

fail:
    module->ready = false;
    if (module->spi_bus.spi) {
        (void)bus_spi_en(&module->spi_bus, false);
    }
    return result;
}

int ztm_stepper_poll(void)
{
    ztm_stepper_t *module = &ztm_stepper_1;
    uint16_t status[ZTM_STEPPER_DRIVER_COUNT] = {0};

    if (!stepper_read_status_all(module, status)) {
        return -1;
    }

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        stepper_process_fault_status(module, i, status[i]);
    }
    return 0;
}

bool ztm_stepper_nop(uint8_t responses[ZTM_STEPPER_DRIVER_COUNT])
{
    if (!responses || !stepper_module_ready(&ztm_stepper_1, true) ||
        !bus_spi_chain_begin(&ztm_stepper_1.chain)) {
        return false;
    }

    bus_spi_chain_transfer_t transfers[ZTM_STEPPER_DRIVER_COUNT];
    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        responses[i] = 0;
        transfers[i] = (bus_spi_chain_transfer_t) {
            .slot = &ztm_stepper_1.slots[i],
            .tx = NULL, /* Slot-specific idle frame: L6470 NOP. */
            .rx = &responses[i],
        };
    }

    bool ok = bus_spi_chain_frame(&ztm_stepper_1.chain,
                                  transfers,
                                  ZTM_STEPPER_DRIVER_COUNT);
    bool ended = bus_spi_chain_end(&ztm_stepper_1.chain);
    return ok && ended;
}

bool ztm_stepper_get_status(uint8_t device_id, uint16_t *status)
{
    l6470_t *driver = stepper_driver(device_id);
    return driver && status && l6470_get_status(driver, status);
}

bool ztm_stepper_get_param(uint8_t device_id,
                           uint8_t reg,
                           uint8_t nbytes,
                           uint32_t *value)
{
    l6470_t *driver = stepper_driver(device_id);
    return driver && l6470_get_param(driver, reg, nbytes, value);
}

bool ztm_stepper_set_param(uint8_t device_id,
                           uint8_t reg,
                           uint8_t nbytes,
                           uint32_t value)
{
    uint8_t device_index;
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !device_id_to_index(device_id, &device_index) ||
        !l6470_set_param(driver, reg, value, nbytes)) {
        return false;
    }

    if (nbytes == 1u && value <= UINT8_MAX) {
        ztm_stepper_kval_config_t *config = &ztm_stepper_1.kval_config[device_index];
        switch (reg) {
            case L6470_REG_KVAL_HOLD: config->hold = (uint8_t)value; break;
            case L6470_REG_KVAL_RUN:  config->run = (uint8_t)value; break;
            case L6470_REG_KVAL_ACC:  config->acc = (uint8_t)value; break;
            case L6470_REG_KVAL_DEC:  config->dec = (uint8_t)value; break;
            case L6470_REG_STEP_MODE:
                ztm_stepper_1.microstep_mode[device_index] =
                    (ztm_stepper_microstep_t)(value & 0x07u);
                break;
            default: break;
        }
    }
    return true;
}

bool ztm_stepper_get_kval_config(uint8_t device_id,
                                 ztm_stepper_kval_config_t *config)
{
    if (!config) {
        return false;
    }

    uint32_t hold = 0;
    uint32_t run = 0;
    uint32_t acc = 0;
    uint32_t dec = 0;
    if (!ztm_stepper_get_param(device_id, L6470_REG_KVAL_HOLD, 1, &hold) ||
        !ztm_stepper_get_param(device_id, L6470_REG_KVAL_RUN, 1, &run) ||
        !ztm_stepper_get_param(device_id, L6470_REG_KVAL_ACC, 1, &acc) ||
        !ztm_stepper_get_param(device_id, L6470_REG_KVAL_DEC, 1, &dec)) {
        return false;
    }

    config->hold = (uint8_t)hold;
    config->run = (uint8_t)run;
    config->acc = (uint8_t)acc;
    config->dec = (uint8_t)dec;
    return true;
}

bool ztm_stepper_set_kval_config(uint8_t device_id,
                                 const ztm_stepper_kval_config_t *config)
{
    uint8_t device_index;
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !config ||
        !device_id_to_index(device_id, &device_index) ||
        !stepper_apply_kval_config(driver, config)) {
        return false;
    }

    ztm_stepper_1.kval_config[device_index] = *config;
    if (device_id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
        return l6470_hard_hiz(driver);
    }
    return true;
}

bool ztm_stepper_set_kval_config_all(const ztm_stepper_kval_config_t *config)
{
    if (!config) {
        return false;
    }

    for (uint8_t id = 1; id <= ZTM_STEPPER_DRIVER_COUNT; id++) {
        if (!ztm_stepper_set_kval_config(id, config)) {
            return false;
        }
    }
    return true;
}

bool ztm_stepper_get_microstep(uint8_t device_id,
                               ztm_stepper_microstep_t *mode)
{
    if (!mode) {
        return false;
    }

    uint32_t step_mode = 0;
    if (!ztm_stepper_get_param(device_id, L6470_REG_STEP_MODE, 1, &step_mode)) {
        return false;
    }

    *mode = (ztm_stepper_microstep_t)(step_mode & 0x07u);
    return stepper_microstep_valid(*mode);
}

bool ztm_stepper_set_microstep(uint8_t device_id,
                               ztm_stepper_microstep_t mode)
{
    uint8_t device_index;
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !stepper_microstep_valid(mode) ||
        !device_id_to_index(device_id, &device_index)) {
        return false;
    }

    uint32_t current_step_mode = 0;
    if (!l6470_hard_hiz(driver) ||
        !l6470_get_param(driver, L6470_REG_STEP_MODE, 1, &current_step_mode)) {
        return false;
    }

    const uint8_t updated_step_mode =
        (uint8_t)((current_step_mode & ~0x07u) | (uint8_t)mode);
    if (!l6470_set_param(driver, L6470_REG_STEP_MODE, updated_step_mode, 1) ||
        !l6470_reset_pos(driver)) {
        return false;
    }

    ztm_stepper_1.microstep_mode[device_index] = mode;
    return true;
}

bool ztm_stepper_set_microstep_all(ztm_stepper_microstep_t mode)
{
    if (!stepper_microstep_valid(mode)) {
        return false;
    }

    for (uint8_t id = 1; id <= ZTM_STEPPER_DRIVER_COUNT; id++) {
        if (!ztm_stepper_set_microstep(id, mode)) {
            return false;
        }
    }
    return true;
}

bool ztm_stepper_reset(uint8_t device_id)
{
    l6470_t *driver = stepper_driver(device_id);
    bool ok = driver && l6470_reset_device(driver);
    if (ok) {
        /* Console/HTTP commands may execute in the CYW43 IRQ callback context. */
        busy_wait_ms(1);
        ok = stepper_apply_verified_config(
            driver,
            &ztm_stepper_1.kval_config[device_id - 1u],
            ztm_stepper_1.microstep_mode[device_id - 1u]);
        if (ok && device_id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
            ok = l6470_hard_hiz(driver);
        }
        if (ok) {
            const size_t device_index = (size_t)(device_id - 1u);
            ztm_stepper_1.stall_reported[device_index] = false;
            ztm_stepper_1.over_temp_reported[device_index] = false;
        }
    }
    return ok;
}

bool ztm_stepper_reset_all(void)
{
    bool ok = stepper_broadcast_byte(&ztm_stepper_1, L6470_CMD_RESET_DEVICE);
    if (ok) {
        /* sleep_ms() panics when this command arrives through the Wi-Fi callback. */
        busy_wait_ms(1);
        ok = stepper_apply_verified_config_all(&ztm_stepper_1);
        if (ok) {
            memset(ztm_stepper_1.stall_reported, 0, sizeof(ztm_stepper_1.stall_reported));
            memset(ztm_stepper_1.over_temp_reported, 0, sizeof(ztm_stepper_1.over_temp_reported));
        }
    }
    return ok;
}

bool ztm_stepper_hard_hiz(uint8_t device_id)
{
    l6470_t *driver = stepper_driver(device_id);
    return driver && l6470_hard_hiz(driver);
}

bool ztm_stepper_hard_hiz_all(void)
{
    return stepper_broadcast_byte(&ztm_stepper_1, L6470_CMD_HARD_HIZ);
}

bool ztm_stepper_soft_stop(uint8_t device_id)
{
    l6470_t *driver = stepper_driver(device_id);
    return driver && l6470_soft_stop(driver);
}

bool ztm_stepper_soft_stop_all(void)
{
    bool ok = stepper_active_byte(&ztm_stepper_1, L6470_CMD_SOFT_STOP);
    bool unused_hiz = ztm_stepper_hard_hiz(ZTM_STEPPER_UNUSED_DRIVER_ID);
    return ok && unused_hiz;
}

bool ztm_stepper_run(uint8_t device_id, bool dir_fwd, float steps_per_s)
{
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !stepper_device_is_active(device_id) || steps_per_s < 0.0f) {
        if (device_id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
            log_printf("Stepper %u is unused and must remain in hard HiZ\n", device_id);
        }
        return false;
    }
    return l6470_run(driver, dir_fwd, l6470_speed_from_steps_per_s(steps_per_s));
}

bool ztm_stepper_run_all(bool dir_fwd, float steps_per_s)
{
    if (steps_per_s < 0.0f) {
        return false;
    }
    return stepper_run_all_encoded(&ztm_stepper_1,
                                   dir_fwd,
                                   l6470_speed_from_steps_per_s(steps_per_s));
}

bool ztm_stepper_move(uint8_t device_id, bool dir_fwd, uint32_t steps)
{
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !stepper_device_is_active(device_id)) {
        if (device_id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
            log_printf("Stepper %u is unused and must remain in hard HiZ\n", device_id);
        }
        return false;
    }
    return l6470_move(driver, dir_fwd, steps);
}

bool ztm_stepper_apply_test_config(uint8_t device_id)
{
    uint8_t device_index;
    l6470_t *driver = stepper_driver(device_id);
    if (!driver || !device_id_to_index(device_id, &device_index)) {
        return false;
    }

    if (!l6470_hard_hiz(driver)) {
        return false;
    }

    ztm_stepper_1.kval_config[device_index] = stepper_default_kval_config();
    ztm_stepper_1.microstep_mode[device_index] = stepper_default_microstep_mode();
    bool ok = stepper_apply_verified_config(
        driver,
        &ztm_stepper_1.kval_config[device_index],
        ztm_stepper_1.microstep_mode[device_index]);
    if (ok && device_id == ZTM_STEPPER_UNUSED_DRIVER_ID) {
        ok = l6470_hard_hiz(driver);
    }
    return ok;
}

bool ztm_stepper_apply_test_config_all(void)
{
    if (!ztm_stepper_hard_hiz_all()) {
        return false;
    }

    for (size_t i = 0; i < ZTM_STEPPER_DRIVER_COUNT; i++) {
        ztm_stepper_1.kval_config[i] = stepper_default_kval_config();
        ztm_stepper_1.microstep_mode[i] = stepper_default_microstep_mode();
    }
    return stepper_apply_verified_config_all(&ztm_stepper_1);
}

void ztm6470_stop_all(void)
{
    if (!ztm_stepper_soft_stop_all()) {
        log_printf("Failed to stop all stepper drivers\n");
    }
}

void ztm6470_stop(uint8_t device_id)
{
    if (!ztm_stepper_soft_stop(device_id)) {
        log_printf("Failed to stop stepper %u\n", device_id);
    }
}

void ztm6470_start_all(bool dir_fwd, float steps_per_s)
{
    if (!ztm_stepper_run_all(dir_fwd, steps_per_s)) {
        log_printf("Failed to start all stepper drivers\n");
    }
}

void ztm6470_start(uint8_t device_id, bool dir_fwd, float steps_per_s)
{
    if (!ztm_stepper_run(device_id, dir_fwd, steps_per_s)) {
        log_printf("Failed to start stepper %u\n", device_id);
    }
}

void ztm6470_test(void)
{
    if (!ztm_stepper_run_all(true, 200.0f)) {
        log_printf("Stepper test start failed\n");
        return;
    }

    uint16_t status[ZTM_STEPPER_DRIVER_COUNT] = {0};
    bool status_ok[ZTM_STEPPER_DRIVER_COUNT] = {0};
    for (uint8_t id = 1; id <= ZTM_STEPPER_DRIVER_COUNT; id++) {
        status_ok[id - 1u] = ztm_stepper_get_status(id, &status[id - 1u]);
    }

    log_printf("L6470 status = %s0x%04X %s0x%04X %s0x%04X %s0x%04X\n",
               status_ok[0] ? "" : "ERR/", status[0],
               status_ok[1] ? "" : "ERR/", status[1],
               status_ok[2] ? "" : "ERR/", status[2],
               status_ok[3] ? "" : "ERR/", status[3]);
}
