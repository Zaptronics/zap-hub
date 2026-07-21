#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "ztmb/cs_mux.h"

#define ZTMB_SPI0_PORT spi0
#define ZTMB_SPI0_MISO 0
#define ZTMB_SPI0_SCK  2
#define ZTMB_SPI0_MOSI 3

#define ZTMB_SPI1_PORT spi1
#define ZTMB_SPI1_MISO 12
#define ZTMB_SPI1_SCK  10
#define ZTMB_SPI1_MOSI 11

#define ZTMB_I2C0_PORT i2c0
#define ZTMB_I2C0_SDA  8
#define ZTMB_I2C0_SCL  9

#define ZTMB_I2C1_PORT    i2c1
#define ZTMB_I2C1_SDA_PIN 18
#define ZTMB_I2C1_SCL_PIN 19
#define ZTMB_EX_INT       20

#define BUS_SPI_DEFAULT_BAUD_HZ          1000000u
#define BUS_SPI_DEFAULT_DATA_BITS        8u
#define BUS_SPI_CHAIN_MAX_SLOTS          16u
#define BUS_SPI_CHAIN_MAX_FRAME_BYTES    128u
#define BUS_I2C_DEFAULT_BAUD_HZ          400000u
#define BUS_I2C_DEFAULT_TIMEOUT_US       5000u
#define BUS_I2C_MAX_WRITE_BYTES          64u

typedef struct {
    uint32_t baud_hz;
    uint32_t timeout_us; /* Timeout applied to each SDK I2C transfer call. */
} bus_i2c_config_t;

typedef struct {
    i2c_inst_t *i2c;
    uint8_t addr;
    int8_t en_mux_id;
    bus_i2c_config_t config;
} bus_i2c_t;

typedef struct {
    uint32_t baud_hz;
    uint8_t data_bits;
    spi_cpol_t cpol;
    spi_cpha_t cpha;
    spi_order_t order;
    uint32_t cs_setup_us;
    uint32_t cs_hold_us;
    uint32_t cs_high_us;
} bus_spi_config_t;

typedef struct {
    spi_inst_t *spi;
    uint cs_mux_id;
    int8_t en_mux_id;
    uint8_t num_devices;
    bus_spi_config_t config;
    bool transaction_active;
    bool cs_active;
    uint8_t transaction_core;
} bus_spi_t;

/*
 * A chain slot describes one silicon device in a serial SPI shift chain.
 * frame_bytes is the number of bytes that device consumes during one CS frame.
 * idle_tx must point to a persistent, harmless frame supplied by that chip driver.
 */
typedef struct {
    size_t frame_bytes;
    const uint8_t *idle_tx;
} bus_spi_chain_slot_config_t;

typedef struct bus_spi_chain bus_spi_chain_t;

typedef struct {
    bus_spi_chain_t *chain;
    bus_spi_chain_slot_config_t config;
    size_t logical_index; /* Slot 0 is physically closest to the controller. */
    size_t wire_offset;
    bool registered;
} bus_spi_chain_slot_t;

struct bus_spi_chain {
    bus_spi_t *bus;
    bus_spi_chain_slot_t *slots[BUS_SPI_CHAIN_MAX_SLOTS];
    size_t slot_count;
    size_t total_frame_bytes;
    uint8_t tx_frame[BUS_SPI_CHAIN_MAX_FRAME_BYTES];
    uint8_t rx_frame[BUS_SPI_CHAIN_MAX_FRAME_BYTES];
    bool finalized;
    bool sequence_active;
};

typedef struct {
    const bus_spi_chain_slot_t *slot;
    const uint8_t *tx; /* NULL transmits this slot's idle frame. */
    uint8_t *rx;       /* Optional native-frame response. */
} bus_spi_chain_transfer_t;

typedef enum {
    BUS_SPI_TARGET_NONE = 0,
    BUS_SPI_TARGET_DIRECT,
    BUS_SPI_TARGET_CHAIN_SLOT,
} bus_spi_target_kind_t;

/*
 * Driver-facing endpoint. A chip driver can use the same framed API whether the
 * device has a private CS line or occupies one slot in a heterogeneous chain.
 */
typedef struct {
    bus_spi_target_kind_t kind;
    bus_spi_chain_slot_config_t config;
    union {
        bus_spi_t *direct;
        bus_spi_chain_slot_t *slot;
    } endpoint;
    bool sequence_active;
} bus_spi_target_t;

/* Initializes locks and places the external chip-select decoder in its idle state. */
bool bus_init(void);

/* I2C/TWI device management. */
bool bus_i2c_init(bus_i2c_t *port,
                  i2c_inst_t *i2c,
                  uint8_t addr,
                  int8_t en_mux_id,
                  const bus_i2c_config_t *config);
bool bus_i2c_set_config(bus_i2c_t *port, const bus_i2c_config_t *config);
bool bus_i2c_send_byte(bus_i2c_t *port, uint8_t cmd);
bool bus_i2c_write_n(bus_i2c_t *port, uint8_t reg, const uint8_t *value, size_t len);
bool bus_i2c_read_n(bus_i2c_t *port, uint8_t reg, uint8_t *value, size_t len);
bool bus_i2c_write(bus_i2c_t *port, uint8_t reg, uint8_t value);
bool bus_i2c_read(bus_i2c_t *port, uint8_t reg, uint8_t *value);
bool bus_i2c_en(bus_i2c_t *port, bool state);

/* SPI device management. Both SPI controllers share one external CS decoder and one critical section. */
bool bus_spi_init(bus_spi_t *port,
                  spi_inst_t *spi,
                  uint cs_mux_id,
                  int8_t en_mux_id,
                  const bus_spi_config_t *config);
bool bus_spi_set_config(bus_spi_t *port, const bus_spi_config_t *config);
/* During an explicit transaction, set_mode() stages the mode for the next transaction. */
bool bus_spi_set_mode(bus_spi_t *port, spi_cpol_t cpol, spi_cpha_t cpha);
bool bus_spi_set_baudrate(bus_spi_t *port, uint32_t baud_hz);

/*
 * Low-level framed access. claim() owns the shared SPI fabric without asserting
 * CS. frame_locked() performs one complete CS-low frame and leaves the fabric
 * claimed. release() returns the fabric. This is the primitive used by chains.
 */
bool bus_spi_claim(bus_spi_t *port);
bool bus_spi_frame_locked(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len);
bool bus_spi_release(bus_spi_t *port);

/*
 * Explicit transactions are for devices that require CS to remain asserted over
 * multiple transfer calls. begin() applies the device configuration and selects CS;
 * transfer() clocks bytes without changing CS; end() deselects and releases the lock.
 */
bool bus_spi_begin(bus_spi_t *port);
bool bus_spi_transfer(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len);
bool bus_spi_end(bus_spi_t *port);

/* One-shot transaction helpers. */
bool bus_spi_xfer(bus_spi_t *port, const uint8_t *tx, uint8_t *rx, size_t len);
bool bus_spi_write(bus_spi_t *port, const uint8_t *tx, size_t len);
bool bus_spi_read(bus_spi_t *port, uint8_t repeated_tx, uint8_t *rx, size_t len);
bool bus_spi_en(bus_spi_t *port, bool state);

/*
 * Heterogeneous SPI shift chains. Slots are added in physical order, nearest to
 * the controller first. Each frame sends the farthest slot first on the wire.
 * The bus only composes frames; the module designer owns electrical/protocol
 * compatibility between the selected silicon devices.
 */
bool bus_spi_chain_init(bus_spi_chain_t *chain, bus_spi_t *bus);
bool bus_spi_chain_add_slot(bus_spi_chain_t *chain,
                            bus_spi_chain_slot_t *slot,
                            const bus_spi_chain_slot_config_t *config);
bool bus_spi_chain_finalize(bus_spi_chain_t *chain);
bool bus_spi_chain_begin(bus_spi_chain_t *chain);
bool bus_spi_chain_frame(bus_spi_chain_t *chain,
                         const bus_spi_chain_transfer_t *transfers,
                         size_t transfer_count);
bool bus_spi_chain_end(bus_spi_chain_t *chain);
bool bus_spi_chain_xfer(bus_spi_chain_t *chain,
                        const bus_spi_chain_transfer_t *transfers,
                        size_t transfer_count);

/* Generic framed target used by chain-capable chip drivers. */
bool bus_spi_target_init_direct(bus_spi_target_t *target,
                                bus_spi_t *bus,
                                const bus_spi_chain_slot_config_t *config);
bool bus_spi_target_init_chain_slot(bus_spi_target_t *target,
                                    bus_spi_chain_slot_t *slot);
size_t bus_spi_target_frame_bytes(const bus_spi_target_t *target);
bool bus_spi_target_begin(bus_spi_target_t *target);
bool bus_spi_target_frame(bus_spi_target_t *target, const uint8_t *tx, uint8_t *rx);
bool bus_spi_target_end(bus_spi_target_t *target);
bool bus_spi_target_xfer(bus_spi_target_t *target, const uint8_t *tx, uint8_t *rx);
