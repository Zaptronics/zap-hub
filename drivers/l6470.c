#include "drivers/l6470.h"

static const uint8_t l6470_idle_frame[] = { L6470_CMD_NOP };

const bus_spi_config_t l6470_spi_config = {
    .baud_hz = 1000000u,
    .data_bits = 8,
    .cpol = SPI_CPOL_1,
    .cpha = SPI_CPHA_1,
    .order = SPI_MSB_FIRST,
    .cs_setup_us = 0,
    .cs_hold_us = 0,
    .cs_high_us = 1,
};

const bus_spi_chain_slot_config_t l6470_chain_slot_config = {
    .frame_bytes = sizeof(l6470_idle_frame),
    .idle_tx = l6470_idle_frame,
};

bool l6470_init(l6470_t *device, bus_spi_target_t *spi_target)
{
    if (!device || !spi_target ||
        bus_spi_target_frame_bytes(spi_target) != l6470_chain_slot_config.frame_bytes) {
        return false;
    }

    device->spi = spi_target;
    return true;
}

bool l6470_ready(const l6470_t *device)
{
    return device && device->spi &&
           bus_spi_target_frame_bytes(device->spi) == l6470_chain_slot_config.frame_bytes;
}

static bool l6470_sequence(l6470_t *device,
                           const uint8_t *tx_frames,
                           uint8_t *rx_frames,
                           size_t frame_count)
{
    if (!l6470_ready(device) || !tx_frames || frame_count == 0 ||
        !bus_spi_target_begin(device->spi)) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < frame_count; i++) {
        uint8_t *rx = rx_frames ? &rx_frames[i] : NULL;
        if (!bus_spi_target_frame(device->spi, &tx_frames[i], rx)) {
            ok = false;
            break;
        }
    }

    bool ended = bus_spi_target_end(device->spi);
    return ok && ended;
}

static bool l6470_command(l6470_t *device, uint8_t command)
{
    return l6470_sequence(device, &command, NULL, 1);
}

bool l6470_reset_device(l6470_t *device)
{
    return l6470_command(device, L6470_CMD_RESET_DEVICE);
}

bool l6470_soft_stop(l6470_t *device)
{
    return l6470_command(device, L6470_CMD_SOFT_STOP);
}

bool l6470_hard_hiz(l6470_t *device)
{
    return l6470_command(device, L6470_CMD_HARD_HIZ);
}

bool l6470_reset_pos(l6470_t *device)
{
    return l6470_command(device, L6470_CMD_RESET_POS);
}

bool l6470_set_param(l6470_t *device,
                     uint8_t reg,
                     uint32_t value,
                     uint8_t nbytes)
{
    if (!l6470_ready(device) || nbytes == 0 || nbytes > sizeof(value)) {
        return false;
    }

    uint8_t frames[1u + sizeof(value)];
    frames[0] = (uint8_t)(L6470_CMD_SET_PARAM | reg);
    for (uint8_t i = 0; i < nbytes; i++) {
        uint8_t shift = (uint8_t)(8u * (nbytes - 1u - i));
        frames[1u + i] = (uint8_t)((value >> shift) & 0xFFu);
    }

    return l6470_sequence(device, frames, NULL, (size_t)nbytes + 1u);
}

bool l6470_get_param(l6470_t *device,
                     uint8_t reg,
                     uint8_t nbytes,
                     uint32_t *value)
{
    if (!l6470_ready(device) || !value || nbytes == 0 || nbytes > sizeof(*value) ||
        !bus_spi_target_begin(device->spi)) {
        return false;
    }

    bool ok = true;
    uint8_t command = (uint8_t)(L6470_CMD_GET_PARAM | reg);
    if (!bus_spi_target_frame(device->spi, &command, NULL)) {
        ok = false;
    }

    uint32_t result = 0;
    for (uint8_t i = 0; ok && i < nbytes; i++) {
        uint8_t received = 0;
        if (!bus_spi_target_frame(device->spi, NULL, &received)) {
            ok = false;
            break;
        }
        result = (result << 8) | received;
    }

    bool ended = bus_spi_target_end(device->spi);
    if (ok && ended) {
        *value = result;
        return true;
    }
    return false;
}

bool l6470_get_status(l6470_t *device, uint16_t *status)
{
    if (!l6470_ready(device) || !status || !bus_spi_target_begin(device->spi)) {
        return false;
    }

    bool ok = true;
    uint8_t command = L6470_CMD_GET_STATUS;
    if (!bus_spi_target_frame(device->spi, &command, NULL)) {
        ok = false;
    }

    uint16_t result = 0;
    for (uint8_t i = 0; ok && i < 2; i++) {
        uint8_t received = 0;
        if (!bus_spi_target_frame(device->spi, NULL, &received)) {
            ok = false;
            break;
        }
        result = (uint16_t)((result << 8) | received);
    }

    bool ended = bus_spi_target_end(device->spi);
    if (ok && ended) {
        *status = result;
        return true;
    }
    return false;
}

void l6470_encode_run(bool dir_fwd,
                      uint32_t speed_20bit,
                      uint8_t frames[L6470_RUN_FRAME_COUNT])
{
    if (!frames) {
        return;
    }
    if (speed_20bit > 0xFFFFFu) {
        speed_20bit = 0xFFFFFu;
    }

    frames[0] = (uint8_t)(L6470_CMD_RUN | (dir_fwd ? 0x01u : 0x00u));
    frames[1] = (uint8_t)((speed_20bit >> 16) & 0xFFu);
    frames[2] = (uint8_t)((speed_20bit >> 8) & 0xFFu);
    frames[3] = (uint8_t)(speed_20bit & 0xFFu);
}

void l6470_encode_move(bool dir_fwd,
                       uint32_t nsteps_22bit,
                       uint8_t frames[L6470_MOVE_FRAME_COUNT])
{
    if (!frames) {
        return;
    }
    if (nsteps_22bit > 0x3FFFFFu) {
        nsteps_22bit = 0x3FFFFFu;
    }

    frames[0] = (uint8_t)(L6470_CMD_MOVE | (dir_fwd ? 0x01u : 0x00u));
    frames[1] = (uint8_t)((nsteps_22bit >> 16) & 0x3Fu);
    frames[2] = (uint8_t)((nsteps_22bit >> 8) & 0xFFu);
    frames[3] = (uint8_t)(nsteps_22bit & 0xFFu);
}

bool l6470_run(l6470_t *device, bool dir_fwd, uint32_t speed_20bit)
{
    uint8_t frames[L6470_RUN_FRAME_COUNT];
    l6470_encode_run(dir_fwd, speed_20bit, frames);
    return l6470_sequence(device, frames, NULL, L6470_RUN_FRAME_COUNT);
}

bool l6470_move(l6470_t *device, bool dir_fwd, uint32_t nsteps_22bit)
{
    uint8_t frames[L6470_MOVE_FRAME_COUNT];
    l6470_encode_move(dir_fwd, nsteps_22bit, frames);
    return l6470_sequence(device, frames, NULL, L6470_MOVE_FRAME_COUNT);
}
