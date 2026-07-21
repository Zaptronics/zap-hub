#pragma once

#include "ztmb/bus.h"


#ifdef __cplusplus
extern "C" {
#endif

// Compose 7-bit address from AD1/AD0 strap pins (see datasheet).
// 7-bit format: 0b01011 AD1 AD0  => 0x2C..0x2F
#define AD5282_ADDR(ad)  (uint8_t)((0x0Bu << 2) | ((ad) & 0x03u))

typedef enum { AD5282_RDAC1 = 0, AD5282_RDAC2 = 1 } ad5282_channel_t;

typedef struct {
    bus_i2c_t i2c_bus;
} ad5282_t;

bool ad5282_init(ad5282_t *dev);

// Basic API
bool ad5282_write_rdac(ad5282_t *dev, ad5282_channel_t ch, uint8_t code, bool o1, bool o2);
bool ad5282_read_rdac (ad5282_t *dev, ad5282_channel_t ch, uint8_t *out_code);

// Convenience: treat two channels in series as a single element.
// Assumes both channels have same RAB (e.g., 200 kΩ) and are wired in series.
// desired_ohms will be clamped to [0, 2*RAB_ohms].
// Splits the total code across both channels to stay within 0..255 each.
bool ad5282_series_set_ohms(ad5282_t *dev, float RAB_ohms, float desired_ohms,
                            uint8_t *out_code_ch1, uint8_t *out_code_ch2);

#ifdef __cplusplus
}
#endif
