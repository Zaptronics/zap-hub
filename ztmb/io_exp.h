// ioexp.h
#pragma once

#include "hardware/i2c.h"
#include "drivers/pcal6416.h"

extern pcal6416a_t ioexp;

#define EX_NONE            (-1)
#define EX_TEC_EN_4         PCAL6416A_PIN_P0_0
#define EX_TEC_EN_3         PCAL6416A_PIN_P0_1
#define EX_TEC_EN_2         PCAL6416A_PIN_P0_2
#define EX_TEC_EN_1         PCAL6416A_PIN_P0_3
#define EX_STEP_FAULT       PCAL6416A_PIN_P0_4
#define EX_STEP_EN_1        PCAL6416A_PIN_P0_5
#define EX_SPI_ALT_GPIO_2   PCAL6416A_PIN_P0_6
#define EX_SPI_ALT_EN_2     PCAL6416A_PIN_P0_7
#define EX_SPI_ALT_GPIO_1   PCAL6416A_PIN_P1_0
#define EX_SPI_ALT_EN_1     PCAL6416A_PIN_P1_1
#define EX_I2C_ALT_IO_1     PCAL6416A_PIN_P1_2
#define EX_I2C_ALT_EN_1     PCAL6416A_PIN_P1_3
#define EX_I2C_ADDR0        PCAL6416A_PIN_P1_4
#define EX_HV_PGOOD_1       PCAL6416A_PIN_P1_5
#define EX_HV_EN_1          PCAL6416A_PIN_P1_6
#define EX_3V3_EN           PCAL6416A_PIN_P1_7


bool io_exp_init(i2c_inst_t *i2c);
