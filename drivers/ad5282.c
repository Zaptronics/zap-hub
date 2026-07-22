#include "drivers/ad5282.h"

static bool valid_channel(ad5282_channel_t ch)
{
    return ch == AD5282_RDAC1 || ch == AD5282_RDAC2;
}

bool ad5282_probe(ad5282_t *dev)
{
    return dev && bus_i2c_probe(&dev->i2c_bus);
}

bool ad5282_init(ad5282_t *dev)
{
    return ad5282_probe(dev);
}

/*
 * Instruction byte in normal write mode:
 *   [7] A/B: 0=RDAC1, 1=RDAC2
 *   [6] RS: midscale reset (kept 0 here)
 *   [5] SD: shutdown (kept 0; the ZTM HV hardware enable is authoritative)
 *   [4] O1
 *   [3] O2
 *   [2:0] don't care, written as 0
 */
static uint8_t make_instruction(ad5282_channel_t ch, bool o1, bool o2)
{
    uint8_t instruction = 0u;
    if (ch == AD5282_RDAC2) {
        instruction |= (uint8_t)(1u << 7);
    }
    if (o1) {
        instruction |= (uint8_t)(1u << 4);
    }
    if (o2) {
        instruction |= (uint8_t)(1u << 3);
    }
    return instruction;
}

bool ad5282_write_rdac(ad5282_t *dev,
                       ad5282_channel_t ch,
                       uint8_t code,
                       bool o1,
                       bool o2)
{
    if (!dev || !valid_channel(ch)) {
        return false;
    }
    return bus_i2c_write(&dev->i2c_bus,
                         make_instruction(ch, o1, o2),
                         code);
}

/*
 * The channel is selected in the instruction phase, followed by a one-byte read.
 */
bool ad5282_read_rdac(ad5282_t *dev,
                      ad5282_channel_t ch,
                      uint8_t *out_code)
{
    if (!dev || !out_code || !valid_channel(ch)) {
        return false;
    }
    return bus_i2c_read(&dev->i2c_bus,
                        make_instruction(ch, false, false),
                        out_code);
}

bool ad5282_series_set_ohms(ad5282_t *dev,
                            float rab_ohms,
                            float desired_ohms,
                            uint8_t *out_code_ch1,
                            uint8_t *out_code_ch2)
{
    if (!dev || rab_ohms <= 0.0f) {
        return false;
    }

    if (desired_ohms < 0.0f) {
        desired_ohms = 0.0f;
    }
    float max_total = 2.0f * rab_ohms;
    if (desired_ohms > max_total) {
        desired_ohms = max_total;
    }

    float total_codes_f = (desired_ohms / rab_ohms) * 256.0f;
    int total_codes = (int)(total_codes_f + 0.5f);
    int code1 = total_codes / 2;
    int code2 = total_codes - code1;

    if (code1 > 255) {
        code1 = 255;
        code2 = total_codes - code1;
    }
    if (code2 > 255) {
        code2 = 255;
        code1 = total_codes - code2;
    }
    if (code1 < 0) {
        code1 = 0;
    }
    if (code2 < 0) {
        code2 = 0;
    }

    bool ok1 = ad5282_write_rdac(dev,
                                 AD5282_RDAC1,
                                 (uint8_t)code1,
                                 false,
                                 false);
    bool ok2 = ad5282_write_rdac(dev,
                                 AD5282_RDAC2,
                                 (uint8_t)code2,
                                 false,
                                 false);

    if (out_code_ch1) {
        *out_code_ch1 = (uint8_t)code1;
    }
    if (out_code_ch2) {
        *out_code_ch2 = (uint8_t)code2;
    }
    return ok1 && ok2;
}
