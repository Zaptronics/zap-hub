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

#include "zaptronics/services/cli.h"
#define cli_tokenize_simple zap_cli_tokenize
#define find_cmd zap_cli_find_cmd
#define find_group zap_cli_find_group
#define parse_u32 zap_cli_parse_u32
#define parse_float zap_cli_parse_float

void cli_init(void);
void cli_run_key(uint8_t byte);
void cli_run_tick(void);
void cli_run_closed(void);
//void cmd_help(void);
//void cmd_status(void);
int handle_command(char *cmd, char *out, size_t out_len);
int cli_tokenize_simple(char *line, char **argv, int argv_max);
const cli_cmd_t *find_cmd(const cli_cmd_t *cmds, size_t n, const char *name);
const cli_group_t *find_group(const cli_group_t *groups, size_t n, const char *name);
