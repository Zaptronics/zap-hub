#include "ztmb/zap_adapter.h"

#include <string.h>

#include "pico/stdlib.h"
#include "ztmb/io_exp.h"

static int make_port(ztmb_zap_i2c_t *adapter,
                     uint8_t address_7bit,
                     uint32_t timeout_us,
                     bus_i2c_t *port)
{
    if (!adapter || !port || !adapter->i2c) {
        return -1;
    }
    const bus_i2c_config_t config = {
        .baud_hz = adapter->baud_hz,
        .timeout_us = timeout_us != 0u ? timeout_us : BUS_I2C_DEFAULT_TIMEOUT_US,
    };
    return bus_i2c_init(port, adapter->i2c, address_7bit, EX_NONE, &config) ? 0 : -1;
}

static int adapter_i2c_write(void *context,
                             uint8_t address_7bit,
                             const uint8_t *data,
                             size_t len,
                             uint32_t timeout_us)
{
    ztmb_zap_i2c_t *adapter = (ztmb_zap_i2c_t *)context;
    bus_i2c_t port = {0};
    if (make_port(adapter, address_7bit, timeout_us, &port) != 0) {
        return -1;
    }
    return bus_i2c_write_raw(&port, data, len) ? 0 : -1;
}

static int adapter_i2c_write_read(void *context,
                                  uint8_t address_7bit,
                                  const uint8_t *write_data,
                                  size_t write_len,
                                  uint8_t *read_data,
                                  size_t read_len,
                                  uint32_t timeout_us)
{
    ztmb_zap_i2c_t *adapter = (ztmb_zap_i2c_t *)context;
    bus_i2c_t port = {0};
    if (make_port(adapter, address_7bit, timeout_us, &port) != 0) {
        return -1;
    }
    return bus_i2c_write_read_raw(&port, write_data, write_len, read_data, read_len) ? 0 : -1;
}

static int adapter_i2c_probe(void *context, uint8_t address_7bit, uint32_t timeout_us)
{
    ztmb_zap_i2c_t *adapter = (ztmb_zap_i2c_t *)context;
    bus_i2c_t port = {0};
    if (make_port(adapter, address_7bit, timeout_us, &port) != 0) {
        return -1;
    }
    return bus_i2c_probe(&port) ? 0 : -1;
}

static const zap_i2c_bus_ops_t g_i2c_ops = {
    .write = adapter_i2c_write,
    .write_read = adapter_i2c_write_read,
    .probe = adapter_i2c_probe,
    .lock = NULL,
    .unlock = NULL,
};

static int adapter_spi_begin(void *context, uint32_t timeout_us)
{
    (void)timeout_us;
    ztmb_zap_spi_t *adapter = (ztmb_zap_spi_t *)context;
    return adapter && bus_spi_begin(&adapter->bus) ? ZAP_SPI_OK : ZAP_SPI_E_IO;
}

static int adapter_spi_transfer(void *context,
                                const uint8_t *tx,
                                uint8_t *rx,
                                size_t len,
                                uint8_t read_fill,
                                uint32_t timeout_us)
{
    (void)timeout_us;
    ztmb_zap_spi_t *adapter = (ztmb_zap_spi_t *)context;
    if (!adapter || len == 0u || (!tx && !rx)) {
        return ZAP_SPI_E_BAD_ARG;
    }

    if (tx || read_fill == 0u) {
        return bus_spi_transfer(&adapter->bus, tx, rx, len) ? ZAP_SPI_OK : ZAP_SPI_E_IO;
    }

    uint8_t fill[32];
    memset(fill, read_fill, sizeof(fill));
    size_t offset = 0u;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > sizeof(fill)) {
            chunk = sizeof(fill);
        }
        if (!bus_spi_transfer(&adapter->bus,
                              fill,
                              rx ? &rx[offset] : NULL,
                              chunk)) {
            return ZAP_SPI_E_IO;
        }
        offset += chunk;
    }
    return ZAP_SPI_OK;
}

static int adapter_spi_end(void *context)
{
    ztmb_zap_spi_t *adapter = (ztmb_zap_spi_t *)context;
    return adapter && bus_spi_end(&adapter->bus) ? ZAP_SPI_OK : ZAP_SPI_E_IO;
}

static const zap_spi_device_ops_t g_spi_ops = {
    .begin = adapter_spi_begin,
    .transfer = adapter_spi_transfer,
    .end = adapter_spi_end,
};

static int adapter_framed_begin(void *context, uint32_t timeout_us)
{
    (void)timeout_us;
    ztmb_zap_framed_spi_t *adapter = (ztmb_zap_framed_spi_t *)context;
    return adapter && bus_spi_claim(&adapter->bus) ? ZAP_SPI_OK : ZAP_SPI_E_LOCK;
}

static int adapter_framed_frame(void *context,
                                const uint8_t *tx,
                                uint8_t *rx,
                                size_t len,
                                uint8_t read_fill,
                                uint32_t timeout_us)
{
    (void)timeout_us;
    ztmb_zap_framed_spi_t *adapter = (ztmb_zap_framed_spi_t *)context;
    if (!adapter || len == 0u || (!tx && !rx)) return ZAP_SPI_E_BAD_ARG;
    if (tx || read_fill == 0u)
        return bus_spi_frame_locked(&adapter->bus, tx, rx, len) ? ZAP_SPI_OK : ZAP_SPI_E_IO;
    uint8_t fill[32];
    if (len > sizeof(fill)) return ZAP_SPI_E_UNSUPPORTED;
    memset(fill, read_fill, len);
    return bus_spi_frame_locked(&adapter->bus, fill, rx, len) ? ZAP_SPI_OK : ZAP_SPI_E_IO;
}

static int adapter_framed_end(void *context)
{
    ztmb_zap_framed_spi_t *adapter = (ztmb_zap_framed_spi_t *)context;
    return adapter && bus_spi_release(&adapter->bus) ? ZAP_SPI_OK : ZAP_SPI_E_STATE;
}

static const zap_spi_framed_device_ops_t g_framed_spi_ops = {
    .sequence_begin = adapter_framed_begin,
    .frame = adapter_framed_frame,
    .sequence_end = adapter_framed_end,
};

static int adapter_lock_acquire(void *context, uint32_t timeout_us)
{
    ztmb_zap_lock_t *adapter = (ztmb_zap_lock_t *)context;
    if (!adapter) {
        return -1;
    }
    if (timeout_us == 0u) {
        uint32_t owner = 0u;
        return mutex_try_enter(&adapter->mutex, &owner) ? 0 : -1;
    }
    return mutex_enter_timeout_us(&adapter->mutex, timeout_us) ? 0 : -1;
}

static void adapter_lock_release(void *context)
{
    ztmb_zap_lock_t *adapter = (ztmb_zap_lock_t *)context;
    if (adapter) {
        mutex_exit(&adapter->mutex);
    }
}

static void adapter_delay_ms(void *context, uint32_t delay_ms)
{
    (void)context;
    busy_wait_ms(delay_ms);
}

static void adapter_delay_us(void *context, uint32_t delay_us)
{
    (void)context;
    busy_wait_us_32(delay_us);
}

static uint64_t adapter_now_us(void *context)
{
    (void)context;
    return time_us_64();
}

static const zap_time_t g_time = {
    .context = NULL,
    .delay_ms = adapter_delay_ms,
    .delay_us = adapter_delay_us,
    .now_us = adapter_now_us,
};

bool ztmb_zap_i2c_init(ztmb_zap_i2c_t *adapter,
                       i2c_inst_t *i2c,
                       uint32_t baud_hz,
                       uint32_t timeout_us)
{
    if (!adapter || !i2c || baud_hz == 0u) {
        return false;
    }
    adapter->i2c = i2c;
    adapter->baud_hz = baud_hz;
    adapter->bus.context = adapter;
    adapter->bus.ops = &g_i2c_ops;
    adapter->bus.timeout_us = timeout_us;
    return true;
}

bool ztmb_zap_spi_init(ztmb_zap_spi_t *adapter,
                       spi_inst_t *spi,
                       uint cs_mux_id,
                       int8_t en_mux_id,
                       const bus_spi_config_t *config,
                       uint32_t timeout_us,
                       uint8_t read_fill)
{
    if (!adapter || !bus_spi_init(&adapter->bus, spi, cs_mux_id, en_mux_id, config)) {
        return false;
    }
    adapter->device.context = adapter;
    adapter->device.ops = &g_spi_ops;
    adapter->device.timeout_us = timeout_us;
    adapter->device.read_fill = read_fill;
    return true;
}

const zap_spi_device_t *ztmb_zap_spi_device(const ztmb_zap_spi_t *adapter)
{
    return adapter ? &adapter->device : NULL;
}

bool ztmb_zap_framed_spi_init(ztmb_zap_framed_spi_t *adapter,
                               spi_inst_t *spi,
                               uint cs_mux_id,
                               int8_t en_mux_id,
                               const bus_spi_config_t *config,
                               uint32_t timeout_us,
                               uint8_t read_fill)
{
    if (!adapter || !bus_spi_init(&adapter->bus, spi, cs_mux_id, en_mux_id, config)) return false;
    adapter->device.context = adapter;
    adapter->device.ops = &g_framed_spi_ops;
    adapter->device.timeout_us = timeout_us;
    adapter->device.read_fill = read_fill;
    return true;
}

const zap_spi_framed_device_t *ztmb_zap_framed_spi_device(const ztmb_zap_framed_spi_t *adapter)
{
    return adapter ? &adapter->device : NULL;
}

void ztmb_zap_lock_init(ztmb_zap_lock_t *adapter, uint32_t timeout_us)
{
    if (!adapter) {
        return;
    }
    mutex_init(&adapter->mutex);
    adapter->lock.context = adapter;
    adapter->lock.acquire = adapter_lock_acquire;
    adapter->lock.release = adapter_lock_release;
    adapter->lock.timeout_us = timeout_us;
}

const zap_lock_t *ztmb_zap_lock(const ztmb_zap_lock_t *adapter)
{
    return adapter ? &adapter->lock : NULL;
}

const zap_time_t *ztmb_zap_time(void)
{
    return &g_time;
}
