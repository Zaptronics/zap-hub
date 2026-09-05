#include <limits.h>
#include <string.h>

#include "pico/critical_section.h"
#include "pico/stdlib.h"

#include "ztmb/bus.h"
#include "ztmb/io_exp.h"

typedef struct {
    bool valid;
    bus_spi_config_t config;
} spi_config_cache_t;

typedef struct {
    bool valid;
    uint32_t baud_hz;
} i2c_config_cache_t;

/*
 * SPI0 and SPI1 use different RP2350 peripherals, but both are routed through the
 * same external chip-select decoder. Selecting a device therefore locks the whole
 * SPI fabric, not just one controller.
 */
static critical_section_t g_spi_fabric_lock;
static critical_section_t g_i2c_lock[2];
static spi_config_cache_t g_spi_cache[2];
static i2c_config_cache_t g_i2c_cache[2];
static bool g_bus_initialized;

static int spi_index(spi_inst_t *spi)
{
    if (spi == spi0) return 0;
    if (spi == spi1) return 1;
    return -1;
}

static int i2c_index(i2c_inst_t *i2c)
{
    if (i2c == i2c0) return 0;
    if (i2c == i2c1) return 1;
    return -1;
}

static bus_spi_config_t spi_default_config(void)
{
    return (bus_spi_config_t) {
        .baud_hz = BUS_SPI_DEFAULT_BAUD_HZ,
        .data_bits = BUS_SPI_DEFAULT_DATA_BITS,
        .cpol = SPI_CPOL_0,
        .cpha = SPI_CPHA_0,
        .order = SPI_MSB_FIRST,
        .cs_setup_us = 0,
        .cs_hold_us = 0,
        .cs_high_us = 0,
    };
}

static bus_i2c_config_t i2c_default_config(void)
{
    return (bus_i2c_config_t) {
        .baud_hz = BUS_I2C_DEFAULT_BAUD_HZ,
        .timeout_us = BUS_I2C_DEFAULT_TIMEOUT_US,
    };
}

static bool spi_config_valid(const bus_spi_config_t *config)
{
    return config &&
           config->baud_hz > 0 &&
           config->data_bits == 8 &&
           (config->cpol == SPI_CPOL_0 || config->cpol == SPI_CPOL_1) &&
           (config->cpha == SPI_CPHA_0 || config->cpha == SPI_CPHA_1) &&
           config->order == SPI_MSB_FIRST;
}

static bool i2c_config_valid(const bus_i2c_config_t *config)
{
    return config && config->baud_hz > 0 && config->timeout_us > 0;
}

static bool spi_config_equal(const bus_spi_config_t *a, const bus_spi_config_t *b)
{
    return a->baud_hz == b->baud_hz &&
           a->data_bits == b->data_bits &&
           a->cpol == b->cpol &&
           a->cpha == b->cpha &&
           a->order == b->order;
}

static bool spi_apply_config(bus_spi_t *port)
{
    int index = spi_index(port->spi);
    if (index < 0 || !spi_config_valid(&port->config)) {
        return false;
    }

    spi_config_cache_t *cache = &g_spi_cache[index];
    if (!cache->valid || !spi_config_equal(&cache->config, &port->config)) {
        (void)spi_set_baudrate(port->spi, port->config.baud_hz);
        spi_set_format(port->spi,
                       port->config.data_bits,
                       port->config.cpol,
                       port->config.cpha,
                       port->config.order);
        cache->config = port->config;
        cache->valid = true;
    }

    return true;
}

static bool i2c_apply_config(bus_i2c_t *port)
{
    int index = i2c_index(port->i2c);
    if (index < 0 || !i2c_config_valid(&port->config)) {
        return false;
    }

    i2c_config_cache_t *cache = &g_i2c_cache[index];
    if (!cache->valid || cache->baud_hz != port->config.baud_hz) {
        (void)i2c_set_baudrate(port->i2c, port->config.baud_hz);
        cache->baud_hz = port->config.baud_hz;
        cache->valid = true;
    }

    return true;
}

static bool i2c_transaction_begin(bus_i2c_t *port, int *index_out)
{
    if (!g_bus_initialized || !port || !port->i2c) {
        return false;
    }

    int index = i2c_index(port->i2c);
    if (index < 0) {
        return false;
    }

    critical_section_enter_blocking(&g_i2c_lock[index]);
    if (!i2c_apply_config(port)) {
        critical_section_exit(&g_i2c_lock[index]);
        return false;
    }

    if (index_out) {
        *index_out = index;
    }
    return true;
}

static void i2c_transaction_end(int index)
{
    if (index >= 0 && index < 2) {
        critical_section_exit(&g_i2c_lock[index]);
    }
}

bool bus_init(void)
{
    if (!g_bus_initialized) {
        critical_section_init(&g_spi_fabric_lock);
        critical_section_init(&g_i2c_lock[0]);
        critical_section_init(&g_i2c_lock[1]);
        memset(g_spi_cache, 0, sizeof(g_spi_cache));
        memset(g_i2c_cache, 0, sizeof(g_i2c_cache));
        g_bus_initialized = true;
    }

    cs_select_line(CS_NO_LINE);
    return true;
}

bool bus_i2c_init(bus_i2c_t *port,
                  i2c_inst_t *i2c,
                  uint8_t addr,
                  int8_t en_mux_id,
                  const bus_i2c_config_t *config)
{
    if (!port || !i2c || addr > 0x7Fu || i2c_index(i2c) < 0) {
        return false;
    }

    bus_i2c_config_t selected = config ? *config : i2c_default_config();
    if (!i2c_config_valid(&selected)) {
        return false;
    }

    port->addr = addr;
    port->i2c = i2c;
    port->en_mux_id = en_mux_id;
    port->config = selected;
    return true;
}

bool bus_i2c_set_config(bus_i2c_t *port, const bus_i2c_config_t *config)
{
    if (!port || !i2c_config_valid(config)) {
        return false;
    }

    if (!g_bus_initialized || !port->i2c) {
        port->config = *config;
        return true;
    }

    int index = i2c_index(port->i2c);
    if (index < 0) {
        return false;
    }

    critical_section_enter_blocking(&g_i2c_lock[index]);
    port->config = *config;
    critical_section_exit(&g_i2c_lock[index]);
    return true;
}

bool bus_i2c_send_byte(bus_i2c_t *port, uint8_t cmd)
{
    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }

    int written = i2c_write_timeout_us(port->i2c,
                                       port->addr,
                                       &cmd,
                                       1,
                                       false,
                                       port->config.timeout_us);
    i2c_transaction_end(index);
    return written == 1;
}

bool bus_i2c_write_raw(bus_i2c_t *port, const uint8_t *data, size_t len)
{
    if (!port || !data || len == 0u || len > (size_t)INT_MAX) {
        return false;
    }

    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }
    int written = i2c_write_timeout_us(port->i2c,
                                       port->addr,
                                       data,
                                       len,
                                       false,
                                       port->config.timeout_us);
    i2c_transaction_end(index);
    return written == (int)len;
}

bool bus_i2c_read_raw(bus_i2c_t *port, uint8_t *data, size_t len)
{
    if (!port || !data || len == 0u || len > (size_t)INT_MAX) {
        return false;
    }

    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }
    int read = i2c_read_timeout_us(port->i2c,
                                   port->addr,
                                   data,
                                   len,
                                   false,
                                   port->config.timeout_us);
    i2c_transaction_end(index);
    return read == (int)len;
}

bool bus_i2c_write_read_raw(bus_i2c_t *port,
                            const uint8_t *write_data,
                            size_t write_len,
                            uint8_t *read_data,
                            size_t read_len)
{
    if (!port || !write_data || write_len == 0u || !read_data || read_len == 0u ||
        write_len > (size_t)INT_MAX || read_len > (size_t)INT_MAX) {
        return false;
    }

    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }

    int written = i2c_write_timeout_us(port->i2c,
                                       port->addr,
                                       write_data,
                                       write_len,
                                       true,
                                       port->config.timeout_us);
    bool ok = false;
    if (written == (int)write_len) {
        int read = i2c_read_timeout_us(port->i2c,
                                       port->addr,
                                       read_data,
                                       read_len,
                                       false,
                                       port->config.timeout_us);
        ok = read == (int)read_len;
    }

    i2c_transaction_end(index);
    return ok;
}

bool bus_i2c_probe(bus_i2c_t *port)
{
    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }

    /*
     * The Pico SDK bus-scan example uses a one-byte dummy read to obtain an
     * address ACK without writing a register value. The returned byte is
     * intentionally discarded. A diagnostic scan may advance a device's
     * internal read pointer or clear a read-to-clear register, so callers
     * should use this only for bench discovery and reinitialize afterward.
     */
    uint8_t discard = 0;
    int read = i2c_read_timeout_us(port->i2c,
                                  port->addr,
                                  &discard,
                                  1,
                                  false,
                                  port->config.timeout_us);
    i2c_transaction_end(index);
    return read == 1;
}

bool bus_i2c_write_n(bus_i2c_t *port, uint8_t reg, const uint8_t *value, size_t len)
{
    if (!port || (!value && len > 0) || len > (BUS_I2C_MAX_WRITE_BYTES - 1u)) {
        return false;
    }

    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }

    uint8_t frame[BUS_I2C_MAX_WRITE_BYTES];
    frame[0] = reg;
    if (len > 0) {
        memcpy(&frame[1], value, len);
    }

    int written = i2c_write_timeout_us(port->i2c,
                                       port->addr,
                                       frame,
                                       len + 1u,
                                       false,
                                       port->config.timeout_us);
    i2c_transaction_end(index);
    return written == (int)(len + 1u);
}

bool bus_i2c_read_n(bus_i2c_t *port, uint8_t reg, uint8_t *value, size_t len)
{
    if (!port || !value || len == 0 || len > (size_t)INT_MAX) {
        return false;
    }

    int index = -1;
    if (!i2c_transaction_begin(port, &index)) {
        return false;
    }

    int written = i2c_write_timeout_us(port->i2c,
                                       port->addr,
                                       &reg,
                                       1,
                                       true,
                                       port->config.timeout_us);
    bool ok = false;
    if (written == 1) {
        int read = i2c_read_timeout_us(port->i2c,
                                       port->addr,
                                       value,
                                       len,
                                       false,
                                       port->config.timeout_us);
        ok = read == (int)len;
    }

    i2c_transaction_end(index);
    return ok;
}

bool bus_i2c_write(bus_i2c_t *port, uint8_t reg, uint8_t value)
{
    return bus_i2c_write_n(port, reg, &value, 1);
}

bool bus_i2c_read(bus_i2c_t *port, uint8_t reg, uint8_t *value)
{
    return bus_i2c_read_n(port, reg, value, 1);
}

bool bus_i2c_en(bus_i2c_t *port, bool state)
{
    if (!port) {
        return false;
    }
    if (port->en_mux_id == EX_NONE) {
        return true;
    }
    return pcal6416a_write_pin(&ioexp, port->en_mux_id, state);
}


bool bus_spi_init(bus_spi_t *port,
                  spi_inst_t *spi,
                  uint cs_mux_id,
                  int8_t en_mux_id,
                  const bus_spi_config_t *config)
{
    if (!port || !spi || spi_index(spi) < 0 || cs_mux_id > 15u) {
        return false;
    }

    bus_spi_config_t selected = config ? *config : spi_default_config();
    if (!spi_config_valid(&selected)) {
        return false;
    }

    port->spi = spi;
    port->cs_mux_id = cs_mux_id;
    port->en_mux_id = en_mux_id;
    port->num_devices = 1;
    port->config = selected;
    port->transaction_active = false;
    port->cs_active = false;
    port->transaction_core = UINT8_MAX;
    return true;
}

bool bus_spi_set_config(bus_spi_t *port, const bus_spi_config_t *config)
{
    if (!port || !spi_config_valid(config) || port->transaction_active) {
        return false;
    }

    if (!g_bus_initialized || !port->spi) {
        port->config = *config;
        return true;
    }

    critical_section_enter_blocking(&g_spi_fabric_lock);
    bool ok = !port->transaction_active;
    if (ok) {
        port->config = *config;
    }
    critical_section_exit(&g_spi_fabric_lock);
    return ok;
}

bool bus_spi_set_mode(bus_spi_t *port, spi_cpol_t cpol, spi_cpha_t cpha)
{
    if (!port ||
        (cpol != SPI_CPOL_0 && cpol != SPI_CPOL_1) ||
        (cpha != SPI_CPHA_0 && cpha != SPI_CPHA_1)) {
        return false;
    }

    /*
     * During an explicit transaction the owner may stage the mode for the next
     * transaction. IRQs are already disabled and the shared fabric lock is held.
     */
    if (port->transaction_active) {
        if (port->transaction_core != (uint8_t)get_core_num()) {
            return false;
        }
        port->config.cpol = cpol;
        port->config.cpha = cpha;
        return true;
    }

    if (!g_bus_initialized || !port->spi) {
        port->config.cpol = cpol;
        port->config.cpha = cpha;
        return true;
    }

    critical_section_enter_blocking(&g_spi_fabric_lock);
    port->config.cpol = cpol;
    port->config.cpha = cpha;
    critical_section_exit(&g_spi_fabric_lock);
    return true;
}

bool bus_spi_set_baudrate(bus_spi_t *port, uint32_t baud_hz)
{
    if (!port || baud_hz == 0 || port->transaction_active) {
        return false;
    }

    if (!g_bus_initialized || !port->spi) {
        port->config.baud_hz = baud_hz;
        return true;
    }

    critical_section_enter_blocking(&g_spi_fabric_lock);
    bool ok = !port->transaction_active;
    if (ok) {
        port->config.baud_hz = baud_hz;
    }
    critical_section_exit(&g_spi_fabric_lock);
    return ok;
}

static bool spi_owner_valid(const bus_spi_t *port)
{
    return port && port->spi && port->transaction_active &&
           port->transaction_core == (uint8_t)get_core_num();
}

static bool spi_select_locked(bus_spi_t *port)
{
    if (!spi_owner_valid(port) || port->cs_active) {
        return false;
    }

    cs_select_line(port->cs_mux_id);
    if (port->config.cs_setup_us > 0) {
        busy_wait_us_32(port->config.cs_setup_us);
    }
    port->cs_active = true;
    return true;
}

static bool spi_deselect_locked(bus_spi_t *port)
{
    if (!spi_owner_valid(port) || !port->cs_active) {
        return false;
    }

    if (port->config.cs_hold_us > 0) {
        busy_wait_us_32(port->config.cs_hold_us);
    }
    cs_deselect();
    port->cs_active = false;
    if (port->config.cs_high_us > 0) {
        busy_wait_us_32(port->config.cs_high_us);
    }
    return true;
}

static bool spi_transfer_locked(bus_spi_t *port,
                                const uint8_t *tx,
                                uint8_t *rx,
                                size_t len)
{
    if (!spi_owner_valid(port) || !port->cs_active || len == 0 ||
        len > (size_t)INT_MAX || (!tx && !rx)) {
        return false;
    }

    int transferred;
    if (tx && rx) {
        transferred = spi_write_read_blocking(port->spi, tx, rx, len);
    } else if (tx) {
        transferred = spi_write_blocking(port->spi, tx, len);
    } else {
        transferred = spi_read_blocking(port->spi, 0, rx, len);
    }

    return transferred == (int)len;
}

bool bus_spi_claim(bus_spi_t *port)
{
    if (!g_bus_initialized || !port || !port->spi) {
        return false;
    }

    critical_section_enter_blocking(&g_spi_fabric_lock);
    if (port->transaction_active || !spi_apply_config(port)) {
        critical_section_exit(&g_spi_fabric_lock);
        return false;
    }

    port->transaction_core = (uint8_t)get_core_num();
    port->transaction_active = true;
    port->cs_active = false;
    return true;
}

bool bus_spi_frame_locked(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!spi_owner_valid(port) || port->cs_active || len == 0 ||
        len > (size_t)INT_MAX || (!tx && !rx)) {
        return false;
    }

    if (!spi_select_locked(port)) {
        return false;
    }

    bool ok = spi_transfer_locked(port, tx, rx, len);
    bool deselected = spi_deselect_locked(port);
    return ok && deselected;
}

bool bus_spi_release(bus_spi_t *port)
{
    if (!spi_owner_valid(port) || port->cs_active) {
        return false;
    }

    port->transaction_active = false;
    port->transaction_core = UINT8_MAX;
    critical_section_exit(&g_spi_fabric_lock);
    return true;
}

bool bus_spi_begin(bus_spi_t *port)
{
    if (!bus_spi_claim(port)) {
        return false;
    }

    if (!spi_select_locked(port)) {
        (void)bus_spi_release(port);
        return false;
    }
    return true;
}

bool bus_spi_transfer(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len)
{
    return spi_transfer_locked(port, tx, rx, len);
}

bool bus_spi_end(bus_spi_t *port)
{
    if (!spi_owner_valid(port) || !port->cs_active) {
        return false;
    }

    bool deselected = spi_deselect_locked(port);
    bool released = deselected && bus_spi_release(port);
    return deselected && released;
}

bool bus_spi_xfer(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!bus_spi_claim(port)) {
        return false;
    }

    bool ok = bus_spi_frame_locked(port, tx, rx, len);
    bool released = bus_spi_release(port);
    return ok && released;
}

bool bus_spi_write(bus_spi_t *port, const uint8_t *tx, size_t len)
{
    return bus_spi_xfer(port, tx, NULL, len);
}

bool bus_spi_read(bus_spi_t *port, uint8_t repeated_tx, uint8_t *rx, size_t len)
{
    if (!port || !rx || len == 0 || len > (size_t)INT_MAX || !bus_spi_claim(port)) {
        return false;
    }

    if (!spi_select_locked(port)) {
        (void)bus_spi_release(port);
        return false;
    }

    int transferred = spi_read_blocking(port->spi, repeated_tx, rx, len);
    bool deselected = spi_deselect_locked(port);
    bool released = deselected && bus_spi_release(port);
    return transferred == (int)len && deselected && released;
}

bool bus_spi_en(bus_spi_t *port, bool state)
{
    if (!port) {
        return false;
    }
    if (port->en_mux_id == EX_NONE) {
        return true;
    }
    return pcal6416a_write_pin(&ioexp, port->en_mux_id, state);
}

static bool chain_slot_config_valid(const bus_spi_chain_slot_config_t *config)
{
    return config && config->frame_bytes > 0 && config->idle_tx;
}

static bool chain_ready(const bus_spi_chain_t *chain)
{
    return chain && chain->bus && chain->finalized && chain->slot_count > 0 &&
           chain->total_frame_bytes > 0 &&
           chain->total_frame_bytes <= BUS_SPI_CHAIN_MAX_FRAME_BYTES;
}

bool bus_spi_chain_init(bus_spi_chain_t *chain, bus_spi_t *bus)
{
    if (!chain || !bus || !bus->spi) {
        return false;
    }

    memset(chain, 0, sizeof(*chain));
    chain->bus = bus;
    return true;
}

bool bus_spi_chain_add_slot(bus_spi_chain_t *chain,
                            bus_spi_chain_slot_t *slot,
                            const bus_spi_chain_slot_config_t *config)
{
    if (!chain || !chain->bus || !slot || !chain_slot_config_valid(config) ||
        chain->finalized || chain->sequence_active ||
        chain->slot_count >= BUS_SPI_CHAIN_MAX_SLOTS) {
        return false;
    }

    if (config->frame_bytes > BUS_SPI_CHAIN_MAX_FRAME_BYTES ||
        chain->total_frame_bytes > BUS_SPI_CHAIN_MAX_FRAME_BYTES - config->frame_bytes) {
        return false;
    }

    for (size_t i = 0; i < chain->slot_count; i++) {
        if (chain->slots[i] == slot) {
            return false;
        }
    }

    memset(slot, 0, sizeof(*slot));
    slot->chain = chain;
    slot->config = *config;
    slot->logical_index = chain->slot_count;
    slot->registered = true;
    chain->slots[chain->slot_count++] = slot;
    chain->total_frame_bytes += config->frame_bytes;
    return true;
}

bool bus_spi_chain_finalize(bus_spi_chain_t *chain)
{
    if (!chain || !chain->bus || chain->finalized || chain->sequence_active ||
        chain->slot_count == 0 || chain->total_frame_bytes == 0 ||
        chain->total_frame_bytes > BUS_SPI_CHAIN_MAX_FRAME_BYTES) {
        return false;
    }

    size_t wire_offset = 0;
    for (size_t logical = chain->slot_count; logical-- > 0;) {
        bus_spi_chain_slot_t *slot = chain->slots[logical];
        if (!slot || !slot->registered || slot->chain != chain ||
            !chain_slot_config_valid(&slot->config)) {
            return false;
        }
        slot->wire_offset = wire_offset;
        wire_offset += slot->config.frame_bytes;
    }

    if (wire_offset != chain->total_frame_bytes) {
        return false;
    }

    chain->bus->num_devices = (uint8_t)chain->slot_count;
    chain->finalized = true;
    return true;
}

bool bus_spi_chain_begin(bus_spi_chain_t *chain)
{
    if (!chain_ready(chain) || chain->sequence_active || !bus_spi_claim(chain->bus)) {
        return false;
    }

    chain->sequence_active = true;
    return true;
}

static bool chain_prepare_idle_frame(bus_spi_chain_t *chain)
{
    if (!chain_ready(chain)) {
        return false;
    }

    memset(chain->rx_frame, 0, chain->total_frame_bytes);
    for (size_t i = 0; i < chain->slot_count; i++) {
        const bus_spi_chain_slot_t *slot = chain->slots[i];
        memcpy(&chain->tx_frame[slot->wire_offset],
               slot->config.idle_tx,
               slot->config.frame_bytes);
    }
    return true;
}

static bool chain_transfer_valid(const bus_spi_chain_t *chain,
                                 const bus_spi_chain_transfer_t *transfer)
{
    return chain && transfer && transfer->slot && transfer->slot->registered &&
           transfer->slot->chain == chain;
}

bool bus_spi_chain_frame(bus_spi_chain_t *chain,
                         const bus_spi_chain_transfer_t *transfers,
                         size_t transfer_count)
{
    if (!chain_ready(chain) || !chain->sequence_active ||
        (transfer_count > 0 && !transfers) ||
        transfer_count > chain->slot_count || !chain_prepare_idle_frame(chain)) {
        return false;
    }

    for (size_t i = 0; i < transfer_count; i++) {
        if (!chain_transfer_valid(chain, &transfers[i])) {
            return false;
        }
        for (size_t previous = 0; previous < i; previous++) {
            if (transfers[previous].slot == transfers[i].slot) {
                return false;
            }
        }

        const bus_spi_chain_slot_t *slot = transfers[i].slot;
        if (transfers[i].tx) {
            memcpy(&chain->tx_frame[slot->wire_offset],
                   transfers[i].tx,
                   slot->config.frame_bytes);
        }
    }

    if (!bus_spi_frame_locked(chain->bus,
                              chain->tx_frame,
                              chain->rx_frame,
                              chain->total_frame_bytes)) {
        return false;
    }

    for (size_t i = 0; i < transfer_count; i++) {
        if (transfers[i].rx) {
            const bus_spi_chain_slot_t *slot = transfers[i].slot;
            memcpy(transfers[i].rx,
                   &chain->rx_frame[slot->wire_offset],
                   slot->config.frame_bytes);
        }
    }
    return true;
}

bool bus_spi_chain_end(bus_spi_chain_t *chain)
{
    if (!chain_ready(chain) || !chain->sequence_active) {
        return false;
    }

    bool released = bus_spi_release(chain->bus);
    if (released) {
        chain->sequence_active = false;
    }
    return released;
}

bool bus_spi_chain_xfer(bus_spi_chain_t *chain,
                        const bus_spi_chain_transfer_t *transfers,
                        size_t transfer_count)
{
    if (!bus_spi_chain_begin(chain)) {
        return false;
    }

    bool ok = bus_spi_chain_frame(chain, transfers, transfer_count);
    bool ended = bus_spi_chain_end(chain);
    return ok && ended;
}

bool bus_spi_target_init_direct(bus_spi_target_t *target,
                                bus_spi_t *bus,
                                const bus_spi_chain_slot_config_t *config)
{
    if (!target || !bus || !bus->spi || !chain_slot_config_valid(config)) {
        return false;
    }

    memset(target, 0, sizeof(*target));
    target->kind = BUS_SPI_TARGET_DIRECT;
    target->config = *config;
    target->endpoint.direct = bus;
    return true;
}

bool bus_spi_target_init_chain_slot(bus_spi_target_t *target,
                                    bus_spi_chain_slot_t *slot)
{
    if (!target || !slot || !slot->registered || !slot->chain ||
        !slot->chain->finalized || !chain_slot_config_valid(&slot->config)) {
        return false;
    }

    memset(target, 0, sizeof(*target));
    target->kind = BUS_SPI_TARGET_CHAIN_SLOT;
    target->config = slot->config;
    target->endpoint.slot = slot;
    return true;
}

size_t bus_spi_target_frame_bytes(const bus_spi_target_t *target)
{
    if (!target || target->kind == BUS_SPI_TARGET_NONE ||
        !chain_slot_config_valid(&target->config)) {
        return 0;
    }
    return target->config.frame_bytes;
}

bool bus_spi_target_begin(bus_spi_target_t *target)
{
    if (!target || target->sequence_active || bus_spi_target_frame_bytes(target) == 0) {
        return false;
    }

    bool ok = false;
    if (target->kind == BUS_SPI_TARGET_DIRECT) {
        ok = bus_spi_claim(target->endpoint.direct);
    } else if (target->kind == BUS_SPI_TARGET_CHAIN_SLOT) {
        ok = bus_spi_chain_begin(target->endpoint.slot->chain);
    }

    if (ok) {
        target->sequence_active = true;
    }
    return ok;
}

bool bus_spi_target_frame(bus_spi_target_t *target, const uint8_t *tx, uint8_t *rx)
{
    if (!target || !target->sequence_active || bus_spi_target_frame_bytes(target) == 0) {
        return false;
    }

    const uint8_t *selected_tx = tx ? tx : target->config.idle_tx;
    if (target->kind == BUS_SPI_TARGET_DIRECT) {
        return bus_spi_frame_locked(target->endpoint.direct,
                                    selected_tx,
                                    rx,
                                    target->config.frame_bytes);
    }

    if (target->kind == BUS_SPI_TARGET_CHAIN_SLOT) {
        bus_spi_chain_transfer_t transfer = {
            .slot = target->endpoint.slot,
            .tx = selected_tx,
            .rx = rx,
        };
        return bus_spi_chain_frame(target->endpoint.slot->chain, &transfer, 1);
    }

    return false;
}

bool bus_spi_target_end(bus_spi_target_t *target)
{
    if (!target || !target->sequence_active) {
        return false;
    }

    bool ok = false;
    if (target->kind == BUS_SPI_TARGET_DIRECT) {
        ok = bus_spi_release(target->endpoint.direct);
    } else if (target->kind == BUS_SPI_TARGET_CHAIN_SLOT) {
        ok = bus_spi_chain_end(target->endpoint.slot->chain);
    }

    if (ok) {
        target->sequence_active = false;
    }
    return ok;
}

bool bus_spi_target_xfer(bus_spi_target_t *target, const uint8_t *tx, uint8_t *rx)
{
    if (!bus_spi_target_begin(target)) {
        return false;
    }

    bool ok = bus_spi_target_frame(target, tx, rx);
    bool ended = bus_spi_target_end(target);
    return ok && ended;
}
