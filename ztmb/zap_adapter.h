#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "pico/sync.h"
#include "zaptronics/platform/i2c.h"
#include "zaptronics/platform/lock.h"
#include "zaptronics/platform/spi.h"
#include "zaptronics/platform/time.h"
#include "ztmb/bus.h"

typedef struct {
    i2c_inst_t *i2c;
    uint32_t baud_hz;
    zap_i2c_bus_t bus;
} ztmb_zap_i2c_t;

typedef struct {
    bus_spi_t bus;
    zap_spi_device_t device;
} ztmb_zap_spi_t;

typedef struct {
    mutex_t mutex;
    zap_lock_t lock;
} ztmb_zap_lock_t;

bool ztmb_zap_i2c_init(ztmb_zap_i2c_t *adapter,
                       i2c_inst_t *i2c,
                       uint32_t baud_hz,
                       uint32_t timeout_us);

bool ztmb_zap_spi_init(ztmb_zap_spi_t *adapter,
                       spi_inst_t *spi,
                       uint cs_mux_id,
                       int8_t en_mux_id,
                       const bus_spi_config_t *config,
                       uint32_t timeout_us,
                       uint8_t read_fill);
const zap_spi_device_t *ztmb_zap_spi_device(const ztmb_zap_spi_t *adapter);

void ztmb_zap_lock_init(ztmb_zap_lock_t *adapter, uint32_t timeout_us);
const zap_lock_t *ztmb_zap_lock(const ztmb_zap_lock_t *adapter);

const zap_time_t *ztmb_zap_time(void);
