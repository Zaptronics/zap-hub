#pragma once
// notes
#include <stdarg.h>
#include <stddef.h>

void log_init(void);
void log_printf(const char *fmt, ...);
void log_vprintf(const char *fmt, va_list ap);
void log_poll(void);
unsigned log_dropped_count(void);
