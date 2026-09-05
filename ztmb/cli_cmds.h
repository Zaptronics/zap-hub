// cli_cmds.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

#include "pico/cyw43_arch.h"
#include "ztmb/cli_wifi.h"

#define CLI_MAX_ARGS   16

typedef int (*cli_cmd_fn_t)(int argc, char **argv, char *out, size_t out_len);

typedef struct {
    const char   *name;
    const char   *help;
    cli_cmd_fn_t  fn;
} cli_cmd_t;

typedef struct {
    const char    *name;      // group name, e.g. "pump"
    const char    *help;      // group help
    const cli_cmd_t *cmds;    // subcommands
    size_t         cmd_count;
} cli_group_t;

/* ---- Argument parsing ---- */
static bool parse_u32(const char *s, uint32_t *out) {
    if (!s || !*s || !out || *s == '-') return false;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 0); /* decimal or 0x-prefixed hex */
    if (errno != 0 || !end || *end != '\0' || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static bool parse_float(const char *s, float *out) {
    if (!s || !*s || !out) return false;
    errno = 0;
    char *end = NULL;
    float v = strtof(s, &end);
    if (errno != 0 || !end || *end != '\0' || !isfinite(v)) return false;
    *out = v;
    return true;
}



void cli_init(void);
//void cmd_help(void);
//void cmd_status(void);
int handle_command(char *cmd, char *out, size_t out_len);
int cli_tokenize_simple(char *line, char **argv, int argv_max);
const cli_cmd_t *find_cmd(const cli_cmd_t *cmds, size_t n, const char *name);
const cli_group_t *find_group(const cli_group_t *groups, size_t n, const char *name);
