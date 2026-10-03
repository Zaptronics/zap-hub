#include "ztmb/log.h"
#include "zaptronics/platform/pico/log.h"
#include "pico/stdlib.h"
static zap_pico_log_t logger;
static void write_uart(void *context, const char *line)
{
    (void)context;
    while (*line) stdio_putchar_raw((int)*line++);
}
void log_init(void) { zap_pico_log_init(&logger,128u,write_uart,NULL); }
unsigned log_dropped_count(void) { return zap_pico_log_dropped(&logger); }
void log_vprintf(const char *fmt, va_list ap) { (void)zap_log_vprintf(&logger.log,fmt,ap); }
void log_printf(const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt); log_vprintf(fmt,ap); va_end(ap);
}
void log_poll(void) { if (zap_pico_log_poll(&logger)) sleep_ms(10); }
