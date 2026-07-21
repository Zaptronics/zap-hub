#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "drivers/tla2518.h"

int ztmb_adc_init(spi_inst_t *spi);

/*
 * Read one TLA2518 input and scale it back through an external divider.
 * ratio_thousands is the input/output ratio multiplied by 1000; for the
 * TEC 130k/20k divider it is 7500 (7.5:1).
 */
bool ztmb_adc_read_channel(uint8_t channel,
                           uint32_t ratio_thousands,
                           uint16_t *code,
                           uint32_t *scaled_uV);

/* Legacy single-channel helper retained for the existing HV prototype path. */
uint32_t ztmb_adc_read(void);
