// cs_mux.c
#include "ztmb/cs_mux.h"

static int current_line = CS_INIT_STATE;

void cs_init(void)
{
    const uint cs_pins[] = {
        CS_A0_PIN, CS_A1_PIN, CS_A2_PIN,
        CS_A3_PIN, CS_A4_PIN, CS_A5_PIN
    };

    // Configure all CS-related pins as outputs
    for (unsigned i = 0; i < 6; ++i) {
        gpio_init(cs_pins[i]);
        gpio_set_function(cs_pins[i], GPIO_FUNC_SIO);
        gpio_set_dir(cs_pins[i], GPIO_OUT);
    }

    /*
    cs_select_line(CS_NO_LINE);
    // First 74HC138 outputs (0..7)
    cs_select_line(ZTMB_SPI_ALT_CS_1);
    cs_select_line(ZTMB_SPI_ALT_CS_2);
    cs_select_line(ZTMB_STEP_CS_1);
    cs_select_line(ZTMB_TEC_CS_1);
    cs_select_line(ZTMB_TEC_CS_2);
    cs_select_line(ZTMB_TEC_CS_3);
    cs_select_line(ZTMB_TEC_CS_4);
    cs_select_line(ZTMB_ADC_CS_2);
    //cs_select_line(ZTMB_ADC_SYNC);
    cs_select_line(ZTMB_ADC_CS_1);
    */

    // Put both decoders into their idle / "none" addresses
    cs_select_line(CS_NO_LINE);

}

void cs_select_line(int line)
{
    // Normalise "none" cases
    if (line < 0 || line > 15) {
        line = CS_NO_LINE;
    }

    if (line == current_line) {
        return; // nothing to do
    }
    current_line = line;

    uint8_t addr0; // address for first 74HC138 (A2..A0)
    uint8_t addr1; // address for second 74HC138 (A2..A0)

    if (line == CS_NO_LINE) {
        // Both idle
        addr0 = CS0_NONE_ADDR & 0x07u;
        addr1 = CS1_NONE_ADDR & 0x07u;
    } else if (line < 8) {
        // Use first chip; second chip idle
        addr0 = (uint8_t)line & 0x07u;
        addr1 = CS1_NONE_ADDR & 0x07u;
    } else {
        // Use second chip; first chip idle
        uint8_t local = (uint8_t)(line - 8) & 0x07u;
        addr0 = CS0_NONE_ADDR & 0x07u;
        addr1 = local;
    }

    // Drive first 74HC138 (GPIO 6,5,4 => A0,A1,A2)
    gpio_put(CS_A0_PIN, (addr0 >> 0) & 0x01u);
    gpio_put(CS_A1_PIN, (addr0 >> 1) & 0x01u);
    gpio_put(CS_A2_PIN, (addr0 >> 2) & 0x01u);

    // Drive second 74HC138 (GPIO 26,27,28 => A0,A1,A2)
    gpio_put(CS_A3_PIN, (addr1 >> 0) & 0x01u);
    gpio_put(CS_A4_PIN, (addr1 >> 1) & 0x01u);
    gpio_put(CS_A5_PIN, (addr1 >> 2) & 0x01u);
}

int cs_get_current_line(void)
{
    return current_line;
}



