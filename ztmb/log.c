#include "ztmb/log.h"

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"

#ifndef LOG_LINE_MAX
#define LOG_LINE_MAX 512
#endif

#ifndef LOG_QUEUE_DEPTH
#define LOG_QUEUE_DEPTH 128
#endif

typedef struct {
    char s[LOG_LINE_MAX];
} log_line_t;

static queue_t g_log_q;
static volatile uint32_t g_log_dropped = 0;

void log_init(void) {
    queue_init(&g_log_q, sizeof(log_line_t), LOG_QUEUE_DEPTH);
    g_log_dropped = 0;
}

unsigned log_dropped_count(void) {
    return (unsigned)g_log_dropped;
}

static inline void log_drop(void) {
    __atomic_fetch_add(&g_log_dropped, 1, __ATOMIC_RELAXED);
}

void log_vprintf(const char *fmt, va_list ap) {
    if (!fmt) return;

    log_line_t line;
    line.s[0] = '\0';

    // IMPORTANT: leave room for '\n' and '\0'
    va_list ap2;
    va_copy(ap2, ap);
    vsnprintf(line.s, sizeof(line.s) - 2, fmt, ap2);
    va_end(ap2);

    // Ensure newline termination
    size_t L = strnlen(line.s, sizeof(line.s));
    if (L == 0 || line.s[L - 1] != '\n') {
        line.s[L] = '\n';
        line.s[L + 1] = '\0';
    }

    // Non-blocking enqueue; drop if full
    if (!queue_try_add(&g_log_q, &line)) {
        log_drop();
    }
}

void log_printf(const char *fmt, ...) {

    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static inline void log_write_blocking(const char *s) {
    while (s && *s) {
        stdio_putchar_raw((int)*s++);
    }
}

void log_poll(void) {
    log_line_t line;
    if(!queue_try_remove(&g_log_q, &line)) return;
    log_write_blocking(line.s);
    sleep_ms(10);

}