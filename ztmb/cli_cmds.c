#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "pico/mutex.h"
#include "pico/stdlib.h"

#include "ztmb/cli_cmds.h"
#include "hardware/watchdog.h"

#include "cmds.h"


static mutex_t g_cli_mutex;



int cli_tokenize_simple(char *line, char **argv, int argv_max) {
    int argc = 0;
    char *p = line;

    while (*p) {
        // skip whitespace
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (argc >= argv_max) break;

        argv[argc++] = p;

        // advance to end of token
        while (*p && !isspace((unsigned char)*p)) p++;

        // null-terminate token
        if (*p) *p++ = '\0';
    }
    return argc;
}

/* ---- Helpers ---- */
const cli_cmd_t *find_cmd(const cli_cmd_t *cmds, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(cmds[i].name, name) == 0) return &cmds[i];
    }
    return NULL;
}

const cli_group_t *find_group(const cli_group_t *groups, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++) {
        if (strcmp(groups[i].name, name) == 0) return &groups[i];
    }
    return NULL;
}





void cli_init(void) {
    mutex_init(&g_cli_mutex);
    init_cli_wifi(handle_command);
    
}







int handle_command(char *cmd, char *out, size_t out_len) {
    
    absolute_time_t deadline = make_timeout_time_ms(1000);
    while (!mutex_try_enter(&g_cli_mutex, NULL)) {
        if (absolute_time_diff_us(get_absolute_time(), deadline) <= 0) {
            snprintf(out, out_len, "busy");
            return 1; // busy
        }
        tight_loop_contents();
    }

    int rc = handle_command_internal(cmd, out, out_len);

    mutex_exit(&g_cli_mutex);
    return rc;
}


