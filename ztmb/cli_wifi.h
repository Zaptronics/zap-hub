// cli_wifi.h
#pragma once


// ======== CONFIG ========
#ifndef WIFI_SSID
#define WIFI_SSID "TP-Link_F962"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "kaleukulele"
#endif

#define CLI_PORT 2323
#define RX_LINE_MAX 256

typedef int (*cli_line_cb_t)(char *cmd, char *out, size_t out_len);

bool init_cli_wifi(cli_line_cb_t on_line);

void wifi_write(const char *s);
bool wifi_status(char *buf);
