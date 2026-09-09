#pragma once

#include "zaptronics/modules/ztm_stepper.h"
#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ZTMB-owned physical instance of the portable ZapEE stepper module. */
extern ztm_stepper_t ztm_stepper_1;

/* Module-registry lifecycle bridge and ZTMB bus diagnostic access. */
int ztmb_stepper_init(void *ctx, void *spi, int8_t cs_pin, int8_t en_pin);
int ztmb_stepper_poll(void);
int ztmb_stepper_deinit(void);
const bus_spi_t *ztmb_stepper_bus(void);

#ifdef __cplusplus
}
#endif
