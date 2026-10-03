#include "ztmb/cli_wifi.h"
#include "ztmb/cli_cmds.h"
#include "ztmb/log.h"
#include "ztmb/web_assets.h"
#include "firmware_version.h"
#include "wifi_config.h"
static const char banner[] =
    "\r\nCirrus Cloud Chamber Hub - Firmware " HUB_FIRMWARE_VERSION "\r\n"
    "Type 'help' for commands; type 'safe' to disable outputs.\r\n"
    "PuTTY: Local echo = Force off; Local line editing = Force off.\r\n";
static bool network_log(void *context, const char *line)
{ (void)context; log_printf("%s",line); return true; }
static const zap_log_t logger = { .submit=network_log };
bool init_cli_wifi(cli_line_cb_t on_line)
{
    web_assets_init();
    const zap_pico_network_config_t config = {
        .ssid=WIFI_SSID, .password=WIFI_PASSWORD,
        .banner=banner, .prompt="\r\ncirrus1> ", .cli_port=CLI_PORT, .http_port=80,
        .command=on_line, .log=&logger,
        .key=cli_run_key, .screen_tick=cli_run_tick, .screen_closed=cli_run_closed,
        .assets={.index_html=INDEX_HTML, .index_html_len=INDEX_HTML_LEN,
                 .app_js=APP_JS, .app_js_len=APP_JS_LEN}
    };
    /* Preserve the legacy wrapper's false=success convention. */
    return zap_pico_network_start(&config) != 0;
}
void wifi_write(const char *s) { zap_pico_network_write(s); }
bool wifi_status(char *buf) { return zap_pico_network_status(buf, 256u); }
