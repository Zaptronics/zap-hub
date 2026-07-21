#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "ztmb/io_exp.h"
#include "ztmb/adc.h"

#include "ztmb.h"

int ztmb_init(void)
{


    log_printf("CS Init\n");
    cs_init();

    // Initialise the physical SPI peripherals in a neutral state. The managed
    // bus layer applies each device's baud rate and mode before every transaction.
    log_printf("BUS SPI Init\n");
    spi_init(ZTMB_SPI0_PORT, 1000*1000);
    spi_set_format(ZTMB_SPI0_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    //spi_set_format(ZTMB_SPI0_PORT, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
    gpio_set_function(ZTMB_SPI0_MISO, GPIO_FUNC_SPI);
    gpio_set_function(ZTMB_SPI0_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(ZTMB_SPI0_MOSI, GPIO_FUNC_SPI);

    spi_init(ZTMB_SPI1_PORT, 1000*1000);
    spi_set_format(ZTMB_SPI1_PORT, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(ZTMB_SPI1_MISO, GPIO_FUNC_SPI);
    gpio_set_function(ZTMB_SPI1_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(ZTMB_SPI1_MOSI, GPIO_FUNC_SPI);

    log_printf("BUS I2C Init\n");
    // I2C Initialisation. Using it at 400Khz.
    i2c_init(ZTMB_I2C0_PORT, 400 * 1000);
    gpio_set_function(ZTMB_I2C0_SDA, GPIO_FUNC_I2C);
    gpio_set_function(ZTMB_I2C0_SCL, GPIO_FUNC_I2C);
    //gpio_pull_up(ZTMB_I2C0_SDA);
    //gpio_pull_up(ZTMB_I2C0_SCL);

    log_printf("PCAL6416A init I2C\n");
    // --- I2C1 setup on GPIO 18/19 ---
    i2c_init(ZTMB_I2C1_PORT, 400 * 1000); // 400 kHz
    gpio_set_function(ZTMB_I2C1_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(ZTMB_I2C1_SCL_PIN, GPIO_FUNC_I2C);
    //gpio_pull_up(ZTMB_I2C1_SDA_PIN);
    //gpio_pull_up(ZTMB_I2C1_SCL_PIN);
    // --- INT pin (active LOW, open-drain from PCAL6416A) ---
    gpio_init(ZTMB_EX_INT);
    gpio_set_dir(ZTMB_EX_INT, GPIO_IN);
    gpio_pull_up(ZTMB_EX_INT); // if you don't have an external pull-up

    if (!bus_init()) {
        log_printf("Shared bus initialization failed\n");
        return -1;
    }

    // The I/O expander uses the managed I2C layer, so initialize it after bus_init().
    log_printf("pcal6416a_init...\n");
    if (!io_exp_init(ZTMB_I2C1_PORT)) {
        log_printf("PCAL6416A init failed\n");
        return -2;
    }

    int adc_result = ztmb_adc_init(ZTMB_SPI1_PORT);
    if (adc_result != 0) {
        log_printf("Onboard ADC initialization failed, rc=%d\n", adc_result);
        return -3;
    }

    log_printf("Hub base hardware initialized\n");
    return 0;
}
