// cs_lines.h
#pragma once

// First 74HC138 outputs (0..7)
#define ZTMB_SPI_ALT_CS_1   0
#define ZTMB_SPI_ALT_CS_2   1
#define ZTMB_STEP_CS_1      2
#define ZTMB_TEC_CS_1       4
#define ZTMB_TEC_CS_2       5
#define ZTMB_TEC_CS_3       6
#define ZTMB_TEC_CS_4       7

// Second 74HC138 outputs (8..15)
#define ZTMB_ADC_CS_2       8   // 24bit ZTM_ADC
#define ZTMB_ADC_SYNC       9
#define ZTMB_ADC_CS_1       14  // 12bit Onboard ADC
