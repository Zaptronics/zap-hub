#include "drivers/mpc4901.h"
#include "pico/stdlib.h"

static uint16_t mcp4901_make_frame(uint8_t code,
                                   bool ref_buffered,
                                   bool gain_1x,
                                   bool active)
{
    uint16_t frame = 0;
    frame |= (uint16_t)(ref_buffered ? 1u : 0u) << 14;
    frame |= (uint16_t)(gain_1x ? 1u : 0u) << 13;
    frame |= (uint16_t)(active ? 1u : 0u) << 12;
    frame |= (uint16_t)code << 4;
    return frame;
}

static bool mcp4901_write_frame(psu_tec_t *ctx, uint16_t frame)
{
    if (!ctx) {
        return false;
    }

    uint8_t bytes[2] = {
        (uint8_t)(frame >> 8),
        (uint8_t)(frame & 0xFFu),
    };
    return bus_spi_write(&ctx->spi_bus, bytes, sizeof(bytes));
}

bool mcp4901_init(psu_tec_t *ctx)
{
    if (!ctx) {
        return false;
    }

    ctx->ref_buffered = true;
    ctx->gain_1x = true;

    /* Attempt both safe-state actions even if one of them reports failure. */
    bool disabled = mcp4901_set_enable(ctx, false);
    bool shutdown = mcp4901_shutdown(ctx);
    return disabled && shutdown;
}

bool mcp4901_set_raw(psu_tec_t *ctx, uint8_t code)
{
    if (!ctx) {
        return false;
    }

    return mcp4901_write_frame(ctx,
                               mcp4901_make_frame(code,
                                                  ctx->ref_buffered,
                                                  ctx->gain_1x,
                                                  true));
}

bool mcp4901_shutdown(psu_tec_t *ctx)
{
    if (!ctx) {
        return false;
    }

    return mcp4901_write_frame(ctx,
                               mcp4901_make_frame(0,
                                                  ctx->ref_buffered,
                                                  ctx->gain_1x,
                                                  false));
}

bool mcp4901_set_then_enable(psu_tec_t *ctx, uint8_t code, uint32_t settle_us)
{
    if (!ctx || !mcp4901_set_enable(ctx, false) ||
        !mcp4901_set_raw(ctx, code) || !mcp4901_set_enable(ctx, true)) {
        return false;
    }

    /* Allow the enabled power stage and VSENSE follower to settle. */
    if (settle_us != 0u) {
        busy_wait_us_32(settle_us);
    }

    return true;
}

bool mcp4901_set_enable(psu_tec_t *ctx, bool enable_high)
{
    return ctx && bus_spi_en(&ctx->spi_bus, enable_high ? 1 : 0);
}

bool mcp4901_set_millivolts(psu_tec_t *ctx,
                            uint32_t vref_mV,
                            uint32_t vout_mV,
                            uint8_t *written_code)
{
    if (!ctx || vref_mV == 0u) {
        return false;
    }

    uint32_t full_scale_mV = ctx->gain_1x ? vref_mV : (2u * vref_mV);
    if (vout_mV >= full_scale_mV) {
        vout_mV = full_scale_mV - 1u;
    }

    uint8_t code = (uint8_t)((vout_mV * 256u) / full_scale_mV);
    if (!mcp4901_set_raw(ctx, code)) {
        return false;
    }

    if (written_code) {
        *written_code = code;
    }
    return true;
}
