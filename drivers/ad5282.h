#pragma once

#include "ztmb/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Compose the AD5282 7-bit I2C address from the AD1/AD0 strap pins.
 * 7-bit format: 0b01011 AD1 AD0 => 0x2C..0x2F.
 */
#define AD5282_ADDR(ad)  (uint8_t)((0x0Bu << 2) | ((ad) & 0x03u))

typedef enum {
    AD5282_RDAC1 = 0,
    AD5282_RDAC2 = 1,
} ad5282_channel_t;

typedef struct {
    bus_i2c_t i2c_bus;
} ad5282_t;

/* Verify that the device acknowledges at its configured address. */
bool ad5282_init(ad5282_t *dev);
bool ad5282_probe(ad5282_t *dev);

/* Basic RDAC access. O1/O2 are the AD5282 logic-output bits. */
bool ad5282_write_rdac(ad5282_t *dev,
                       ad5282_channel_t ch,
                       uint8_t code,
                       bool o1,
                       bool o2);
bool ad5282_read_rdac(ad5282_t *dev,
                      ad5282_channel_t ch,
                      uint8_t *out_code);

/*
 * Convenience helper for two equal RDACs wired as one series rheostat.
 * This is not used by the ZTM HV module but remains useful to other modules.
 */
bool ad5282_series_set_ohms(ad5282_t *dev,
                            float rab_ohms,
                            float desired_ohms,
                            uint8_t *out_code_ch1,
                            uint8_t *out_code_ch2);

#ifdef __cplusplus
}
#endif
