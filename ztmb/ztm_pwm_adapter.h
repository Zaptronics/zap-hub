#pragma once

#include "zaptronics/modules/ztm_pwm.h"

/* Hub owns one ZTM_PWM instance; the reusable library itself has no singleton. */
extern ztm_pwm_t ztm_pwm_1;

int ztmb_ztm_pwm_init(void *ctx, void *i2c, int8_t addr, int8_t en_pin);
int ztmb_ztm_pwm_poll(void);
int ztmb_ztm_pwm_deinit(void);
int8_t ztmb_ztm_pwm_enable_pin(void);
int8_t ztmb_ztm_pwm_irq_pin(void);
