#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "lwip/tcp.h"

#include "http_server.h"
#include "web_assets.h"


#ifndef HTTP_CHUNK_SIZE
#define HTTP_CHUNK_SIZE 1460u  // good default (≈ TCP_MSS)
#endif

static cli_line_cb_t g_on_line = NULL;

// Per-connection state
typedef struct {
    struct tcp_pcb *pcb;
    char req[512];
    uint16_t req_len;
    bool done;
} http_conn_t;



static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

// Decodes in-place. Converts '+' to space.
// Returns true on success, false on malformed % encoding.
static bool url_decode_inplace(char *s) {
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%') {
            if (r[1] == 0 || r[2] == 0) return false;
            int hi = hexval(r[1]);
            int lo = hexval(r[2]);
            if (hi < 0 || lo < 0) return false;
            *w++ = (char)((hi << 4) | lo);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
    return true;
}

// Very small JSON string escaper (enough for CLI output and command echo).
// Returns number of bytes written (excluding terminating 0), always 0-terminated.
static size_t json_escape(char *dst, size_t dst_len, const char *src) {
    size_t w = 0;
    if (dst_len == 0) return 0;

    for (const unsigned char *p = (const unsigned char*)src; *p; p++) {
        const char *rep = NULL;
        char tmp[8];

        switch (*p) {
            case '\"': rep = "\\\""; break;
            case '\\': rep = "\\\\"; break;
            case '\b': rep = "\\b";  break;
            case '\f': rep = "\\f";  break;
            case '\n': rep = "\\n";  break;
            case '\r': rep = "\\r";  break;
            case '\t': rep = "\\t";  break;
            default:
                if (*p < 0x20) {
                    // control char -> \u00XX
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)*p);
                    rep = tmp;
                }
                break;
        }

        if (rep) {
            size_t rl = strlen(rep);
            if (w + rl >= dst_len) break;
            memcpy(dst + w, rep, rl);
            w += rl;
        } else {
            if (w + 1 >= dst_len) break;
            dst[w++] = (char)*p;
        }
    }

    dst[w] = 0;
    return w;
}


static void http_conn_free_no_close(http_conn_t *hs) {
    if (!hs) return;
    hs->pcb = NULL;   // important: prevent accidental close paths
    free(hs);
}

static err_t http_conn_close(http_conn_t *hs) {
    if (!hs || !hs->pcb) return ERR_OK;
    tcp_arg(hs->pcb, NULL);
    tcp_sent(hs->pcb, NULL);
    tcp_recv(hs->pcb, NULL);
    tcp_err(hs->pcb, NULL);
    err_t e = tcp_close(hs->pcb);
    hs->pcb = NULL;
    free(hs);
    return e;
}

typedef struct {
    struct tcp_pcb *pcb;

    // header is copied into this struct so it survives after send_response returns
    char hdr[256];
    uint16_t hdr_len;
    uint16_t hdr_sent;

    const uint8_t *body;
    uint8_t *owned_body;
    size_t body_len;
    size_t body_sent;

    bool close_when_done;
} http_tx_t;

static void http_tx_free(http_tx_t *tx) {
    if (!tx) return;
    free(tx->owned_body);
    free(tx);
}

static err_t http_tx_close(http_tx_t *tx) {
    if (!tx || !tx->pcb) { http_tx_free(tx); return ERR_OK; }

    struct tcp_pcb *pcb = tx->pcb;

    tcp_arg(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);

    tx->pcb = NULL;
    http_tx_free(tx);

    return tcp_close(pcb);
}

// Try to push as much queued data as lwIP will accept right now.
static err_t http_tx_pump(http_tx_t *tx) {
    if (!tx || !tx->pcb) return ERR_OK;

    struct tcp_pcb *pcb = tx->pcb;
    err_t err;

    // 1) Send remaining header bytes
    while (tx->hdr_sent < tx->hdr_len) {
        u16_t space = tcp_sndbuf(pcb);
        if (space == 0) break;

        u16_t remaining = (u16_t)(tx->hdr_len - tx->hdr_sent);
        u16_t to_send = remaining;
        if (to_send > space) to_send = space;

        err = tcp_write(pcb, tx->hdr + tx->hdr_sent, to_send, TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) break;          // no room right now; wait for tcp_sent
        if (err != ERR_OK) return err;      // fatal
        tx->hdr_sent += to_send;
    }

    // 2) Send body in chunks
    while (tx->hdr_sent >= tx->hdr_len && tx->body_sent < tx->body_len) {
        u16_t space = tcp_sndbuf(pcb);
        if (space == 0) break;

        size_t remaining = tx->body_len - tx->body_sent;
        size_t want = remaining;
        if (want > HTTP_CHUNK_SIZE) want = HTTP_CHUNK_SIZE;
        if (want > space) want = space;

        err = tcp_write(pcb, tx->body + tx->body_sent, (u16_t)want, TCP_WRITE_FLAG_COPY);
        if (err == ERR_MEM) break;
        if (err != ERR_OK) return err;
        tx->body_sent += want;
    }

    tcp_output(pcb);

    // 3) Done?
    if (tx->hdr_sent >= tx->hdr_len && tx->body_sent >= tx->body_len) {
        if (tx->close_when_done) {
            return http_tx_close(tx);
        }
    }

    return ERR_OK;
}

// lwIP calls this when some data has been ACKed (space freed in send buffer)
static err_t http_tx_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    (void)tpcb; (void)len;
    http_tx_t *tx = (http_tx_t*)arg;
    err_t e = http_tx_pump(tx);
    // If pump closes the pcb, tx is freed and pcb callbacks removed.
    return e;
}

// lwIP calls this on connection abort/reset, etc.
static void http_tx_err(void *arg, err_t err) {
    (void)err;
    http_tx_t *tx = (http_tx_t*)arg;
    if (tx) {
        tx->pcb = NULL;
        http_tx_free(tx);
    }
}

// Replaces your previous send_response.
// IMPORTANT: This function takes ownership of tpcb callbacks (sent/err/arg).
static void send_response_ex(struct tcp_pcb *tpcb,
                             const char *status,
                             const char *ctype,
                             const uint8_t *body,
                             size_t body_len,
                             bool copy_body)
{
    if (!tpcb) return;

    http_tx_t *tx = (http_tx_t*)calloc(1, sizeof(http_tx_t));
    if (!tx) {
        // Best-effort: can't allocate state, just close.
        tcp_abort(tpcb);
        return;
    }

    tx->pcb = tpcb;
    tx->body_len = body ? body_len : 0;
    tx->close_when_done = true;

    if (copy_body && tx->body_len > 0) {
        tx->owned_body = (uint8_t *)malloc(tx->body_len);
        if (!tx->owned_body) {
            tx->pcb = NULL;
            http_tx_free(tx);
            tcp_abort(tpcb);
            return;
        }
        memcpy(tx->owned_body, body, tx->body_len);
        tx->body = tx->owned_body;
    } else {
        tx->body = body;
    }

    int n = snprintf(tx->hdr, sizeof(tx->hdr),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n",
        status, ctype, (unsigned)tx->body_len);

    if (n < 0 || (size_t)n >= sizeof(tx->hdr)) {
        tx->pcb = NULL;
        http_tx_free(tx);
        tcp_abort(tpcb);
        return;
    }
    tx->hdr_len = (uint16_t)n;
    tx->hdr_sent = 0;

    // Install callbacks for streaming
    tcp_arg(tpcb, tx);
    tcp_sent(tpcb, http_tx_sent);
    tcp_err(tpcb, http_tx_err);

    // Kick off initial send immediately
    err_t e = http_tx_pump(tx);
    if (e != ERR_OK) {
        // If error occurred, close/cleanup
        http_tx_close(tx);
    }
}

/* Static assets can stay zero-copy; transient stack buffers must be copied. */
static void send_response(struct tcp_pcb *tpcb,
                          const char *status,
                          const char *ctype,
                          const uint8_t *body,
                          size_t body_len)
{
    send_response_ex(tpcb, status, ctype, body, body_len, false);
}

static void send_response_copy(struct tcp_pcb *tpcb,
                               const char *status,
                               const char *ctype,
                               const uint8_t *body,
                               size_t body_len)
{
    send_response_ex(tpcb, status, ctype, body, body_len, true);
}

static size_t formatted_length(int result, size_t capacity)
{
    if (result <= 0 || capacity == 0) return 0;
    size_t length = (size_t)result;
    return length < capacity ? length : capacity - 1u;
}

// Builds a standard JSON error
static void send_json_error(struct tcp_pcb *tpcb, int http_status,
                            const char *code, const char *message)
{
    char msg_esc[160];
    json_escape(msg_esc, sizeof(msg_esc), message);

    char body[256];
    int n = snprintf(body, sizeof(body),
        "{\"ok\":false,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}\n",
        code, msg_esc);

    const char *status =
        (http_status == 400) ? "400 Bad Request" :
        (http_status == 404) ? "404 Not Found" :
        (http_status == 405) ? "405 Method Not Allowed" :
        "500 Internal Server Error";

    send_response_copy(tpcb, status, "application/json; charset=utf-8",
                       (const uint8_t*)body, formatted_length(n, sizeof(body)));
}


static void send_text(struct tcp_pcb *tpcb, const char *status, const char *ctype, const char *txt) {
    send_response_copy(tpcb, status, ctype, (const uint8_t*)txt, strlen(txt));
}


static void handle_api(struct tcp_pcb *tpcb, const char *path) {
    // path like "/api/pump/start/1"
    // Minimal parse without allocations
    const char *p = path;
    if (strncmp(p, "/api/", 5) != 0) {
        send_text(tpcb, "404 Not Found", "application/json", "{\"ok\":false}\n");
        return;
    }
    p += 5;

    // Copy tail to a mutable buffer
    char tmp[256];
    strncpy(tmp, path + 5, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;

    // Strip off any query string (ignore ?x=y)
    char *q = strchr(tmp, '?');
    if (q) *q = 0;

    // Convert slashes to spaces
    for (char *p = tmp; *p; p++) {
        if (*p == '/') *p = ' ';
    }

    // URL decode (%xx, +)
    if (!url_decode_inplace(tmp)) {
        send_json_error(tpcb, 400, "bad_encoding", "Malformed URL encoding");
        return;
    }

    // Trim leading spaces
    char *cmd = tmp;
    while (*cmd == ' ') cmd++;

    if (*cmd == 0) {
        send_json_error(tpcb, 400, "empty_cmd", "No command after /api/");
        return;
    }

    // Collapse multiple spaces to single spaces (nice to have)
    {
        char *r = cmd, *w = cmd;
        bool prev_space = false;
        while (*r) {
            bool is_space = (*r == ' ');
            if (is_space) {
                if (!prev_space) *w++ = ' ';
            } else {
                *w++ = *r;
            }
            prev_space = is_space;
            r++;
        }
        // Trim trailing space
        while (w > cmd && w[-1] == ' ') w--;
        *w = 0;
    }

    // Execute CLI into a response buffer
    char cli_out[256];     // increase if you want bigger replies
    cli_out[0] = 0;

    int rc = g_on_line(cmd, cli_out, sizeof(cli_out));

    // Build JSON response
    char cmd_esc[256];
    char out_esc[256];
    json_escape(cmd_esc, sizeof(cmd_esc), cmd);
    json_escape(out_esc, sizeof(out_esc), cli_out);

    char body[768];
    int n = snprintf(body, sizeof(body),
        "{\"ok\":%s,\"cmd\":\"%s\",\"rc\":%d,\"result\":\"%s\"}\n",
        (rc == 0) ? "true" : "false",
        cmd_esc,
        rc,
        out_esc
    );

    // If your CLI returns error codes, you can choose 200 always, or 400 on rc!=0.
    // I usually keep it 200 and rely on ok/rc fields (simpler for UI).
    send_response_copy(tpcb, "200 OK", "application/json; charset=utf-8",
                       (const uint8_t*)body, formatted_length(n, sizeof(body)));
}

static void route_request(struct tcp_pcb *tpcb, const char *method, const char *path) {
    if (strcmp(method, "GET") != 0) {
        send_text(tpcb, "405 Method Not Allowed", "text/plain", "GET only\n");
        return;
    }

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        send_response(tpcb, "200 OK", "text/html; charset=utf-8", INDEX_HTML, INDEX_HTML_LEN);
        return;
    }
    if (strcmp(path, "/app.js") == 0) {
        send_response(tpcb, "200 OK", "application/javascript; charset=utf-8", APP_JS, APP_JS_LEN);
        return;
    }
    if (strncmp(path, "/api/", 5) == 0) {
        handle_api(tpcb, path);
        return;
    }

    send_text(tpcb, "404 Not Found", "text/plain", "not found\n");
}

static err_t http_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    http_conn_t *hs = (http_conn_t*)arg;

    if (!p) {
        // client closed before we responded
        http_conn_close(hs);
        return ERR_OK;
    }
    if (err != ERR_OK) {
        pbuf_free(p);
        http_conn_close(hs);
        return err;
    }

    // Copy into request buffer (only first ~512 bytes)
    for (struct pbuf *q = p; q && hs->req_len < sizeof(hs->req) - 1; q = q->next) {
        uint16_t can = (uint16_t)q->len;
        if (hs->req_len + can > sizeof(hs->req) - 1) {
            can = (uint16_t)((sizeof(hs->req) - 1) - hs->req_len);
        }
        memcpy(&hs->req[hs->req_len], q->payload, can);
        hs->req_len += can;
    }
    hs->req[hs->req_len] = 0;

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    // Refuse oversized headers rather than acknowledging data forever once the
    // fixed request buffer is full.
    if (!hs->done && hs->req_len >= sizeof(hs->req) - 1u &&
        !strstr(hs->req, "\r\n\r\n")) {
        hs->done = true;
        tcp_recv(tpcb, NULL);
        send_text(tpcb, "431 Request Header Fields Too Large", "text/plain",
                  "request headers too large\n");
        http_conn_free_no_close(hs);
        return ERR_OK;
    }

    // If we have the end of headers, process once
    if (!hs->done && strstr(hs->req, "\r\n\r\n")) {
        hs->done = true;

        char method[8] = {0};
        char path[256] = {0};

        // We are done receiving; detach recv so we don't get called again
        tcp_recv(tpcb, NULL);

        if (sscanf(hs->req, "%7s %255s", method, path) == 2) {
            // route_request() must call send_response()/send_text(),
            // which installs the TX callbacks and will close when finished.
            route_request(tpcb, method, path);
        } else {
            // make sure send_text ultimately uses send_response (chunked)
            send_text(tpcb, "400 Bad Request", "text/plain", "bad request\n");
        }

        // IMPORTANT: do NOT close pcb here — TX streamer will close it.
        http_conn_free_no_close(hs);
        return ERR_OK;
    }

    return ERR_OK;
}

static void http_err(void *arg, err_t err) {
    (void)err;
    http_conn_t *hs = (http_conn_t*)arg;
    if (hs) {
        hs->pcb = NULL;
        free(hs);
    }
}

static err_t http_accept(void *arg, struct tcp_pcb *newpcb, err_t err) {
    (void)arg;
    if (err != ERR_OK || !newpcb) return ERR_VAL;

    http_conn_t *hs = (http_conn_t*)calloc(1, sizeof(http_conn_t));
    if (!hs) return ERR_MEM;

    hs->pcb = newpcb;
    tcp_arg(newpcb, hs);
    tcp_recv(newpcb, http_recv);
    tcp_err(newpcb, http_err);

    // Optional: small recv timeout-ish behavior is manual; keep simple for now.
    return ERR_OK;
}

static struct tcp_pcb *g_listen_pcb;

bool http_server_start(cli_line_cb_t on_line)
{
    if (!on_line) return false;

    web_assets_init();
    g_on_line = on_line;
    g_listen_pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!g_listen_pcb) return false;

    if (tcp_bind(g_listen_pcb, IP_ANY_TYPE, 80) != ERR_OK) {
        tcp_abort(g_listen_pcb);
        g_listen_pcb = NULL;
        return false;
    }

    struct tcp_pcb *listen_pcb = tcp_listen_with_backlog(g_listen_pcb, 2);
    if (!listen_pcb) {
        tcp_abort(g_listen_pcb);
        g_listen_pcb = NULL;
        return false;
    }
    g_listen_pcb = listen_pcb;
    tcp_accept(g_listen_pcb, http_accept);
    return true;
}
