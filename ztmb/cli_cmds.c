#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "pico/mutex.h"
#include "pico/stdlib.h"

#include "ztmb/cli_cmds.h"
#include "hardware/watchdog.h"

#include "cmds.h"


static mutex_t g_cli_mutex;



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


