/* Included only by cmds.h after cmd_safe. Recipes await operating targets. */
#include "cirrus1_config.h"
#pragma once

static bool run_visible;
static bool run_escape_pending;
static uint32_t run_escape_at;
static unsigned run_active;
static const char *run_notice;

/* Fixed storage avoids large stack frames and heap allocation. Text is clipped
 * at 99 columns so a 100-column terminal cannot wrap a row and scroll. */
#define RUN_SCREEN_ROWS 48u
#define RUN_SCREEN_COLUMNS 100u
#define RUN_UPDATE_CAPACITY (RUN_SCREEN_ROWS * (RUN_SCREEN_COLUMNS + 16u) + 16u)
static char run_previous[RUN_SCREEN_ROWS][RUN_SCREEN_COLUMNS];
static char run_frame[RUN_SCREEN_ROWS][RUN_SCREEN_COLUMNS];
static char run_update[RUN_UPDATE_CAPACITY];
static unsigned run_row;
static unsigned run_column;
static bool run_screen_valid;
static uint32_t run_full_redraw_at;
#define RUN_FULL_REDRAW_MS 10000u

static void run_frame_begin(void)
{
    memset(run_frame, 0, sizeof(run_frame));
    run_row = 0;
    run_column = 0;
}

static void run_frame_write(const char *text)
{
    while (*text && run_row < RUN_SCREEN_ROWS) {
        char c = *text++;
        if (c == '\r') continue;
        if (c == '\n') {
            ++run_row;
            run_column = 0;
        } else if (run_column < RUN_SCREEN_COLUMNS - 1u) {
            run_frame[run_row][run_column++] = c;
        }
    }
}

static void run_frame_printf(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    run_frame_write(text);
}

static void run_frame_send(void)
{
    size_t used = 0;
    bool full_redraw = !run_screen_valid;
    /* Clear on entry and periodic recovery. Other frames update rows in place,
     * then erase just their trailing text (including shortened status messages). */
    if (!run_screen_valid) {
        const char clear[] = "\033[H\033[2J";
        memcpy(run_update, clear, sizeof(clear) - 1u);
        used = sizeof(clear) - 1u;
    }
    for (unsigned row = 0; row < RUN_SCREEN_ROWS; ++row) {
        if (run_screen_valid && strcmp(run_frame[row], run_previous[row]) == 0)
            continue;
        if (!run_screen_valid && !run_frame[row][0]) continue;
        int count = snprintf(run_update + used, sizeof(run_update) - used,
                             "\033[%u;1H%s\033[K", row + 1u, run_frame[row]);
        if (count < 0 || (size_t)count >= sizeof(run_update) - used) {
            /* No partial update was sent. Recover with a complete frame later. */
            run_screen_valid = false;
            return;
        }
        used += (size_t)count;
    }
    if (used) {
        run_update[used] = '\0';
        /* Below the network service's 16 KiB queue limit; caller has checked
         * output-idle. One write per refresh, no writes for unchanged frames. */
        wifi_write(run_update);
    }
    memcpy(run_previous, run_frame, sizeof(run_previous));
    run_screen_valid = true;
    if (full_redraw) run_full_redraw_at = to_ms_since_boot(get_absolute_time());
}

static void run_draw(void)
{
    if (!run_visible || !zap_pico_network_output_idle()) return;
    /* Check after output-idle: a congested connection defers recovery rather
     * than queuing repeated clears. Unsigned subtraction handles timer wrap. */
    if (run_screen_valid &&
        (uint32_t)(to_ms_since_boot(get_absolute_time()) - run_full_redraw_at) >= RUN_FULL_REDRAW_MS)
        run_screen_valid = false;
    run_frame_begin();
    static const char *const modes[] = {
        "Tests - ZapLink board checks [not connected]",
        "Standby - filter / low fans / low lights / glass heat [unconfigured]",
        "Prime - IPA loop [unconfigured]",
        "Runup - wick / low TEC / monitoring / CHILLER CONFIRM [unconfigured]",
        "Go - lights / glass / TEC / wick / sump [locked]",
        "Shutdown - ramp TEC/glass down; retain lights [unconfigured]",
        "STOP - request safe off on all systems"
    };
    run_frame_write("Cirrus chamber - RUN console\r\n");
    run_frame_write("\r\n");
    for (unsigned i = 0; i < 7; ++i)
        run_frame_printf("%s %u. %s\r\n", run_active == i+1 ? "->" : "  ", i+1, modes[i]);
    run_frame_printf("\r\n%s\r\n", run_notice);
    run_frame_write("\r\nModule initialization (not a live board test):\r\n");
    for (size_t i = 0; i < modules_count(); ++i) {
        const module_entry_t *m = modules_get(i);
        if (m) run_frame_printf("  %-14s %s\r\n", m->short_name ? m->short_name : "?",
                              modules_is_initialized(i) ? "initialized" : "FAILED");
    }
    pumping_status_t p;
    pumping_get_status(&p);
    run_frame_printf("\r\nPumps (commanded): filter %.0f, wick %.0f, sump %.0f steps/s\r\n",
                   (double)p.filter_speed, (double)p.wick_speed, (double)p.sump_speed);
    run_frame_printf("Wick remaining: %lu ms | PWM POWER_SAFE rc: %d\r\n",
                   (unsigned long)p.wick_remaining_ms, ztm_pwm_1.power_safe_rc);
    run_frame_printf("Wick: %s | motors: filter %u / wick %u / return %u\r\n",
                   p.wick_state == PUMP_WICK_WET ? "continuous" : "timed/off",
                   CIRRUS1_FILTER_MOTOR, CIRRUS1_WICK_MOTOR, CIRRUS1_RETURN_MOTOR);
    run_frame_printf("Loads: TEC fans L%u / LEDs L%u,L%u / glass L%u\r\n",
                   CIRRUS1_TEC_FANS_LOAD, CIRRUS1_LED_LEFT_LOAD,
                   CIRRUS1_LED_RIGHT_LOAD, CIRRUS1_GLASS_HEATER_LOAD);
    run_frame_printf("Fans: exhaust %u / intake %u (equal RPM targets required)\r\n",
                   CIRRUS1_EXHAUST_FAN, CIRRUS1_INTAKE_FAN);
    run_frame_write("PWM commanded %: ");
    for (uint8_t i = 1; i <= ZTM_PWM_CHANNEL_COUNT; ++i)
        run_frame_printf("%u:%u ", (unsigned)i, (unsigned)ztm_pwm_channel_get_duty(&ztm_pwm_1, i));
    run_frame_write("\r\nFan commanded %: ");
    for (uint8_t i = 1; i <= ZTM_PWM_FAN_COUNT; ++i)
        run_frame_printf("%u:%u ", (unsigned)i, (unsigned)ztm_pwm_fan_get_duty(&ztm_pwm_1, i));
    run_frame_printf("\r\nTEC operator permission: %s\r\n",
                   cooling_tec_permission_granted() ? "GRANTED (operator-confirmed coolant)" : "LOCKED");
    run_frame_write("\r\nTemperatures / currents: acquisition not connected\r\n"
               "TEC targets / glass heat / low lights / fan RPM: not configured\r\n"
               "Go locked: Runup sequence is not implemented\r\n\r\n"
               "1-7: select | ESC: leave outputs unchanged | Any other key: STOP\r\n"
               "Client: local echo OFF, local line editing OFF. Do not press Enter.\r\n");
    run_frame_send();
}

static void run_stop(void)
{
    run_escape_pending = false;
    int rc = cmd_safe(0, NULL, NULL, 0);
    run_active = 7;
    run_notice = rc == 0 ? "STOP: off commands accepted; physical state not verified."
                        : "STOP FAILED: one or more outputs could not be confirmed off.";
}

void cli_run_key(uint8_t byte)
{
    if (!run_visible) return;
    /* Delay bare ESC so arrow/function sequences request STOP, not CLI input. */
    if (run_escape_pending) {
        run_stop();
    } else if (byte == 27u) {
        run_escape_pending = true;
        run_escape_at = to_ms_since_boot(get_absolute_time());
        return;
    } else if (byte == '7' || byte < '1' || byte > '7') {
        run_stop();
    } else {
        run_notice = byte == '1'
            ? "Tests unavailable: ZapLink checks not connected; no all-systems-go claim."
            : byte == '5'
            ? "Go BLOCKED: completed Runup and explicit chiller confirmation required."
            : "Mode BLOCKED: operating targets and sequence timing required.";
    }
    run_draw();
}

void cli_run_tick(void)
{
    if (!run_visible) return;
    if (run_escape_pending &&
        (uint32_t)(to_ms_since_boot(get_absolute_time()) - run_escape_at) >= 200u) {
        run_escape_pending = false;
        run_visible = false;
        console_screen_active = false;
        run_screen_valid = false;
        (void)zap_pico_network_set_raw(false);
        wifi_write("\033[?25h\033[?1049l\r\ncirrus1> ");
        return;
    }
    run_draw();
}

void cli_run_closed(void)
{
    run_visible = false;
    console_screen_active = false;
    run_screen_valid = false;
    run_escape_pending = false;
}

static int cmd_run(int argc, char **argv, char *out, size_t out_len)
{
    (void)argv; (void)out; (void)out_len;
    if (argc != 1) {
        console_printf("\r\nUsage: run\r\n");
        return -1;
    }
    if (!zap_pico_network_set_raw(true)) {
        console_printf("\r\nRun requires the TCP terminal on port 2323.\r\n");
        return -1;
    }
    run_visible = true;
    console_screen_active = true;
    run_screen_valid = false;
    run_escape_pending = false;
    run_active = 0; /* Manual commands outside the screen may change outputs. */
    run_notice = "No sequence selected; existing outputs unchanged.";
    wifi_write("\033[?1049h\033[?25l");
    return 0;
}
