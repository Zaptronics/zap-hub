#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"

#include "lwip/tcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"

#include "cli_wifi.h"
#include "http_server.h"
#include "ztmb/log.h"


static cli_line_cb_t g_on_line = NULL;
static char ip_addr[16];

// Forward declare the lwIP netif created by cyw43 (provided by pico_cyw43_arch)
static struct netif *get_netif(void) {
    // lwIP sets this to the active interface (your Wi-Fi STA)
    return netif_default;
}


// ======== Simple session state ========
typedef struct {
    struct tcp_pcb *pcb;
    char line[RX_LINE_MAX];
    size_t line_len;
} cli_session_t;

static cli_session_t g_sess = {0};

// ======== Helpers ========
void wifi_write(const char *s) {
    if (!g_sess.pcb) return;
    // tcp_write is buffered; use TCP_WRITE_FLAG_COPY to copy from our buffer.
    tcp_write(g_sess.pcb, s, (u16_t)strlen(s), TCP_WRITE_FLAG_COPY);
    tcp_output(g_sess.pcb);
}

static void cli_prompt(void) {
    wifi_write("\r\ncirrus1> ");
}

static void cli_banner(void) {
    wifi_write("\r\nCirrus CLI over TCP\r\nType 'help'\r\n");
    cli_prompt();
}

bool wifi_status(char *buf) {
    const ip4_addr_t *ip = netif_ip4_addr(get_netif());
    const ip4_addr_t *gw = netif_ip4_addr(netif_default);
    const ip4_addr_t *mask = netif_ip4_addr(netif_default);

    snprintf(buf, 256,
             "WiFi SSID: %s\r\n"
             "IP Address: %s\r\n"
             "Gateway: %s\r\n"
             "Netmask: %s\r\n",
             WIFI_SSID,
             ip4addr_ntoa(ip),
             ip4addr_ntoa(gw),
             ip4addr_ntoa(mask));

    return true;
}

// ======== TCP callbacks ========
static err_t on_cli_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    (void)arg;
    if (!p) {
        // Connection closed by remote
        g_sess.pcb = NULL;
        g_sess.line_len = 0;
        tcp_close(tpcb);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    tcp_recved(tpcb, p->tot_len);

    // Read bytes into our line buffer
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        const char *data = (const char *)q->payload;
        for (u16_t i = 0; i < q->len; i++) {
            char c = data[i];

            // Handle CR/LF line endings
            if (c == '\r') continue;
            if (c == '\n') {
                // terminate and execute
                g_sess.line[g_sess.line_len] = 0;
                
                // Execute CLI into a response buffer
                char cli_out[256];     // increase if you want bigger replies
                cli_out[0] = 0;
                int rc = g_on_line(g_sess.line, cli_out, sizeof(cli_out));

                g_sess.line_len = 0;
                cli_prompt();
                continue;
            }

            // Basic backspace
            if (c == 0x08 || c == 0x7f) {
                if (g_sess.line_len > 0) g_sess.line_len--;
                continue;
            }

            if (g_sess.line_len < (RX_LINE_MAX - 1)) {
                g_sess.line[g_sess.line_len++] = c;
            }
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t on_cli_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    // Only allow one session at a time
    if (g_sess.pcb) {
        tcp_abort(newpcb);
        return ERR_ABRT;
    }

    g_sess.pcb = newpcb;
    g_sess.line_len = 0;

    tcp_recv(newpcb, on_cli_recv);
    tcp_nagle_disable(newpcb); // snappier CLI

    cli_banner();
    return ERR_OK;
}

static struct tcp_pcb *start_cli_server(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) return NULL;

    if (tcp_bind(pcb, IP_ANY_TYPE, CLI_PORT) != ERR_OK) {
        tcp_close(pcb);
        return NULL;
    }

    pcb = tcp_listen_with_backlog(pcb, 1);
    tcp_accept(pcb, on_cli_accept);
    return pcb;
}

// ======== WiFi connect ========
static bool wifi_connect_blocking(void) {
    // STA mode
    cyw43_arch_enable_sta_mode();

    log_printf("Connecting to WiFi SSID '%s'...\n", WIFI_SSID);

    // WPA2 PSK. For open networks pass NULL password and CYW43_AUTH_OPEN.
    int err = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID,
        WIFI_PASSWORD,
        CYW43_AUTH_WPA2_AES_PSK,
        30000
    );

    if (err) {
        log_printf("WiFi connect failed: %d\n", err);
        return false;
    }

    const ip4_addr_t *ip = netif_ip4_addr(get_netif());
    log_printf("WiFi connected. IP = %s\n", ip4addr_ntoa(ip));
    ip4addr_ntoa_r(ip, ip_addr, sizeof(ip_addr));
    return true;
}

bool init_cli_wifi(cli_line_cb_t on_line) {

    g_on_line = on_line;


    sleep_ms(1500);
    log_printf("Pico 2W WiFi CLI starting...\n");

    if (cyw43_arch_init()) {
        log_printf("cyw43_arch_init() failed\n");
        return 1;
    }

    bool ok = wifi_connect_blocking();
    if (!ok) {
        log_printf("Retrying in 5s...\n");
        sleep_ms(5000);
        ok = wifi_connect_blocking();
        if (!ok) {
            log_printf("Giving up.\n");
            cyw43_arch_deinit();
            return 1;
        }
    }

    struct tcp_pcb *server = start_cli_server();
    if (!server) {
        log_printf("Failed to start TCP CLI server\n");
        cyw43_arch_deinit();
        return 1;
    }

    //log_printf("TCP CLI listening on port %d\n", CLI_PORT);
    log_printf("Connect from your PC:  nc %s %d\n", ip_addr, CLI_PORT);

    
    if (!http_server_start(on_line)) {
        log_printf("HTTP server failed to start\n");
    } else {
        log_printf("HTTP server started on port 80\n");
    }


    // unreachable
    // tcp_close(server);
    // cyw43_arch_deinit();
    return 0;
}
