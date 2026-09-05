// cli_wifi.h
#pragma once

#include <stdbool.h>
#include <stddef.h>


#define CLI_PORT 2323
#define RX_LINE_MAX 256

/*
 * Command handlers currently stream their response through wifi_write().
 * Keep enough queued output for long diagnostics/help commands while lwIP's
 * TCP send window drains asynchronously.
 */
#ifndef CLI_TX_BUFFER_MAX
#define CLI_TX_BUFFER_MAX 16384
#endif

typedef int (*cli_line_cb_t)(char *cmd, char *out, size_t out_len);

bool init_cli_wifi(cli_line_cb_t on_line);

void wifi_write(const char *s);
bool wifi_status(char *buf);
