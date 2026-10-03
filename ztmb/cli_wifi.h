#pragma once

#include "zaptronics/platform/pico/network.h"

#define CLI_PORT 2323

bool init_cli_wifi(cli_line_cb_t on_line);
void wifi_write(const char *s);
bool wifi_status(char *buf);
