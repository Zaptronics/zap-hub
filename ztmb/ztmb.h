// ztmb.h
#pragma once

#include "ztmb/bus.h"
#include "ztmb/modules.h"
#include "ztmb/log.h"
#include "ztmb/io_exp.h"

//#define PICO_DEFAULT_UART_TX_PIN 16
//#define PICO_DEFAULT_UART_RX_PIN 17

// SPI Defines
// We are going to use SPI 0, and allocate it to the following GPIO pins
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments




int ztmb_init(void);
