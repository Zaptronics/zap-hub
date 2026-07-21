#include "ad5282.h"


bool ad5282_init(ad5282_t *dev) {
    if (!dev) return false;


    return true;
}

// Instruction byte (write mode):
// [7]=A/B (0=RDAC1, 1=RDAC2)
// [6]=RS  (midscale reset) -> 0 (normal)
// [5]=SD  (shutdown)       -> 0 (normal)
// [4]=O1  (logic output)
// [3]=O2  (logic output)
// [2:0]=don't care -> 0
static inline uint8_t mk_instr(ad5282_channel_t ch, bool o1, bool o2) {
    uint8_t instr = 0;
    if (ch == AD5282_RDAC2) instr |= (1u << 7);
    if (o1) instr |= (1u << 4);
    if (o2) instr |= (1u << 3);
    return instr;
}

bool ad5282_write_rdac(ad5282_t *dev, ad5282_channel_t ch, uint8_t code, bool o1, bool o2) {
    return bus_i2c_write(&dev->i2c_bus, mk_instr(ch, o1, o2), code);
}

// Per datasheet, to read back you must first "select" the channel in write mode
// (instruction phase), then perform a read transaction to get 1 data byte.
bool ad5282_read_rdac(ad5282_t *dev, ad5282_channel_t ch, uint8_t *out_code) {
    if (!out_code) return false;
    return bus_i2c_read(&dev->i2c_bus, mk_instr(ch, false, false), out_code);
}

bool ad5282_series_set_ohms(ad5282_t *dev, float RAB_ohms, float desired_ohms,
                            uint8_t *out_code_ch1, uint8_t *out_code_ch2)
{
    if (RAB_ohms <= 0) return false;
    // Clamp to [0, 2*RAB]
    if (desired_ohms < 0) desired_ohms = 0;
    float max_total = 2.0f * RAB_ohms;
    if (desired_ohms > max_total) desired_ohms = max_total;

    // Model each channel (rheostat mode W-B, ignoring wiper resistance) as:
    // R ≈ (D/256) * RAB. So total desired in code-space is:
    float total_codes_f = (desired_ohms / RAB_ohms) * 256.0f; // 0..512
    // Distribute across two channels without exceeding 255 each.
    int total_codes = (int)(total_codes_f + 0.5f);
    int c1 = total_codes / 2;
    int c2 = total_codes - c1;
    if (c1 > 255) { c1 = 255; c2 = total_codes - c1; }
    if (c2 > 255) { c2 = 255; c1 = total_codes - c2; }
    if (c1 < 0) c1 = 0;
    if (c2 < 0) c2 = 0;

    bool ok1 = ad5282_write_rdac(dev, AD5282_RDAC1, (uint8_t)c1, false, false);
    bool ok2 = ad5282_write_rdac(dev, AD5282_RDAC2, (uint8_t)c2, false, false);

    if (out_code_ch1) *out_code_ch1 = (uint8_t)c1;
    if (out_code_ch2) *out_code_ch2 = (uint8_t)c2;

    return ok1 && ok2;
}
