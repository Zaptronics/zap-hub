#include "ztmb/adc.h"
#include "zaptronics/boards/ztpico2/board.h"

bool ztmb_adc_read_channel(uint8_t channel, uint32_t ratio_thousands,
                           uint16_t *code, uint32_t *scaled_uV)
{
    return ztpico2_adc_read(ztpico2_active(), channel, ratio_thousands, code, scaled_uV);
}

uint32_t ztmb_adc_read(void)
{
    uint32_t value = UINT32_MAX;
    (void)ztmb_adc_read_channel(1u, 7500u, NULL, &value);
    return value;
}
