#include "pico/stdlib.h"
#include "hardware/spi.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// -----------------------------
// SPI & pin config (adjust to taste)
// -----------------------------
#define AD7124_SPI         spi0
#define PIN_SPI_SCK        2
#define PIN_SPI_MOSI       3
#define PIN_SPI_MISO       4
#define PIN_ADC_CS         5

// -----------------------------
// AD7124 basic register addresses (per datasheet)
// -----------------------------
#define AD7124_REG_STATUS          0x00  // 8b
#define AD7124_REG_ADCCONTROL      0x01  // 16b
#define AD7124_REG_DATA            0x02  // 24b
#define AD7124_REG_IOCTRL1         0x03  // 24b (IDAC currents + MUX)
#define AD7124_REG_IOCTRL2         0x04  // 16b
#define AD7124_REG_CH0             0x09  // .. up to 0x18 (CH15), 16b each
#define AD7124_REG_CFG0            0x19  // .. CFG7 (0x20), 16b
#define AD7124_REG_FILT0           0x21  // .. FILT7 (0x28), 24b

// -----------------------------
// Handy constants
// -----------------------------
#define AD7124_COMMS_READ          0x40  // bit6 = 1 = read, 0 = write
#define AD7124_RESET_BYTES         8     // send 64 SCLKs of 1's

#define AD7124_FULLSCALE_24        16777215.0  // 2^24 - 1

// -----------------------------
// Channel register field macros (confirm against datasheet once)
// CHn (16b): [15]=EN, [14:12]=SETUP(0..7), [11:5]=AINP(0..31), [4:0]=AINM(0..31)
// -----------------------------
#define CH_EN(v)                   (((v)&0x1) << 15)
#define CH_SETUP(s)                (((s)&0x7) << 12)
#define CH_AINP(p)                 (((p)&0x1F) << 5)
#define CH_AINM(m)                 (((m)&0x1F) << 0)

// -----------------------------
// IOCTRL1 field helpers (confirm once)
//  - IDAC current code (e.g., 250uA, 500uA)
//  - Route IOUT0 to AINx (0..31)
// We'll keep a shadow, and patch only IOUT0 fields.
// -----------------------------
static uint32_t g_ioctrl1_shadow = 0;

// Example encodings (typical for ADI parts; verify your table):
// Currents: 0=off, 1=50uA, 2=100uA, 3=250uA, 4=500uA, 5=750uA, 6=1000uA, 7=1500uA
static inline uint32_t IOCTRL1_SET_IOUT0_CUR(uint32_t reg, uint8_t code) {
    // suppose bits [20:18] = IOUT0 current code
    reg &= ~(0x7u << 18);
    reg |=  ((uint32_t)(code & 0x7) << 18);
    return reg;
}
static inline uint32_t IOCTRL1_SET_IOUT0_MUX(uint32_t reg, uint8_t ain) {
    // suppose bits [4:0] = IOUT0_MUX (AIN0..AIN31)
    reg &= ~(0x1Fu << 0);
    reg |=  ((uint32_t)(ain & 0x1F) << 0);
    return reg;
}

// -----------------------------
// Minimal SPI low-level
// -----------------------------
static inline void ad7124_cs_low(void){ gpio_put(PIN_ADC_CS, 0); }
static inline void ad7124_cs_high(void){ gpio_put(PIN_ADC_CS, 1); }

static void ad7124_write_bytes(uint8_t reg, const uint8_t *data, size_t n) {
    uint8_t cmd = (reg & 0x3F); // write: bit6=0
    ad7124_cs_low();
    spi_write_blocking(AD7124_SPI, &cmd, 1);
    spi_write_blocking(AD7124_SPI, data, n);
    ad7124_cs_high();
}

static void ad7124_read_bytes(uint8_t reg, uint8_t *data, size_t n) {
    uint8_t cmd = AD7124_COMMS_READ | (reg & 0x3F);
    ad7124_cs_low();
    spi_write_blocking(AD7124_SPI, &cmd, 1);
    spi_read_blocking(AD7124_SPI, 0xFF, data, n);
    ad7124_cs_high();
}

static void ad7124_write_u16(uint8_t reg, uint16_t val){
    uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    ad7124_write_bytes(reg, b, 2);
}
static void ad7124_write_u24(uint8_t reg, uint32_t val){
    uint8_t b[3] = { (uint8_t)(val >> 16), (uint8_t)(val >> 8), (uint8_t)(val) };
    ad7124_write_bytes(reg, b, 3);
}
static uint32_t ad7124_read_u24(uint8_t reg){
    uint8_t b[3]; ad7124_read_bytes(reg, b, 3);
    return ((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | (uint32_t)b[2];
}
static uint8_t ad7124_read_u8(uint8_t reg){
    uint8_t b; ad7124_read_bytes(reg, &b, 1); return b;
}

static void ad7124_reset(void){
    // 64 SCLKs with DIN high (send 0xFF x 8)
    uint8_t ff[AD7124_RESET_BYTES]; memset(ff, 0xFF, sizeof ff);
    ad7124_cs_low();
    spi_write_blocking(AD7124_SPI, ff, sizeof ff);
    ad7124_cs_high();
    sleep_ms(2); // allow reset time
}

// -----------------------------
// Quick init for 15x 2-wire PT1000 (AIN15 common return)
// - Ratiometric: REFIN1+ = AIN15, REFIN1- = AVSS (done in board wiring)
// - We'll use SETUP0 for all channels: GAIN=1, REF=REFIN1, SINC4 @ modest ODR
// NOTE: You only need to confirm the CFG0 and FILT0 bitfields once.
// -----------------------------
static void ad7124_init_basic(void)
{
    ad7124_reset();

    // ADC_CONTROL: continuous conversion, internal clock, data status off, power mode = mid
    // Example value; confirm bits for your preferences.
    // [15:13]=power mode, [10]=ref_en?, [7:5]=clock sel, [3:0]=mode (0=cont conv)
    uint16_t adc_ctrl = 0x0000;
    // e.g., power mid (010), internal clk (000), continuous (0000)
    // adc_ctrl |= (2u << 13);
    ad7124_write_u16(AD7124_REG_ADCCONTROL, adc_ctrl);

    // CFG0: bipolar=0 (unipolar), burnout off, REF=REFIN1 (010), gain=1 (000)
    // Typical encoding: [15]=bipolar, [10:8]=ref_sel, [2:0]=gain
    uint16_t cfg0 = 0;
    // cfg0 |= (0u << 15);                  // unipolar
    // cfg0 |= (2u << 8);                   // REFIN1
    // cfg0 |= (0u << 0);                   // GAIN=1
    ad7124_write_u16(AD7124_REG_CFG0, cfg0);

    // FILT0: SINC4, FS/ODR setting. Start with a moderate ODR; you can tune later.
    // As a placeholder, write zero (datasheet default is usually SINC4 mid-rate).
    ad7124_write_u24(AD7124_REG_FILT0, 0);

	// After writing CFG0/FILT0 for RTDs, also configure CFG1/FILT1 for voltages:
	uint16_t cfg1 = 0;
	// cfg1: unipolar, REF = Internal 2.5 V, GAIN=1, input buffers ON (set the two buffer bits)
	cfg1 |= (0u << 15);           // unipolar
	cfg1 |= (0u << 8);            // REF_SEL=0 (internal 2.5 V)
	cfg1 |= (0u << 0);            // GAIN=1
	// Example: enable both input buffers if using high-value dividers:
	// cfg1 |= (1u << 4) | (1u << 5);  // confirm positions in your datasheet
	ad7124_write_u16(AD7124_REG_CFG0 + 1, cfg1);   // CFG1
	ad7124_write_u24(AD7124_REG_FILT0 + 1, 0);     // FILT1 (tune later)


    // IOCTRL1: set IOUT0 current (e.g., 500uA) and route initially to AIN0
    g_ioctrl1_shadow = 0;
    // current code example: 4 => 500uA (verify table!)
    g_ioctrl1_shadow = IOCTRL1_SET_IOUT0_CUR(g_ioctrl1_shadow, 4);
    g_ioctrl1_shadow = IOCTRL1_SET_IOUT0_MUX(g_ioctrl1_shadow, 0); // AIN0
    ad7124_write_u24(AD7124_REG_IOCTRL1, g_ioctrl1_shadow);

    // Create 15 channels: CH0..CH14 map AINP=AIN0..AIN14, AINM=AIN15, SETUP=0, ENABLED=0 for now
	const int even_pins[] = {0,2,4,6,8,10,12};
    for (int ch = 0; ch < 8; ch++) {
        int ainp = even_pins[i];
		uint16_t chval = CH_EN(0) | CH_SETUP(0) | CH_AINP(ainp) | CH_AINM(15);
        ad7124_write_u16(AD7124_REG_CH0 + ch, chval);
    }
	
	const int odd_pins[] = {1,3,5,7,9,11,13};
	for (int i = 0; i < 7; i++) {
		int ch = 8 + i;                    // put them after your 8 RTD channels
		int ainp = odd_pins[i];
		uint16_t chval = CH_EN(0) | CH_SETUP(1) | CH_AINP(ainp) | CH_AINM(0); // AINM=AVSS(0)
		ad7124_write_u16(AD7124_REG_CH0 + ch, chval);
	}

}

// Enable exactly one channel (n=0..14), others off
static void ad7124_enable_only_channel(int n){
    for (int ch = 0; ch < 15; ch++) {
        uint16_t chval = CH_EN(ch == n) | CH_SETUP(0) | CH_AINP(ch) | CH_AINM(15);
        ad7124_write_u16(AD7124_REG_CH0 + ch, chval);
    }
}

// Route IDAC (IOUT0) to AINn (0..15)
static void ad7124_route_idac_to_ain(uint8_t ain){
    g_ioctrl1_shadow = IOCTRL1_SET_IOUT0_MUX(g_ioctrl1_shadow, ain);
    ad7124_write_u24(AD7124_REG_IOCTRL1, g_ioctrl1_shadow);
}

// Read one 24-bit conversion result (blocking, simple)
static uint32_t ad7124_read_data_once(void){
    // Easiest: small delay to allow conversion; for production, poll RDY or use DRDY interrupt
    sleep_ms(2);
    return ad7124_read_u24(AD7124_REG_DATA);
}

// -----------------------------
// PT1000 math
// RREF is the precision reference resistor (Ohms).
// Convert code -> Rrtd via ratiometric relation (GAIN=1, unipolar, REFIN1).
// Then Rrtd -> Temp via Callendar–Van Dusen (fast analytic for T >= 0°C).
// -----------------------------
static inline double ad7124_code_to_r(double code_u24, double RREF){
    double ratio = code_u24 / AD7124_FULLSCALE_24;
    return RREF * ratio;
}
static double pt1000_temp_from_R(double R){
    // CVD constants for IEC 60751 (PT1000)
    const double R0 = 1000.0;
    const double A = 3.9083e-3;
    const double B = -5.775e-7;
    // For T >= 0°C, R = R0*(1 + A*T + B*T^2). Solve quadratic:
    // B*R0*T^2 + A*R0*T + (R0 - R) = 0
    double a = B*R0, b = A*R0, c = (R0 - R);
    double disc = b*b - 4*a*c;
    if (disc < 0) disc = 0;
    double Tpos = (-b + sqrt(disc)) / (2*a);
    // For T < 0°C you’d include the C term; quick approx:
    if (R < R0) {
        // Newton step with full CVD (adds C term)
        const double C = -4.183e-12;
        double T = Tpos; // start near 0
        for (int i=0;i<5;i++){
            double f = R0*(1 + A*T + B*T*T + C*(T-100)*T*T*T) - R;
            double df = R0*(A + 2*B*T + C*(4*T*T*T - 300*T*T + 200*T));
            T -= f/df;
        }
        return T;
    }
    return Tpos;
}

// -----------------------------
// Public: scan all 15 channels once
// RREF_OHMS should match your board (e.g., 2000.0)
// IDAC current is set in init (500uA in this example)
// -----------------------------
typedef struct {
    double ohms;
    double degC;
    uint32_t raw24;
} rtd_sample_t;

void ad7124_scan_all(double RREF_OHMS, rtd_sample_t out[15]){
    for (int ch = 0; ch < 15; ch++) {
        ad7124_route_idac_to_ain((uint8_t)ch);
        ad7124_enable_only_channel(ch);

        // Throw away first sample for settling (filter + IDAC reroute)
        (void)ad7124_read_data_once();

        // Take the real sample
        uint32_t raw = ad7124_read_data_once();
        double R = ad7124_code_to_r((double)raw, RREF_OHMS);
        double T = pt1000_temp_from_R(R);

        out[ch].raw24 = raw;
        out[ch].ohms  = R;
        out[ch].degC  = T;
    }
}


double ad7124_read_voltage_channel(int ch_index, double vref, double scale_k) {
    // ch_index is the channel number you assigned above (8..14)
    // vref = 2.5 (internal), scale_k = (1 + Rtop/Rbot)
    // Enable only that channel:
    for (int ch = 0; ch < 15; ch++) {
        // Keep your existing CH map; RTD CH0..CH7 use SETUP0, volts CH8..CH14 use SETUP1
        // Here just write enable bit per channel; reuse the same CH value otherwise
        uint16_t base = CH_SETUP((ch < 8) ? 0 : 1)
                      | CH_AINP((ch < 8) ? (ch*2) : (1 + 2*(ch-8)))
                      | CH_AINM((ch < 8) ? 15 : 0);
        ad7124_write_u16(AD7124_REG_CH0 + ch, base | CH_EN(ch == ch_index));
    }

    // Throw away first sample for settling
    (void)ad7124_read_data_once();
    uint32_t raw = ad7124_read_data_once();

    double v_ain = (raw / 16777215.0) * vref; // unipolar, G=1
    return v_ain * scale_k;                   // node voltage
}


// -----------------------------
// Example main()
// -----------------------------
int main() {
    stdio_init_all();

    // SPI init
    spi_init(AD7124_SPI, 1 * 1000 * 1000);
    gpio_set_function(PIN_SPI_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SPI_MISO, GPIO_FUNC_SPI);
    gpio_init(PIN_ADC_CS);
    gpio_set_dir(PIN_ADC_CS, GPIO_OUT);
    ad7124_cs_high();

    sleep_ms(50);

    ad7124_init_basic();

    const double RREF = 2000.0; // your precision reference resistor in ohms

    while (true) {
        rtd_sample_t s[15];
        ad7124_scan_all(RREF, s);

        for (int i=0;i<15;i++) {
            log_printf("CH%02d: raw=0x%06lx  R=%.2f Ω  T=%.2f °C\n",
                   i, (unsigned long)s[i].raw24, s[i].ohms, s[i].degC);
        }
        log_printf("----\n");
        sleep_ms(200);
    }
    return 0;
}
