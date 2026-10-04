/* Included only by cmds.h after cmd_safe. Recipes await operating targets. */
#include "cirrus1_config.h"
#include <malloc.h>
#pragma once

static bool run_visible;
static bool run_escape_pending;
static uint32_t run_escape_at;
static unsigned run_active;
static const char *run_notice;
/* Sequences: a mode key arms, the same key again within the confirm window
 * starts it. Sequences advance only while Run is on screen. */
static unsigned run_armed;
static uint32_t run_armed_at;
static uint32_t run_seq_since;   /* when the active mode started */
static uint32_t run_seq_step_at; /* last Shutdown ramp step */
/* Leaving Run while outputs are running needs ESC twice. */
static bool run_leave_armed;
static uint32_t run_leave_at;
static unsigned run_safe_seen; /* cmd_safe_requests already reflected here */

/* Manual prompts: a letter opens a prompt for one output (table below).
 * Space is STOP everywhere; 7 is STOP from the dashboard (inside a prompt it
 * is a digit). Other keys are ignored. */
typedef struct {
    char key;
    const char *label;
    unsigned max;
    bool percent;
    const char *hint; /* printf format; any %u is max */
} run_prompt_def_t;
static const run_prompt_def_t run_prompt_defs[] = {
    { 't', "TEC",       100u, true,  "Enter 0-%u%%. Non-zero needs permit, chiller ready (CB) and turns TEC fans on." },
    { 'h', "HV",        100u, true,  "Enter 0-%u%% of the calibrated HV range. 0 turns HV off." },
    { 'l', "LED LEFT",  100u, true,  "Enter 0-%u%%. 0 turns the left LEDs off." },
    { 'r', "LED RIGHT", 100u, true,  "Enter 0-%u%%. 0 turns the right LEDs off." },
    { 'g', "GLASS",     CIRRUS1_GLASS_MAX_PCT, true, "Enter 0-%u%% ITO glass heat (capped; no glass sensor)." },
    { 'c', "TEC FANS 0-1", 1u, false, "Enter 1 on or 0 off. TEC fans stay on while any TEC is on." },
    { 'f', "FANS 0-3",  3u,   false, "Enter 0 off, 1 low 30%%, 2 moderate 50%%, 3 high 90%%." },
    { 'p', "PUMPS 0-3", 3u,   false, "Enter 0 filter and return off, 1 filter, 2 return, 3 both." },
    { 'w', "WICK 0-3",  3u,   false, "Enter 0 stop, 1 prime (fast 5 s), 2 wet (slow, continuous), 3 drain (reverse 5 s)." },
};
#define RUN_PROMPT_COUNT (sizeof(run_prompt_defs) / sizeof(run_prompt_defs[0]))
static const run_prompt_def_t *run_prompt;
static char run_prompt_text[4];
static unsigned run_prompt_len;
static uint8_t run_escape_skip; /* 1: inside CSI, 2: skip one SS3 byte */
/* CSI parameter bytes, kept so F5 (ESC [ 1 5 ~) can request a redraw. */
static char run_csi[8];
static unsigned run_csi_len;
static uint8_t run_tec_percent;
static uint8_t run_hv_percent;
static uint32_t run_hv_mV; /* nominal estimate when started from Run */
static char run_result[160];

/* Protection while any TEC is on: see the CIRRUS1_TRIP_* placeholders. */
static char run_trip[80];   /* last TEC trip reason, kept until the next TEC start */
static cooling_tec_status_t run_vsense[COOLING_TEC_COUNT];
static bool run_vsense_ok;
static uint32_t run_vsense_at;

/* One sensor per poll keeps each blocking AD7124 conversion short enough
 * that key handling and STOP stay responsive. */
#define RUN_SENSOR_PERIOD_MS 500u
typedef enum { RUN_RTD, RUN_TC, RUN_CJC } run_sensor_kind_t;
typedef struct { const char *name; run_sensor_kind_t kind; uint8_t id; bool plate; } run_sensor_t;
/* Order matters: indices below are used by the plate map. */
enum { RUN_S_B, RUN_S_C, RUN_S_W1, RUN_S_W2, RUN_S_L1, RUN_S_L2, RUN_S_CB, RUN_S_TEC2, RUN_S_CJC };
static const run_sensor_t run_sensors[] = {
    [RUN_S_B]    = { "B",  RUN_RTD, CIRRUS1_B_RTD,  true },
    [RUN_S_C]    = { "C",  RUN_RTD, CIRRUS1_C_RTD,  true },
    [RUN_S_W1]   = { "W1", RUN_RTD, CIRRUS1_W1_RTD, true },
    [RUN_S_W2]   = { "W2", RUN_RTD, CIRRUS1_W2_RTD, true },
    [RUN_S_L1]   = { "L1", RUN_RTD, CIRRUS1_L1_RTD, true },
    [RUN_S_L2]   = { "L2", RUN_RTD, CIRRUS1_L2_RTD, true },
    [RUN_S_CB]   = { "CB",   RUN_TC, CIRRUS1_CB_TC,           false },
    [RUN_S_TEC2] = { "TEC2", RUN_TC, CIRRUS1_TEC_HEATSINK_TC, false },
    [RUN_S_CJC]  = { "ADC",  RUN_CJC, 0u,                     false },
};
#define RUN_SENSOR_COUNT (sizeof(run_sensors) / sizeof(run_sensors[0]))
typedef struct {
    bool seen;
    bool valid;
    ztm_adc_sensor_status_t status;
    double celsius;
    bool ranged;     /* min/max since Run was opened */
    double low;
    double high;
    unsigned faults; /* consecutive reads that were not OK */
} run_reading_t;
static run_reading_t run_readings[RUN_SENSOR_COUNT];
static unsigned run_sensor_next;
static uint32_t run_sensor_at;

/* Fan presets from notes/cirrus1-configuration.md. Both fans start open loop
 * at the preset duty; after settling, both are locked to the slower fan's RPM
 * so intake and exhaust share one target. */
typedef enum { RUN_FANS_OFF, RUN_FANS_SETTLING, RUN_FANS_MATCHED, RUN_FANS_OPEN } run_fan_phase_t;
static const char *const run_fan_levels[] = { "off", "low", "moderate", "high" };
static const uint8_t run_fan_pct[] = {
    0u, CIRRUS1_FAN_LOW_PCT, CIRRUS1_FAN_MODERATE_PCT, CIRRUS1_FAN_HIGH_PCT
};
static unsigned run_fan_level;
static run_fan_phase_t run_fan_phase;
static uint32_t run_fan_since;
static uint32_t run_fan_target;
static uint32_t run_fan_rpm[2];
static bool run_fan_rpm_ok[2];
static uint32_t run_fan_read_at;
static unsigned run_fan_stall[2]; /* consecutive slow reads per fan */

/* Colour layout for a 120x40 ANSI terminal with UTF-8 line drawing. Rows
 * hold styled bytes (SGR colour codes and 3-byte UTF-8 box characters), so
 * byte capacity exceeds the visible width; the layout pads every row to at
 * most RUN_SCREEN_COLUMNS visible columns so the terminal never wraps. */
#define RUN_SCREEN_ROWS 40u
#define RUN_SCREEN_COLUMNS 120u
#define RUN_ROW_BYTES 640u
/* The network TX buffer is 16 KiB. One refresh sends at most this many bytes;
 * rows that do not fit are sent on the following ticks. */
#define RUN_SEND_BUDGET 12000u
static char run_frame[RUN_SCREEN_ROWS][RUN_ROW_BYTES];
/* A hash per row (not a copy) detects changes and saves RAM. */
static uint32_t run_sent_hash[RUN_SCREEN_ROWS];
static bool run_row_sent[RUN_SCREEN_ROWS];
static char run_update[RUN_SEND_BUDGET + RUN_ROW_BYTES + 32u];
static unsigned run_row;
static size_t run_row_used;
static bool run_screen_valid;

/* SGR styles. Keep UTF-8 glyphs as separate literals: a hex escape followed
 * by a hex digit in the same literal would change the byte. */
#define RUN_RESET    "\033[0m"
#define RUN_BOLD     "\033[1m"
#define RUN_DIM      "\033[2m"
#define RUN_REVERSE  "\033[7m"
#define RUN_RED      "\033[91m"
#define RUN_GREEN    "\033[92m"
#define RUN_YELLOW   "\033[93m"
#define RUN_BLUE     "\033[94m"
#define RUN_MAGENTA  "\033[95m"
#define RUN_CYAN     "\033[96m"
#define RUN_WHITE    "\033[97m"
#define RUN_ON_BLUE  "\033[44m"
#define RUN_ON_RED   "\033[41m"
#define RUN_ON_GREEN "\033[42m"
#define RUN_H   "\xe2\x94\x80"
#define RUN_V   "\xe2\x94\x82"
#define RUN_TL  "\xe2\x94\x8c"
#define RUN_TR  "\xe2\x94\x90"
#define RUN_BL  "\xe2\x94\x94"
#define RUN_BR  "\xe2\x94\x98"
#define RUN_DOT "\xc2\xb7"
#define RUN_DEG "\xc2\xb0"
#define RUN_ARROW "\xe2\x96\xb6"
#define RUN_BOLT "\xe2\x9a\xa1"

static void run_frame_begin(void)
{
    memset(run_frame, 0, sizeof(run_frame));
    run_row = 0;
    run_row_used = 0;
}

static void run_frame_write(const char *text)
{
    while (*text && run_row < RUN_SCREEN_ROWS) {
        char c = *text++;
        if (c == '\r') continue;
        if (c == '\n') {
            ++run_row;
            run_row_used = 0;
        } else if (run_row_used < RUN_ROW_BYTES - 1u) {
            run_frame[run_row][run_row_used++] = c;
        }
    }
}

static void run_frame_printf(const char *format, ...)
{
    char text[RUN_ROW_BYTES];
    va_list args;
    va_start(args, format);
    (void)vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    run_frame_write(text);
}

static void run_frame_repeat(const char *glyph, unsigned count)
{
    while (count--) run_frame_write(glyph);
}

/* Length of the next ESC sequence at s (0 if none): ESC [ params final. */
static size_t run_escape_len(const char *s)
{
    if (s[0] != '\033') return 0;
    if (s[1] != '[') return s[1] ? 2u : 1u;
    size_t n = 2;
    while (s[n] && !(s[n] >= 0x40 && s[n] <= 0x7E)) ++n;
    return s[n] ? n + 1u : n;
}

/* Visible columns: skips SGR sequences and UTF-8 continuation bytes. */
static unsigned run_visible_len(const char *s)
{
    unsigned n = 0;
    while (*s) {
        size_t esc = run_escape_len(s);
        if (esc) { s += esc; continue; }
        if (((unsigned char)*s & 0xC0u) != 0x80u) ++n;
        ++s;
    }
    return n;
}

/* Append s to buf, padded with spaces to width visible columns. */
static void run_append_cell(char *buf, size_t size, const char *s, unsigned width)
{
    size_t used = strlen(buf);
    (void)snprintf(buf + used, size - used, "%s", s);
    for (unsigned n = run_visible_len(s); n < width; ++n) {
        used = strlen(buf);
        if (used + 1u >= size) break;
        buf[used] = ' ';
        buf[used + 1u] = '\0';
    }
}

/* Write s clipped to width visible columns, reset style, then pad. */
static void run_frame_cell(const char *s, unsigned width)
{
    char out[RUN_ROW_BYTES];
    size_t used = 0;
    unsigned n = 0;
    while (*s && used < sizeof(out) - 16u) {
        size_t esc = run_escape_len(s);
        if (esc) {
            memcpy(out + used, s, esc);
            used += esc;
            s += esc;
            continue;
        }
        bool lead = ((unsigned char)*s & 0xC0u) != 0x80u;
        if (lead && n == width) break;
        if (lead) ++n;
        out[used++] = *s++;
    }
    out[used] = '\0';
    run_frame_write(out);
    run_frame_write(RUN_RESET);
    for (; n < width; ++n) run_frame_write(" ");
}

static uint32_t run_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

static void run_frame_send(void)
{
    size_t used = 0;
    /* Clear on entry and on F5, then resend every row. */
    if (!run_screen_valid) {
        const char clear[] = RUN_RESET "\033[H\033[2J";
        memcpy(run_update, clear, sizeof(clear) - 1u);
        used = sizeof(clear) - 1u;
        memset(run_row_sent, 0, sizeof(run_row_sent));
        run_screen_valid = true;
    }
    for (unsigned row = 0; row < RUN_SCREEN_ROWS; ++row) {
        uint32_t hash = run_hash(run_frame[row]);
        if (run_row_sent[row] && hash == run_sent_hash[row]) continue;
        if (!run_row_sent[row] && !run_frame[row][0]) {
            /* Blank after a clear: nothing to send. */
            run_row_sent[row] = true;
            run_sent_hash[row] = hash;
            continue;
        }
        size_t length = strlen(run_frame[row]);
        if (used + length + 24u > RUN_SEND_BUDGET) break; /* rest next tick */
        int count = snprintf(run_update + used, sizeof(run_update) - used,
                             "\033[%u;1H%s" RUN_RESET "\033[K", row + 1u, run_frame[row]);
        if (count < 0 || (size_t)count >= sizeof(run_update) - used) break;
        used += (size_t)count;
        run_row_sent[row] = true;
        run_sent_hash[row] = hash;
    }
    if (used) {
        run_update[used] = '\0';
        /* Caller has checked output-idle; one write per refresh. */
        wifi_write(run_update);
    }
}

/* Panels are bordered boxes drawn side by side, one space apart. */
#define RUN_PANEL_LINES 10u
#define RUN_LINE_BYTES 320u
typedef struct {
    const char *title;
    const char *color;
    unsigned width; /* outer width including borders */
    unsigned count;
    char lines[RUN_PANEL_LINES][RUN_LINE_BYTES];
} run_panel_t;
static run_panel_t run_panels[3];

static run_panel_t *run_panel(unsigned index, const char *title, const char *color, unsigned width)
{
    run_panel_t *p = &run_panels[index];
    p->title = title;
    p->color = color;
    p->width = width;
    p->count = 0;
    return p;
}

static void run_panel_printf(run_panel_t *p, const char *format, ...)
{
    if (p->count >= RUN_PANEL_LINES) return;
    va_list args;
    va_start(args, format);
    (void)vsnprintf(p->lines[p->count++], RUN_LINE_BYTES, format, args);
    va_end(args);
}

static void run_frame_panels(unsigned count)
{
    unsigned height = 0;
    for (unsigned i = 0; i < count; ++i)
        if (run_panels[i].count > height) height = run_panels[i].count;
    for (unsigned i = 0; i < count; ++i) {
        const run_panel_t *p = &run_panels[i];
        unsigned title = run_visible_len(p->title);
        if (i) run_frame_write(" ");
        run_frame_printf("%s" RUN_TL RUN_H " " RUN_BOLD "%s" RUN_RESET "%s ", p->color, p->title, p->color);
        run_frame_repeat(RUN_H, p->width > title + 5u ? p->width - title - 5u : 0u);
        run_frame_write(RUN_TR RUN_RESET);
    }
    run_frame_write("\r\n");
    for (unsigned line = 0; line < height; ++line) {
        for (unsigned i = 0; i < count; ++i) {
            const run_panel_t *p = &run_panels[i];
            if (i) run_frame_write(" ");
            run_frame_printf("%s" RUN_V RUN_RESET " ", p->color);
            run_frame_cell(line < p->count ? p->lines[line] : "", p->width - 4u);
            run_frame_printf(" %s" RUN_V RUN_RESET, p->color);
        }
        run_frame_write("\r\n");
    }
    for (unsigned i = 0; i < count; ++i) {
        const run_panel_t *p = &run_panels[i];
        if (i) run_frame_write(" ");
        run_frame_printf("%s" RUN_BL, p->color);
        run_frame_repeat(RUN_H, p->width - 2u);
        run_frame_write(RUN_BR RUN_RESET);
    }
    run_frame_write("\r\n");
}

static unsigned run_wick_code(pump_state_t state)
{
    return state == PUMP_WICK_PRIME ? 1u : state == PUMP_WICK_WET ? 2u
         : state == PUMP_WICK_DRAIN ? 3u : 0u;
}

static unsigned run_prompt_current(char key)
{
    pumping_status_t s;
    switch (key) {
        case 't': return run_tec_percent;
        case 'h': return run_hv_percent;
        case 'l': return ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_LED_LEFT_LOAD);
        case 'r': return ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_LED_RIGHT_LOAD);
        case 'g': return ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_GLASS_HEATER_LOAD);
        case 'c': return ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_TEC_FANS_LOAD) ? 1u : 0u;
        case 'f': return run_fan_level;
        case 'p':
            pumping_get_status(&s);
            return (s.filter_state != PUMP_OFF ? 1u : 0u) | (s.sump_state != PUMP_OFF ? 2u : 0u);
        case 'w':
            pumping_get_status(&s);
            return run_wick_code(s.wick_state);
        default:  return 0u;
    }
}

static void run_sensor_poll(void)
{
    if (!ztm_adc_ready(&ztm_adc_1)) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - run_sensor_at) < RUN_SENSOR_PERIOD_MS) return;
    unsigned i = run_sensor_next;
    run_reading_t *r = &run_readings[i];
    if (run_sensors[i].kind == RUN_TC) {
        ztm_adc_tc_sample_t tc = {0};
        r->valid = ztm_adc_read_thermocouple(&ztm_adc_1, run_sensors[i].id, &tc) && tc.valid;
        r->status = tc.status;
        r->celsius = tc.temperature_c;
    } else {
        ztm_adc_rtd_sample_t rtd = {0};
        r->valid = (run_sensors[i].kind == RUN_CJC
                        ? ztm_adc_read_cold_junction(&ztm_adc_1, &rtd)
                        : ztm_adc_read_rtd(&ztm_adc_1, run_sensors[i].id, &rtd)) && rtd.valid;
        r->status = rtd.status;
        r->celsius = rtd.temperature_c;
    }
    r->seen = true;
    r->faults = r->valid && r->status == ZTM_ADC_SENSOR_OK ? 0u : r->faults + 1u;
    if (r->valid && r->status == ZTM_ADC_SENSOR_OK) {
        if (!r->ranged || r->celsius < r->low) r->low = r->celsius;
        if (!r->ranged || r->celsius > r->high) r->high = r->celsius;
        r->ranged = true;
    }
    run_sensor_next = (i + 1u) % RUN_SENSOR_COUNT;
    /* Measured after the read so a slow conversion cannot run back to back. */
    run_sensor_at = to_ms_since_boot(get_absolute_time());
}

static bool run_reading_ok(unsigned i)
{
    return run_readings[i].seen && run_readings[i].valid &&
           run_readings[i].status == ZTM_ADC_SENSOR_OK;
}

/* Colour bands are for reading at a glance, not limits: blue very cold,
 * cyan cold, green ambient, yellow warm, red hot. */
static const char *run_temp_color(double celsius)
{
    return celsius < -20.0 ? RUN_BLUE : celsius < 5.0 ? RUN_CYAN
         : celsius < 30.0 ? RUN_GREEN : celsius < 45.0 ? RUN_YELLOW : RUN_RED;
}

/* "<name> <value>" where value is degrees C or a short fault word. */
static void run_format_reading(unsigned i, char *text, size_t size)
{
    const run_reading_t *r = &run_readings[i];
    const char *name = run_sensors[i].name;
    if (run_reading_ok(i))
        (void)snprintf(text, size, RUN_WHITE "%s " RUN_BOLD "%s%.1f" RUN_RESET,
                       name, run_temp_color(r->celsius), r->celsius);
    else if (!r->seen)
        (void)snprintf(text, size, RUN_WHITE "%s " RUN_DIM "--" RUN_RESET, name);
    else
        (void)snprintf(text, size, RUN_WHITE "%s " RUN_MAGENTA "%s" RUN_RESET, name,
                       !r->valid ? "ERR"
                       : r->status == ZTM_ADC_SENSOR_DISCONNECTED ? "open"
                       : r->status == ZTM_ADC_SENSOR_CJC_UNAVAILABLE ? "noCJC" : "range");
}

static void run_fans_off(void)
{
    run_fan_level = 0;
    run_fan_phase = RUN_FANS_OFF;
    run_fan_target = 0;
}

static void run_fan_poll(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((uint32_t)(now - run_fan_read_at) < 1000u) return;
    run_fan_read_at = now;
    const uint8_t fans[2] = { CIRRUS1_EXHAUST_FAN, CIRRUS1_INTAKE_FAN };
    for (unsigned i = 0; i < 2u; ++i) {
        uint16_t count = 0u;
        run_fan_rpm_ok[i] = ztm_pwm_fan_get_tach_status(&ztm_pwm_1, fans[i], &count, &run_fan_rpm[i])
                            == ZTM_PWM_FAN_TACH_OK;
        /* Stall: a settled fan reporting a valid but very low RPM. An
         * invalid tach is unknown, not stalled. */
        bool settled = run_fan_phase == RUN_FANS_MATCHED || run_fan_phase == RUN_FANS_OPEN;
        run_fan_stall[i] = settled && run_fan_rpm_ok[i] && run_fan_rpm[i] < CIRRUS1_FAN_STALL_RPM
                         ? run_fan_stall[i] + 1u : 0u;
    }
    for (unsigned i = 0; i < 2u; ++i) {
        if (run_fan_stall[i] < CIRRUS1_FAN_STALL_READS) continue;
        /* Stop both so a stalled fan cannot leave a pressure differential. */
        bool ok = ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN);
        ok = ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_INTAKE_FAN) && ok;
        run_fans_off();
        run_fan_stall[0] = run_fan_stall[1] = 0u;
        (void)snprintf(run_result, sizeof(run_result),
                       "FAN STALL: %s fan F%u below %u RPM; both fans %s.",
                       i ? "intake" : "exhaust", (unsigned)fans[i], CIRRUS1_FAN_STALL_RPM,
                       ok ? "stopped" : "stop FAILED, press Space");
        run_notice = run_result;
        return;
    }
    if (run_fan_phase != RUN_FANS_SETTLING ||
        (uint32_t)(now - run_fan_since) < CIRRUS1_FAN_SETTLE_MS)
        return;
    uint8_t pct = run_fan_pct[run_fan_level];
    if (!run_fan_rpm_ok[0] || !run_fan_rpm_ok[1]) {
        run_fan_phase = RUN_FANS_OPEN;
        (void)snprintf(run_result, sizeof(run_result),
                       "Fan RPM match FAILED (no valid tach); both left at %u%% open loop.", (unsigned)pct);
        run_notice = run_result;
        return;
    }
    uint32_t target = run_fan_rpm[0] < run_fan_rpm[1] ? run_fan_rpm[0] : run_fan_rpm[1];
    if (target == 0u ||
        !ztm_pwm_fan_set_min_drive(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN, CIRRUS1_FAN_MIN_DRIVE_PCT) ||
        !ztm_pwm_fan_set_min_drive(&ztm_pwm_1, CIRRUS1_INTAKE_FAN, CIRRUS1_FAN_MIN_DRIVE_PCT) ||
        !ztm_pwm_fan_set_target_rpm(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN, target) ||
        !ztm_pwm_fan_set_target_rpm(&ztm_pwm_1, CIRRUS1_INTAKE_FAN, target)) {
        /* Never leave one fan closed loop and the other open loop. */
        (void)ztm_pwm_fan_set_open_loop(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN, pct);
        (void)ztm_pwm_fan_set_open_loop(&ztm_pwm_1, CIRRUS1_INTAKE_FAN, pct);
        run_fan_phase = RUN_FANS_OPEN;
        (void)snprintf(run_result, sizeof(run_result),
                       "Fan RPM target FAILED; both returned to %u%% open loop.", (unsigned)pct);
        run_notice = run_result;
        return;
    }
    run_fan_phase = RUN_FANS_MATCHED;
    run_fan_target = target;
    (void)snprintf(run_result, sizeof(run_result),
                   "Fans %s: intake and exhaust locked to %lu RPM (closed loop).",
                   run_fan_levels[run_fan_level], (unsigned long)target);
    run_notice = run_result;
}

/* VSENSE of the four TEC boards (A-D), read once a second for display. */
static void run_vsense_poll(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!cooling_ready() || (uint32_t)(now - run_vsense_at) < 1000u) return;
    run_vsense_at = now;
    run_vsense_ok = cooling_tec_read_all(run_vsense);
}

/* False, with the reason in why, when the TECs must not run. starting
 * requires fresh valid CB and TEC2 readings and CB at or below the chiller
 * ready limit; while running, a sensor trips after repeated faults. Limits
 * are CIRRUS1_* placeholders in cirrus1_config.h. */
static bool run_tec_check(bool starting, char *why, size_t size)
{
    static const struct { unsigned sensor; const char *name; double trip; } guards[] = {
        { RUN_S_TEC2, "TEC2 heat-sink", CIRRUS1_TRIP_HEATSINK_C },
        { RUN_S_CB,   "CB chiller return", CIRRUS1_TRIP_CB_C },
    };
    if (!ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_TEC_FANS_LOAD)) {
        (void)snprintf(why, size, "TEC fans L%u are off", CIRRUS1_TEC_FANS_LOAD);
        return false;
    }
    for (unsigned g = 0; g < sizeof(guards) / sizeof(guards[0]); ++g) {
        unsigned i = guards[g].sensor;
        const run_reading_t *r = &run_readings[i];
        if (!run_reading_ok(i) && (starting || r->faults >= CIRRUS1_SENSOR_FAULT_READS)) {
            (void)snprintf(why, size, "%s sensor %s", guards[g].name, r->seen ? "fault" : "not read yet");
            return false;
        }
        if (run_reading_ok(i) && r->celsius > guards[g].trip) {
            (void)snprintf(why, size, "%s %.1f" RUN_DEG "C above %.0f", guards[g].name,
                           r->celsius, guards[g].trip);
            return false;
        }
    }
    if (starting && run_readings[RUN_S_CB].celsius > CIRRUS1_CB_READY_C) {
        (void)snprintf(why, size, "chiller not ready: CB %.1f" RUN_DEG "C above %.0f",
                       run_readings[RUN_S_CB].celsius, CIRRUS1_CB_READY_C);
        return false;
    }
    for (unsigned i = 0; i < RUN_SENSOR_COUNT; ++i) {
        if (!run_sensors[i].plate || !run_reading_ok(i)) continue;
        double c = run_readings[i].celsius;
        if (c > CIRRUS1_TRIP_PLATE_HIGH_C || c < CIRRUS1_TRIP_PLATE_LOW_C) {
            (void)snprintf(why, size, "plate %s %.1f" RUN_DEG "C outside %.0f..%.0f", run_sensors[i].name,
                           c, CIRRUS1_TRIP_PLATE_LOW_C, CIRRUS1_TRIP_PLATE_HIGH_C);
            return false;
        }
    }
    return true;
}

/* TEC trip: all TECs off and permission revoked. Fans, pumps and the other
 * outputs keep running so the heat-sinks keep cooling. */
static void run_tec_trip(const char *reason)
{
    bool ok = cooling_tec_stop_all();
    run_tec_percent = 0;
    (void)snprintf(run_trip, sizeof(run_trip), "%s", reason);
    (void)snprintf(run_result, sizeof(run_result), "TEC TRIP: %s. TECs off, permission revoked; %s",
                   reason, ok ? "fans keep running." : "TEC STOP FAILED, press Space or type safe.");
    run_notice = run_result;
}

/* Something Run started is still running: TECs, HV or a timed sequence. */
static bool run_busy(void)
{
    return run_tec_percent || run_hv_percent || run_active == 3u || run_active == 4u || run_active == 6u;
}

static void run_protect(void)
{
    if (!run_tec_percent) return;
    if (!cooling_tec_permission_granted()) {
        /* Revoked outside Run (or by a failed write): TECs are already off. */
        run_tec_percent = 0;
        return;
    }
    char why[80];
    if (!run_tec_check(false, why, sizeof(why))) run_tec_trip(why);
}

/* Heap free = space between the end of static data and the stack limit,
 * minus what malloc has handed out. Static RAM is fixed at link time. */
extern char __end__, __StackLimit;
static unsigned run_free_ram_kb(void)
{
    struct mallinfo info = mallinfo();
    size_t heap = (size_t)(&__StackLimit - &__end__);
    size_t used = (size_t)info.uordblks;
    return used < heap ? (unsigned)((heap - used) / 1024u) : 0u;
}

static void run_frame_title(void)
{
    bool stopped = run_active == 7u;
    unsigned free_kb = run_free_ram_kb();
    char right[192];
    (void)snprintf(right, sizeof(right),
                   "RAM free %s%u KB" RUN_WHITE "  Hub %s  %s%s" RUN_ON_BLUE "  %s%s " RUN_RESET,
                   free_kb < 32u ? RUN_YELLOW : "", free_kb,
                   HUB_FIRMWARE_VERSION,
                   cooling_tec_permission_granted() ? RUN_ON_GREEN " TEC PERMIT " : RUN_ON_RED " TEC LOCKED ",
                   RUN_ON_BLUE,
                   stopped ? RUN_ON_RED " STOPPED " : RUN_ON_BLUE " READY ",
                   RUN_ON_BLUE);
    /* The Zaptronics bolt is a wide glyph: two terminal columns, counted as one. */
    const char left[] = " " RUN_YELLOW RUN_BOLT RUN_WHITE " CIRRUS1 CLOUD CHAMBER  " RUN_DOT "  RUN CONSOLE";
    unsigned used = run_visible_len(left) + 1u + run_visible_len(right);
    run_frame_write(RUN_ON_BLUE RUN_WHITE RUN_BOLD);
    run_frame_write(left);
    run_frame_repeat(" ", used < RUN_SCREEN_COLUMNS ? RUN_SCREEN_COLUMNS - used : 0u);
    run_frame_write(right);
    run_frame_write("\r\n");
}

static void run_build_sequence(run_panel_t *p)
{
    static const struct { const char *name, *what, *tag, *tag_color; } modes[7] = {
        { "Tests",    "ZapLink board checks",     "N/C",    RUN_DIM },
        { "Standby",  "filter/fans/lights/glass", "DRAFT",  RUN_YELLOW },
        { "Prime",    "IPA loop, then Standby",   "DRAFT",  RUN_YELLOW },
        { "Runup",    "wick, low TEC, chiller",   "TEC TEST", RUN_MAGENTA },
        { "Go",       "full run",                 "LOCKED", RUN_RED },
        { "Shutdown", "TEC ramp down, lights on", "DRAFT",  RUN_YELLOW },
        { "STOP",     "safe off: everything",     "SPACE",  RUN_RED },
    };
    for (unsigned i = 0; i < 7u; ++i) {
        bool active = run_active == i + 1u;
        run_panel_printf(p, "%s%s " RUN_RESET "%s%u %-8s" RUN_RESET " %-24s " RUN_BOLD "%s%s" RUN_RESET,
                         active ? RUN_YELLOW RUN_BOLD : "", active ? RUN_ARROW : " ",
                         active ? RUN_REVERSE RUN_BOLD : RUN_WHITE, i + 1u, modes[i].name,
                         modes[i].what, modes[i].tag_color, modes[i].tag);
    }
    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - run_seq_since;
    if (run_active == 3u)
        run_panel_printf(p, RUN_CYAN "Prime %lu of %lu s, then Standby" RUN_RESET,
                         (unsigned long)(elapsed / 1000u), (unsigned long)(CIRRUS1_SEQ_PRIME_MS / 1000u));
    else if (run_active == 4u && run_reading_ok(RUN_S_B))
        run_panel_printf(p, "%sRunup: B %.1f" RUN_DEG "C, ready at <= %.0f%s" RUN_RESET,
                         run_readings[RUN_S_B].celsius <= CIRRUS1_SEQ_RUNUP_READY_C ? RUN_GREEN RUN_BOLD : RUN_CYAN,
                         run_readings[RUN_S_B].celsius, CIRRUS1_SEQ_RUNUP_READY_C,
                         run_readings[RUN_S_B].celsius <= CIRRUS1_SEQ_RUNUP_READY_C ? " READY" : "");
    else if (run_active == 4u)
        run_panel_printf(p, RUN_MAGENTA "Runup: plate B not reading" RUN_RESET);
    else if (run_active == 6u && run_tec_percent)
        run_panel_printf(p, RUN_CYAN "Shutdown: TEC %u%%, -%u%% every %u s" RUN_RESET,
                         (unsigned)run_tec_percent, CIRRUS1_SEQ_RAMP_STEP_PCT, CIRRUS1_SEQ_RAMP_STEP_MS / 1000u);
    else if (run_active == 6u)
        run_panel_printf(p, RUN_GREEN "Shutdown: TECs off" RUN_RESET);
    else
        run_panel_printf(p, " ");
    run_panel_printf(p, RUN_DIM "Press a mode twice to start it. Values are" RUN_RESET);
    run_panel_printf(p, RUN_DIM "placeholders in cirrus1_config.h." RUN_RESET);
}

/* Plate map follows notes/cirrus1-configuration.md: looking from the edge,
 * AC inlet on the far side. A and D columns have no RTD fitted. */
static void run_build_temperatures(run_panel_t *p)
{
    if (!ztm_adc_ready(&ztm_adc_1)) {
        run_panel_printf(p, RUN_RED RUN_BOLD "ZTM_ADC not ready: no temperatures available" RUN_RESET);
        return;
    }
    char t[RUN_SENSOR_COUNT][64];
    for (unsigned i = 0; i < RUN_SENSOR_COUNT; ++i)
        run_format_reading(i, t[i], sizeof(t[i]));

    char middle[RUN_LINE_BYTES] = "  ";
    run_append_cell(middle, sizeof(middle), t[RUN_S_L1], 9u);
    run_append_cell(middle, sizeof(middle), RUN_DIM "  (A)  " RUN_RESET, 7u);
    run_append_cell(middle, sizeof(middle), t[RUN_S_B], 9u);
    run_append_cell(middle, sizeof(middle), " ", 1u);
    unsigned bar = run_visible_len(middle);
    run_append_cell(middle, sizeof(middle), RUN_BLUE RUN_V RUN_RESET " ", 2u);
    run_append_cell(middle, sizeof(middle), t[RUN_S_C], 9u);
    run_append_cell(middle, sizeof(middle), RUN_DIM "  (D)  " RUN_RESET, 7u);
    run_append_cell(middle, sizeof(middle), t[RUN_S_L2], 9u);
    /* Centre W2/W1 on the divider between the B and C columns. */
    unsigned pad = bar > 3u ? bar - 3u : 0u;
    run_panel_printf(p, "%*s%s", (int)pad, "", t[RUN_S_W2]);
    run_panel_printf(p, "%s", middle);
    run_panel_printf(p, "%*s%s", (int)pad, "", t[RUN_S_W1]);
    run_panel_printf(p, " ");
    run_panel_printf(p, "%s chiller return  " RUN_DIM RUN_DOT RUN_RESET "  %s heat-sink  "
                     RUN_DIM RUN_DOT RUN_RESET "  %s board",
                     t[RUN_S_CB], t[RUN_S_TEC2], t[RUN_S_CJC]);

    int hot = -1, cold = -1;
    for (unsigned i = 0; i < RUN_SENSOR_COUNT; ++i) {
        if (!run_sensors[i].plate || !run_reading_ok(i)) continue;
        if (hot < 0 || run_readings[i].celsius > run_readings[hot].celsius) hot = (int)i;
        if (cold < 0 || run_readings[i].celsius < run_readings[cold].celsius) cold = (int)i;
    }
    if (hot >= 0)
        run_panel_printf(p, "Plate spread " RUN_BOLD RUN_YELLOW "%.1f" RUN_RESET
                         " (hottest %s, coldest %s)  " RUN_DIM "RT0 RT1 TC2 n/c" RUN_RESET,
                         run_readings[hot].celsius - run_readings[cold].celsius,
                         run_sensors[hot].name, run_sensors[cold].name);
    else
        run_panel_printf(p, "Plate spread: no valid plate readings  " RUN_DIM "RT0 RT1 TC2 n/c" RUN_RESET);

    /* Min/max since Run was opened, to help choose operating thresholds. */
    run_panel_printf(p, RUN_DIM "Min..max since Run opened (one sensor every %.1f s):" RUN_RESET,
                     (double)RUN_SENSOR_PERIOD_MS / 1000.0);
    char line[RUN_LINE_BYTES] = "";
    for (unsigned i = 0; i < RUN_SENSOR_COUNT; ++i) {
        const run_reading_t *r = &run_readings[i];
        char entry[96];
        if (r->ranged)
            (void)snprintf(entry, sizeof(entry), RUN_WHITE "%s " "%s%.1f" RUN_RESET RUN_DIM ".." RUN_RESET "%s%.1f" RUN_RESET,
                           run_sensors[i].name, run_temp_color(r->low), r->low,
                           run_temp_color(r->high), r->high);
        else
            (void)snprintf(entry, sizeof(entry), RUN_WHITE "%s " RUN_DIM "--" RUN_RESET, run_sensors[i].name);
        run_append_cell(line, sizeof(line), entry, 21u);
        if (i % 3u == 2u) {
            run_panel_printf(p, "%s", line);
            line[0] = '\0';
        }
    }
}

static void run_build_status(run_panel_t *p)
{
    bool failed = strstr(run_notice, "FAILED") != NULL || strstr(run_notice, "NOT CONFIRMED") != NULL ||
                  strstr(run_notice, "TRIP") != NULL || strstr(run_notice, "STALL") != NULL;
    run_panel_printf(p, "%s%s" RUN_RESET,
                     failed ? RUN_RED RUN_BOLD : run_active == 7u ? RUN_RED : RUN_YELLOW RUN_BOLD,
                     run_notice);
    if (run_prompt)
        run_panel_printf(p, RUN_CYAN RUN_BOLD "%s [%u%s]> " RUN_RESET RUN_REVERSE "%s_" RUN_RESET
                         RUN_DIM "   Enter apply " RUN_DOT " Backspace edit " RUN_DOT
                         " ESC cancel " RUN_DOT " Space STOP" RUN_RESET,
                         run_prompt->label, run_prompt_current(run_prompt->key),
                         run_prompt->percent ? "%" : "", run_prompt_text);
    else
        run_panel_printf(p, RUN_DIM "No prompt open." RUN_RESET);
    /* Protection summary: limits are placeholders until confirmed. */
    bool cb_ok = run_reading_ok(RUN_S_CB);
    bool ready = cb_ok && run_readings[RUN_S_CB].celsius <= CIRRUS1_CB_READY_C;
    run_panel_printf(p, "Chiller %s" RUN_RESET " (CB " RUN_DEG "C <= %.0f)  " RUN_DIM RUN_DOT RUN_RESET
                     "  TEC trips: sink > %.0f, CB > %.0f, plate %.0f..%.0f, TEC fans off, sensor fault",
                     ready ? RUN_GREEN RUN_BOLD "READY" : cb_ok ? RUN_YELLOW RUN_BOLD "NOT READY" : RUN_MAGENTA "CB ?",
                     CIRRUS1_CB_READY_C, CIRRUS1_TRIP_HEATSINK_C, CIRRUS1_TRIP_CB_C,
                     CIRRUS1_TRIP_PLATE_LOW_C, CIRRUS1_TRIP_PLATE_HIGH_C);
    run_panel_printf(p, "Last TEC trip  %s%s" RUN_RESET,
                     run_trip[0] ? RUN_RED RUN_BOLD : RUN_DIM, run_trip[0] ? run_trip : "none");
}

/* Zero values dim, non-zero values bright, so active outputs stand out. */
static const char *run_level_color(unsigned value)
{
    return value ? RUN_BOLD RUN_GREEN : RUN_DIM;
}

static void run_build_outputs(run_panel_t *p)
{
    unsigned left = ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_LED_LEFT_LOAD);
    unsigned right = ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_LED_RIGHT_LOAD);
    unsigned glass = ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_GLASS_HEATER_LOAD);
    unsigned tec_fans = ztm_pwm_channel_get_duty(&ztm_pwm_1, CIRRUS1_TEC_FANS_LOAD);
    bool permit = cooling_tec_permission_granted();
    run_panel_printf(p, "TEC      %s%3u%%" RUN_RESET "  permit %s" RUN_RESET,
                     run_level_color(run_tec_percent), (unsigned)run_tec_percent,
                     permit ? RUN_GREEN RUN_BOLD "GRANTED" : RUN_RED RUN_BOLD "LOCKED");
    if (run_vsense_ok)
        run_panel_printf(p, "VSENSE V A%s%5.2f" RUN_RESET " B%s%5.2f" RUN_RESET
                         " C%s%5.2f" RUN_RESET " D%s%5.2f" RUN_RESET,
                         run_level_color(run_vsense[0].output_uV > 100000u), (double)run_vsense[0].output_uV / 1e6,
                         run_level_color(run_vsense[1].output_uV > 100000u), (double)run_vsense[1].output_uV / 1e6,
                         run_level_color(run_vsense[2].output_uV > 100000u), (double)run_vsense[2].output_uV / 1e6,
                         run_level_color(run_vsense[3].output_uV > 100000u), (double)run_vsense[3].output_uV / 1e6);
    else
        run_panel_printf(p, "VSENSE   " RUN_DIM "--" RUN_RESET);
    if (run_hv_percent)
        run_panel_printf(p, "HV       %s%3u%%" RUN_RESET "  ~" RUN_BOLD RUN_YELLOW "%.2f kV" RUN_RESET RUN_DIM " est." RUN_RESET,
                         run_level_color(run_hv_percent), (unsigned)run_hv_percent, (double)run_hv_mV / 1e6);
    else
        run_panel_printf(p, "HV       %s%3u%%" RUN_RESET, run_level_color(0u), 0u);
    run_panel_printf(p, "LED L    %s%3u%%" RUN_RESET "  LED R  %s%3u%%" RUN_RESET,
                     run_level_color(left), left, run_level_color(right), right);
    run_panel_printf(p, "Glass    %s%3u%%" RUN_RESET "  TEC fans %s" RUN_RESET,
                     run_level_color(glass), glass, tec_fans ? RUN_BOLD RUN_GREEN "ON" : RUN_DIM "off");
    run_panel_printf(p, "PWM POWER_SAFE rc %s%d" RUN_RESET,
                     ztm_pwm_1.power_safe_rc ? RUN_RED RUN_BOLD : RUN_GREEN, ztm_pwm_1.power_safe_rc);
}

static void run_build_fans(run_panel_t *p)
{
    static const char *const phases[] = { "off", "settling", "matched", "open loop" };
    static const char *const phase_colors[] = { RUN_DIM, RUN_YELLOW RUN_BOLD, RUN_GREEN RUN_BOLD, RUN_YELLOW RUN_BOLD };
    run_panel_printf(p, "Preset   %s%s (%u%%)" RUN_RESET, run_level_color(run_fan_level),
                     run_fan_levels[run_fan_level], (unsigned)run_fan_pct[run_fan_level]);
    run_panel_printf(p, "State    %s%s" RUN_RESET, phase_colors[run_fan_phase], phases[run_fan_phase]);
    const char *names[2] = { "Exhaust", "Intake " };
    const unsigned ids[2] = { CIRRUS1_EXHAUST_FAN, CIRRUS1_INTAKE_FAN };
    for (unsigned i = 0; i < 2u; ++i) {
        if (run_fan_rpm_ok[i])
            run_panel_printf(p, "%s  F%u  " RUN_BOLD RUN_WHITE "%5lu" RUN_RESET " rpm",
                             names[i], ids[i], (unsigned long)run_fan_rpm[i]);
        else
            run_panel_printf(p, "%s  F%u  " RUN_DIM "   --" RUN_RESET " rpm", names[i], ids[i]);
    }
    if (run_fan_phase == RUN_FANS_MATCHED)
        run_panel_printf(p, "Target       " RUN_GREEN RUN_BOLD "%5lu" RUN_RESET " rpm (shared)",
                         (unsigned long)run_fan_target);
    else
        run_panel_printf(p, "Target       " RUN_DIM "   --" RUN_RESET);
    char duties[RUN_LINE_BYTES] = "Duty %";
    for (uint8_t i = 1; i <= ZTM_PWM_FAN_COUNT; ++i) {
        unsigned duty = ztm_pwm_fan_get_duty(&ztm_pwm_1, i);
        size_t len = strlen(duties);
        (void)snprintf(duties + len, sizeof(duties) - len, " %u:%s%u" RUN_RESET,
                       (unsigned)i, run_level_color(duty), duty);
    }
    run_panel_printf(p, "%s", duties);
}

static void run_build_pumps(run_panel_t *p)
{
    pumping_status_t s;
    pumping_get_status(&s);
    run_panel_printf(p, "Filter   M%u  %s%4.0f" RUN_RESET " steps/s", CIRRUS1_FILTER_MOTOR,
                     run_level_color(s.filter_speed != 0.0f), (double)s.filter_speed);
    run_panel_printf(p, "Wick     M%u  %s%4.0f" RUN_RESET " steps/s", CIRRUS1_WICK_MOTOR,
                     run_level_color(s.wick_speed != 0.0f), (double)s.wick_speed);
    run_panel_printf(p, "Return   M%u  %s%4.0f" RUN_RESET " steps/s", CIRRUS1_RETURN_MOTOR,
                     run_level_color(s.sump_speed != 0.0f), (double)s.sump_speed);
    static const char *const wick_modes[] = {
        RUN_DIM "off", RUN_CYAN RUN_BOLD "prime", RUN_CYAN RUN_BOLD "wet (continuous)", RUN_YELLOW RUN_BOLD "drain"
    };
    run_panel_printf(p, "Wick mode    %s" RUN_RESET,
                     s.wick_state == PUMP_WICK_DOSE ? RUN_CYAN RUN_BOLD "dose" : wick_modes[run_wick_code(s.wick_state)]);
    run_panel_printf(p, "Wick timer   %lu ms", (unsigned long)s.wick_remaining_ms);
    run_panel_printf(p, RUN_DIM "Commanded speeds, not measured" RUN_RESET);
}

static void run_build_modules(run_panel_t *p)
{
    /* Six per row; initialization status, not a live board test. */
    char line[RUN_LINE_BYTES] = "";
    unsigned shown = 0;
    for (size_t i = 0; i < modules_count(); ++i) {
        const module_entry_t *m = modules_get(i);
        if (!m) continue;
        char entry[64];
        bool ok = modules_is_initialized(i);
        (void)snprintf(entry, sizeof(entry), "%s%s " RUN_RESET "%-12s",
                       ok ? RUN_GREEN RUN_BOLD : RUN_RED RUN_BOLD, ok ? "ok" : "FAIL",
                       m->short_name ? m->short_name : "?");
        run_append_cell(line, sizeof(line), entry, 19u);
        if (++shown % 6u == 0u) {
            run_panel_printf(p, "%s", line);
            line[0] = '\0';
        }
    }
    if (line[0]) run_panel_printf(p, "%s", line);
    char loads[RUN_LINE_BYTES] = "Load PWM %";
    for (uint8_t i = 1; i <= ZTM_PWM_CHANNEL_COUNT; ++i) {
        unsigned duty = ztm_pwm_channel_get_duty(&ztm_pwm_1, i);
        size_t len = strlen(loads);
        (void)snprintf(loads + len, sizeof(loads) - len, " L%u:%s%u" RUN_RESET,
                       (unsigned)i, run_level_color(duty), duty);
    }
    run_panel_printf(p, "%s", loads);
    run_panel_printf(p, RUN_DIM "Sequence values are placeholders in cirrus1_config.h. Go stays locked until running targets are set." RUN_RESET);
}

static void run_frame_footer(void)
{
    run_frame_write(" " RUN_REVERSE " t " RUN_RESET " TEC   "
                    RUN_REVERSE " h " RUN_RESET " HV   "
                    RUN_REVERSE " l " RUN_RESET " " RUN_REVERSE " r " RUN_RESET " LEDs   "
                    RUN_REVERSE " g " RUN_RESET " glass   "
                    RUN_REVERSE " c " RUN_RESET " TEC fans   "
                    RUN_REVERSE " f " RUN_RESET " fans   "
                    RUN_REVERSE " p " RUN_RESET " pumps   "
                    RUN_REVERSE " w " RUN_RESET " wick\r\n");
    run_frame_write(" " RUN_REVERSE " 1-6 " RUN_RESET " modes   "
                    RUN_REVERSE " ESC " RUN_RESET " leave   "
                    RUN_ON_RED RUN_WHITE RUN_BOLD " SPACE " RUN_RESET " "
                    RUN_ON_RED RUN_WHITE RUN_BOLD " 7 " RUN_RESET RUN_RED RUN_BOLD " STOP" RUN_RESET
                    RUN_DIM "   other keys ignored" RUN_RESET "\r\n");
}

static void run_draw(void)
{
    if (!run_visible || !zap_pico_network_output_idle()) return;
    /* Full redraws happen on entry and on F5 only; otherwise changed rows. */
    run_frame_begin();
    run_frame_title();
    run_frame_write("\r\n");
    run_build_sequence(run_panel(0, "Sequence", RUN_CYAN, 50u));
    run_build_temperatures(run_panel(1, "Temperatures " RUN_DEG "C", RUN_BLUE, 69u));
    run_frame_panels(2);
    run_build_status(run_panel(0, "Status", run_active == 7u ? RUN_RED : RUN_YELLOW, 120u));
    run_frame_panels(1);
    run_build_outputs(run_panel(0, "Outputs", RUN_MAGENTA, 40u));
    run_build_fans(run_panel(1, "Fans", RUN_GREEN, 39u));
    run_build_pumps(run_panel(2, "Pumps", RUN_CYAN, 39u));
    run_frame_panels(3);
    run_build_modules(run_panel(0, "Modules and loads", RUN_WHITE, 120u));
    run_frame_panels(1);
    run_frame_write("\r\n");
    run_frame_footer();
    run_frame_send();
}

static void run_stop(void)
{
    run_escape_pending = false;
    run_escape_skip = 0;
    run_prompt = NULL;
    int rc = cmd_safe(0, NULL, NULL, 0);
    run_safe_seen = cmd_safe_requests;
    run_active = 7;
    run_armed = 0;
    run_tec_percent = 0;
    run_hv_percent = 0;
    run_hv_mV = 0;
    run_fans_off();
    run_notice = rc == 0 ? "STOP: off commands accepted; physical state not verified."
                        : "STOP FAILED: one or more outputs could not be confirmed off.";
}

static void run_apply_tec(unsigned percent)
{
    if (percent == 0u) {
        /* Per-channel stop keeps operator permission; STOP revokes it. */
        bool ok = true;
        for (uint8_t id = 1u; id <= COOLING_TEC_COUNT; ++id)
            ok = cooling_tec_stop(id) && ok;
        if (ok) run_tec_percent = 0;
        run_notice = ok ? "TEC 0%: all four stopped; permission kept."
                        : "TEC STOP FAILED on one or more channels. Press Space for full STOP.";
        return;
    }
    if (!cooling_tec_permission_granted()) {
        run_notice = "TEC LOCKED: leave Run and use 'tec permit' after checking the chiller.";
        return;
    }
    /* TEC fans first: the TECs never run without them. */
    if (!ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_TEC_FANS_LOAD, 100u)) {
        run_notice = "TEC not started: TEC fans could not be turned on.";
        return;
    }
    char why[80];
    if (!run_tec_check(true, why, sizeof(why))) {
        (void)snprintf(run_result, sizeof(run_result), "TEC not started: %s. TEC fans left on.", why);
        run_notice = run_result;
        return;
    }
    run_trip[0] = '\0';
    uint8_t dac = (uint8_t)((percent * 255u + 50u) / 100u);
    cooling_tec_status_t status[COOLING_TEC_COUNT];
    if (!cooling_tec_set_all_raw(dac, status)) {
        run_tec_percent = 0;
        run_notice = "TEC set FAILED; permission revoked and all-off attempted.";
        return;
    }
    run_tec_percent = (uint8_t)percent;
    (void)snprintf(run_result, sizeof(run_result),
                   "TEC %u%% (DAC %u) on all four; VSENSE %.2f %.2f %.2f %.2f V",
                   percent, (unsigned)dac,
                   (double)status[0].output_uV / 1e6, (double)status[1].output_uV / 1e6,
                   (double)status[2].output_uV / 1e6, (double)status[3].output_uV / 1e6);
    run_notice = run_result;
}

static void run_apply_hv(unsigned percent)
{
    if (percent == 0u) {
        (void)ztm_psu_hv_stop(&ztm_psu_hv_1);
        bool enabled = true;
        bool off = ztm_psu_hv_read_enable(&ztm_psu_hv_1, &enabled) && !enabled;
        if (off) {
            run_hv_percent = 0;
            run_hv_mV = 0;
        }
        run_notice = off ? "HV 0%: converter EN low (off)."
                         : "HV OFF NOT CONFIRMED. Press Space for full STOP.";
        return;
    }
    /* 1-100% spans the calibrated nominal range; 100% is the highest safe code. */
    uint32_t span = ZTM_PSU_HV_CAL_MAX_ESTIMATED_HV_V - ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V;
    uint32_t target_V = ZTM_PSU_HV_CAL_MIN_ESTIMATED_HV_V + (percent * span + 50u) / 100u;
    uint8_t code = 0u;
    uint32_t drive_uV = 0u, hv_mV = 0u;
    if (!ztm_psu_hv_select_code_for_estimated_hv(target_V, &code, &drive_uV, &hv_mV)) {
        run_notice = "HV target could not be mapped to a safe code; output unchanged.";
        return;
    }
    if (!ztm_psu_hv_start(&ztm_psu_hv_1, code, ZTM_PSU_HV_DEFAULT_PGOOD_TIMEOUT_MS)) {
        run_hv_percent = 0;
        run_hv_mV = 0;
        (void)snprintf(run_result, sizeof(run_result),
                       "HV start FAILED at %s (rc=%d); converter disabled.",
                       ztm_psu_hv_operation_stage_name(ztm_psu_hv_1.last_operation_stage),
                       ztm_psu_hv_1.last_operation_rc);
        run_notice = run_result;
        return;
    }
    run_hv_percent = (uint8_t)percent;
    run_hv_mV = ztm_psu_hv_1.estimated_hv_mV;
    (void)snprintf(run_result, sizeof(run_result),
                   "HV %u%%: code %u, nominal ~%.2f kV (estimate, not a direct measurement)",
                   percent, (unsigned)code, (double)ztm_psu_hv_1.estimated_hv_mV / 1e6);
    run_notice = run_result;
}

static void run_apply_led(uint8_t load, const char *name, unsigned percent)
{
    bool ok = ztm_pwm_channel_set(&ztm_pwm_1, load, (uint8_t)percent);
    (void)snprintf(run_result, sizeof(run_result), ok ? "%s set to %u%%." : "%s set FAILED (%u%% requested).",
                   name, percent);
    run_notice = run_result;
}

static void run_apply_tec_fans(unsigned on)
{
    if (!on && run_tec_percent) {
        run_notice = "TEC fans stay on while a TEC is on. Set TEC to 0 first.";
        return;
    }
    bool ok = ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_TEC_FANS_LOAD, on ? 100u : 0u);
    run_notice = ok ? (on ? "TEC fans on (100%)." : "TEC fans off.")
                    : "TEC fans set FAILED.";
}

/* p: bit 0 filter loop, bit 1 return; both fixed speed from cirrus1_config.h. */
static void run_apply_pumps(unsigned code)
{
    if (!pumping_ready()) {
        run_notice = "Pumps unavailable: ztm_stepper not ready.";
        return;
    }
    bool ok = (code & 1u) ? pumping_filter_start_normal() : pumping_filter_stop();
    ok = ((code & 2u) ? pumping_sump_run() : pumping_sump_stop()) && ok;
    (void)snprintf(run_result, sizeof(run_result), "%sFilter %s, return %s.",
                   ok ? "" : "Pump command FAILED. ",
                   (code & 1u) ? "on" : "off", (code & 2u) ? "on" : "off");
    run_notice = run_result;
}

static void run_apply_wick(unsigned mode)
{
    static const char *const names[] = { "stopped", "priming (fast, 5 s)", "wetting (slow, continuous)", "draining (reverse, 5 s)" };
    if (!pumping_ready()) {
        run_notice = "Wick unavailable: ztm_stepper not ready.";
        return;
    }
    bool ok = mode == 1u ? pumping_wick_prime() : mode == 2u ? pumping_wick_wet()
            : mode == 3u ? pumping_wick_drain() : pumping_wick_stop();
    (void)snprintf(run_result, sizeof(run_result), ok ? "Wick %s." : "Wick command FAILED (%s requested).",
                   names[mode]);
    run_notice = run_result;
}

static void run_apply_fans(unsigned level)
{
    bool ok;
    if (level == 0u) {
        ok = ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN);
        ok = ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_INTAKE_FAN) && ok;
        if (ok) run_fans_off();
        run_notice = ok ? "Fans off: intake and exhaust stopped."
                        : "Fan stop FAILED on one or both fans. Press Space for full STOP.";
        return;
    }
    uint8_t pct = run_fan_pct[level];
    ok = ztm_pwm_fan_set_open_loop(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN, pct);
    ok = ztm_pwm_fan_set_open_loop(&ztm_pwm_1, CIRRUS1_INTAKE_FAN, pct) && ok;
    if (!ok) {
        /* Avoid a pressure differential: stop both if either write failed. */
        (void)ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_EXHAUST_FAN);
        (void)ztm_pwm_fan_stop(&ztm_pwm_1, CIRRUS1_INTAKE_FAN);
        run_fans_off();
        run_notice = "Fan set FAILED; both fans stop requested.";
        return;
    }
    run_fan_level = level;
    run_fan_phase = RUN_FANS_SETTLING;
    run_fan_target = 0;
    run_fan_since = to_ms_since_boot(get_absolute_time());
    (void)snprintf(run_result, sizeof(run_result),
                   "Fans %s: both at %u%% open loop; matching RPM after %u s.",
                   run_fan_levels[level], (unsigned)pct, CIRRUS1_FAN_SETTLE_MS / 1000u);
    run_notice = run_result;
}

/* Sequence building blocks. Each returns false when a write failed. */
static bool run_tec_off(void)
{
    bool ok = true;
    for (uint8_t id = 1u; id <= COOLING_TEC_COUNT; ++id) ok = cooling_tec_stop(id) && ok;
    if (ok) run_tec_percent = 0;
    return ok;
}

static bool run_hv_off(void)
{
    (void)ztm_psu_hv_stop(&ztm_psu_hv_1);
    bool enabled = true;
    bool off = ztm_psu_hv_read_enable(&ztm_psu_hv_1, &enabled) && !enabled;
    if (off) {
        run_hv_percent = 0;
        run_hv_mV = 0;
    }
    return off;
}

/* Shutdown ramp step: no start checks, so a warm chiller cannot hold the TECs
 * at a higher level. Same % to DAC mapping as the t prompt. */
static bool run_tec_ramp_to(unsigned percent)
{
    if (percent == 0u) return run_tec_off();
    cooling_tec_status_t status[COOLING_TEC_COUNT];
    if (!cooling_tec_set_all_raw((uint8_t)((percent * 255u + 50u) / 100u), status)) {
        run_tec_percent = 0; /* the failed write stopped all and revoked permission */
        return false;
    }
    run_tec_percent = (uint8_t)percent;
    return true;
}

static void run_seq_begin(unsigned mode)
{
    run_active = mode;
    run_seq_since = to_ms_since_boot(get_absolute_time());
    run_seq_step_at = run_seq_since;
}

/* 2 Standby: TECs and HV off, filter on, return and wick off, fans low,
 * low lights, glass at its standby level. */
static void run_seq_standby(const char *prefix)
{
    unsigned failed = 0;
    failed += !run_tec_off();
    failed += !run_hv_off();
    failed += !(pumping_ready() && pumping_wick_stop());
    failed += !(pumping_ready() && pumping_sump_stop());
    failed += !(pumping_ready() && pumping_filter_start_normal());
    failed += !ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_LED_LEFT_LOAD, CIRRUS1_SEQ_STANDBY_LED_PCT);
    failed += !ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_LED_RIGHT_LOAD, CIRRUS1_SEQ_STANDBY_LED_PCT);
    failed += !ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_GLASS_HEATER_LOAD, CIRRUS1_SEQ_STANDBY_GLASS_PCT);
    if (run_fan_level != CIRRUS1_SEQ_STANDBY_FAN_LEVEL || run_fan_phase == RUN_FANS_OFF) {
        run_apply_fans(CIRRUS1_SEQ_STANDBY_FAN_LEVEL);
        failed += run_fan_level != CIRRUS1_SEQ_STANDBY_FAN_LEVEL;
    }
    run_seq_begin(2u);
    if (failed)
        (void)snprintf(run_result, sizeof(run_result),
                       "%sStandby incomplete: %u step(s) FAILED. Check the panels or press Space.", prefix, failed);
    else
        (void)snprintf(run_result, sizeof(run_result),
                       "%sStandby: filter on, fans %s, LEDs %u%%, glass %u%%; TECs, HV, wick and return off.",
                       prefix, run_fan_levels[CIRRUS1_SEQ_STANDBY_FAN_LEVEL],
                       CIRRUS1_SEQ_STANDBY_LED_PCT, CIRRUS1_SEQ_STANDBY_GLASS_PCT);
    run_notice = run_result;
}

/* 3 Prime: filter and return on, wick prime; after the prime time, Standby. */
static void run_seq_prime(void)
{
    if (run_tec_percent) {
        run_notice = "Prime BLOCKED while TECs are on. Use Shutdown or set TEC to 0 first.";
        return;
    }
    bool ok = pumping_ready() && pumping_filter_start_normal();
    ok = pumping_ready() && pumping_sump_run() && ok;
    ok = pumping_ready() && pumping_wick_prime() && ok;
    run_seq_begin(3u);
    (void)snprintf(run_result, sizeof(run_result),
                   ok ? "Prime: filter and return on, wick priming; Standby follows in %lu s."
                      : "Prime incomplete: a pump command FAILED (Standby still follows in %lu s). Press Space to stop.",
                   (unsigned long)(CIRRUS1_SEQ_PRIME_MS / 1000u));
    run_notice = run_result;
}

/* 4 Runup: from Standby; wick wetting, filter and return on, low TEC through
 * the normal TEC checks (permit, TEC fans, chiller ready). */
static void run_seq_runup(void)
{
    if (run_active != 2u && run_active != 4u) {
        run_notice = "Runup needs Standby first (press 2 twice).";
        return;
    }
    bool ok = pumping_ready() && pumping_wick_wet();
    ok = pumping_ready() && pumping_filter_start_normal() && ok;
    ok = pumping_ready() && pumping_sump_run() && ok;
    run_apply_tec(CIRRUS1_SEQ_RUNUP_TEC_PCT);
    if (run_tec_percent != CIRRUS1_SEQ_RUNUP_TEC_PCT) {
        /* run_notice already says why the TECs did not start; back to the
         * Standby pumps (wick and return off). */
        if (pumping_ready()) {
            (void)pumping_wick_stop();
            (void)pumping_sump_stop();
        }
        run_seq_begin(2u);
        return;
    }
    run_seq_begin(4u);
    (void)snprintf(run_result, sizeof(run_result),
                   "%sRunup: wick wetting, TEC %u%%; ready when plate B <= %.0f" RUN_DEG "C. TEC direction UNCONFIRMED: watch B.",
                   ok ? "" : "Pump command FAILED. ", CIRRUS1_SEQ_RUNUP_TEC_PCT, CIRRUS1_SEQ_RUNUP_READY_C);
    run_notice = run_result;
}

/* 6 Shutdown: HV and glass off, wick drain, filter and return on, TECs ramp
 * down in steps; lights and fans are left as they are. */
static void run_seq_shutdown(void)
{
    unsigned failed = 0;
    failed += !run_hv_off();
    failed += !ztm_pwm_channel_set(&ztm_pwm_1, CIRRUS1_GLASS_HEATER_LOAD, 0u);
    failed += !(pumping_ready() && pumping_wick_drain());
    failed += !(pumping_ready() && pumping_filter_start_normal());
    failed += !(pumping_ready() && pumping_sump_run());
    run_seq_begin(6u);
    if (failed)
        (void)snprintf(run_result, sizeof(run_result),
                       "Shutdown: %u step(s) FAILED; TEC ramp continues. Press Space for full STOP.", failed);
    else
        (void)snprintf(run_result, sizeof(run_result),
                       "Shutdown: HV and glass off, wick draining; TECs ramp down %u%% every %u s; lights kept.",
                       CIRRUS1_SEQ_RAMP_STEP_PCT, CIRRUS1_SEQ_RAMP_STEP_MS / 1000u);
    run_notice = run_result;
}

static void run_sequence_tick(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (run_active == 3u && (uint32_t)(now - run_seq_since) >= CIRRUS1_SEQ_PRIME_MS) {
        run_seq_standby("Prime complete. ");
    } else if (run_active == 6u && run_tec_percent &&
               (uint32_t)(now - run_seq_step_at) >= CIRRUS1_SEQ_RAMP_STEP_MS) {
        run_seq_step_at = now;
        unsigned next = run_tec_percent > CIRRUS1_SEQ_RAMP_STEP_PCT ? run_tec_percent - CIRRUS1_SEQ_RAMP_STEP_PCT : 0u;
        bool ok = run_tec_ramp_to(next);
        if (!ok)
            run_notice = "Shutdown ramp: TEC write FAILED; all-off attempted. Press Space for full STOP.";
        else if (!next)
            run_notice = "Shutdown complete: TECs off (permission kept); lights, fans, filter and return running.";
        else {
            (void)snprintf(run_result, sizeof(run_result), "Shutdown: TEC ramped to %u%%.", next);
            run_notice = run_result;
        }
    }
}

static void run_mode_key(unsigned mode)
{
    static const char *const names[] = { "", "Tests", "Standby", "Prime", "Runup", "Go", "Shutdown" };
    if (mode == 1u) {
        run_notice = "Tests unavailable: ZapLink checks not connected; no all-systems-go claim.";
        return;
    }
    if (mode == 5u) {
        run_notice = "Go LOCKED: running targets (TEC, glass, HV, lights) are not set yet.";
        return;
    }
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (run_armed != mode || (uint32_t)(now - run_armed_at) > CIRRUS1_SEQ_CONFIRM_MS) {
        run_armed = mode;
        run_armed_at = now;
        (void)snprintf(run_result, sizeof(run_result), "Press %u again within %u s to start %s.",
                       mode, CIRRUS1_SEQ_CONFIRM_MS / 1000u, names[mode]);
        run_notice = run_result;
        return;
    }
    run_armed = 0;
    switch (mode) {
        case 2u: run_seq_standby(""); break;
        case 3u: run_seq_prime(); break;
        case 4u: run_seq_runup(); break;
        case 6u: run_seq_shutdown(); break;
        default: break;
    }
}

static unsigned run_prompt_value(void)
{
    unsigned value = 0;
    for (const char *c = run_prompt_text; *c; ++c) value = value * 10u + (unsigned)(*c - '0');
    return value;
}

static void run_prompt_apply(void)
{
    char prompt = run_prompt->key;
    run_prompt = NULL;
    if (!run_prompt_len) {
        run_notice = "No value entered; outputs unchanged.";
        return;
    }
    unsigned value = run_prompt_value();
    switch (prompt) {
        case 't': run_apply_tec(value); break;
        case 'h': run_apply_hv(value); break;
        case 'l': run_apply_led(CIRRUS1_LED_LEFT_LOAD, "LED LEFT", value); break;
        case 'r': run_apply_led(CIRRUS1_LED_RIGHT_LOAD, "LED RIGHT", value); break;
        case 'g': run_apply_led(CIRRUS1_GLASS_HEATER_LOAD, "Glass heater", value); break;
        case 'c': run_apply_tec_fans(value); break;
        case 'f': run_apply_fans(value); break;
        case 'p': run_apply_pumps(value); break;
        case 'w': run_apply_wick(value); break;
        default: break;
    }
}

static void run_prompt_key(uint8_t byte)
{
    if (byte == '\r' || byte == '\n') {
        run_prompt_apply();
    } else if (byte == 8u || byte == 127u) {
        if (run_prompt_len) run_prompt_text[--run_prompt_len] = '\0';
    } else if (byte >= '0' && byte <= '9' && run_prompt_len < 3u) {
        run_prompt_text[run_prompt_len] = (char)byte;
        run_prompt_text[run_prompt_len + 1u] = '\0';
        if (run_prompt_value() <= run_prompt->max) ++run_prompt_len;
        else run_prompt_text[run_prompt_len] = '\0';
    }
}

void cli_run_key(uint8_t byte)
{
    if (!run_visible) return;
    /* Swallow the rest of an arrow/function-key sequence without acting,
     * except F5, which clears and redraws the whole screen. */
    if (run_escape_skip) {
        if (run_escape_skip == 2u) {
            run_escape_skip = 0;
        } else if (byte >= 0x40u && byte <= 0x7Eu) {
            run_escape_skip = 0;
            run_csi[run_csi_len] = '\0';
            if (byte == '~' && strcmp(run_csi, "15") == 0) {
                run_screen_valid = false;
                run_draw();
            }
        } else if (run_csi_len < sizeof(run_csi) - 1u) {
            run_csi[run_csi_len++] = (char)byte;
        }
        return;
    }
    /* Bare ESC is delayed so it can be told apart from ESC-prefixed sequences. */
    if (run_escape_pending) {
        run_escape_pending = false;
        if (byte == '[') { run_escape_skip = 1; run_csi_len = 0; return; }
        if (byte == 'O') { run_escape_skip = 2; return; }
        /* ESC + other byte (e.g. Alt+key): drop the ESC, handle the byte. */
    } else if (byte == 27u) {
        run_escape_pending = true;
        run_escape_at = to_ms_since_boot(get_absolute_time());
        return;
    }
    run_leave_armed = false; /* any key other than a second ESC cancels leaving */
    if (byte == ' ' || (byte == '7' && !run_prompt)) {
        run_stop();
    } else if (run_prompt) {
        run_prompt_key(byte);
    } else {
        char key = (byte >= 'A' && byte <= 'Z') ? (char)(byte - 'A' + 'a') : (char)byte;
        const run_prompt_def_t *def = NULL;
        for (unsigned i = 0; i < RUN_PROMPT_COUNT; ++i)
            if (run_prompt_defs[i].key == key) def = &run_prompt_defs[i];
        if (def) {
            run_prompt = def;
            run_prompt_len = 0;
            run_prompt_text[0] = '\0';
            (void)snprintf(run_result, sizeof(run_result), def->hint, def->max);
            run_notice = run_result;
        } else if (byte >= '1' && byte <= '6') {
            run_mode_key((unsigned)(byte - '0'));
        } else if (byte > ' ' && byte < 127u) {
            run_notice = "Key ignored. Space or 7 = STOP; t h l r g c f p w = manual prompts.";
        }
        /* Enter, NUL and other control bytes are ignored silently. */
    }
    run_draw();
}

/* A safe typed at the normal prompt (or over HTTP) stops everything; mirror
 * it so no Run sequence or ramp turns outputs back on afterwards. */
static void run_sync_safe(void)
{
    if (run_safe_seen == cmd_safe_requests) return;
    run_safe_seen = cmd_safe_requests;
    run_active = 7;
    run_armed = 0;
    run_tec_percent = 0;
    run_hv_percent = 0;
    run_hv_mV = 0;
    run_fans_off();
    run_notice = "STOP: safe was requested outside Run; sequences cancelled.";
}

void cli_run_tick(void)
{
    if (!run_visible) return;
    run_sync_safe();
    if (run_escape_pending &&
        (uint32_t)(to_ms_since_boot(get_absolute_time()) - run_escape_at) >= 200u) {
        run_escape_pending = false;
        if (run_prompt) {
            /* ESC in a prompt cancels it and stays in Run. */
            run_prompt = NULL;
            run_notice = "Prompt cancelled; outputs unchanged.";
            run_draw();
            return;
        }
        /* Outputs keep running after Run is left (cli_run_background keeps
         * protection and sequences going), so ask first. */
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool busy = run_busy();
        if (busy && !(run_leave_armed && (uint32_t)(now - run_leave_at) <= CIRRUS1_SEQ_CONFIRM_MS)) {
            run_leave_armed = true;
            run_leave_at = now;
            (void)snprintf(run_result, sizeof(run_result),
                           "Leave Run with TEC %u%%, HV %u%% and mode %u still running? Press ESC again within %u s to leave.",
                           (unsigned)run_tec_percent, (unsigned)run_hv_percent, run_active,
                           CIRRUS1_SEQ_CONFIRM_MS / 1000u);
            run_notice = run_result;
            run_draw();
            return;
        }
        run_leave_armed = false;
        run_visible = false;
        console_screen_active = false;
        run_screen_valid = false;
        (void)zap_pico_network_set_raw(false);
        wifi_write(RUN_RESET "\033[?25h\033[?1049l");
        if (busy)
            wifi_write("\r\nLeft Run with outputs running. TEC protection, fan stall checks and the active"
                       "\r\nsequence continue in the background. Type run to return, safe to stop all.");
        wifi_write("\r\ncirrus1> ");
        return;
    }
    run_sensor_poll();
    run_fan_poll();
    run_vsense_poll();
    run_protect();
    run_sequence_tick();
    run_draw();
}

void cli_run_closed(void)
{
    /* Outputs keep running; cli_run_background keeps protecting them. */
    run_leave_armed = false;
    run_visible = false;
    console_screen_active = false;
    run_screen_valid = false;
    run_escape_pending = false;
    run_escape_skip = 0;
    run_prompt = NULL;
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
    run_escape_skip = 0;
    run_prompt = NULL;
    run_armed = 0;
    run_leave_armed = false;
    /* Mode, outputs, min..max and the last notice (including any trip while
     * Run was off screen) are kept from the previous visit. */
    if (!run_notice) run_notice = "No sequence selected; existing outputs unchanged.";
    run_sync_safe();
    wifi_write("\033[?1049h\033[?25l");
    return 0;
}

/* Called from the main loop. While Run is off screen, keep TEC protection,
 * fan stall checks and sequence timing going for outputs Run left running,
 * and report any change (trip, stall, step) on the normal console. */
void cli_run_background(void)
{
    if (run_visible || !run_notice) return;
    bool fans = run_fan_phase != RUN_FANS_OFF;
    if (!run_busy() && !fans) return;
    /* Commands, Run keys and network writes run in the lwIP background
     * context; hold its lock so this main-loop work never interleaves with
     * them on the ADC/PWM buses or the TCP connection. */
    cyw43_arch_lwip_begin();
    if (!run_visible) {
        run_sync_safe();
        uint32_t before = run_hash(run_notice);
        if (run_tec_percent || run_active == 3u || run_active == 4u || run_active == 6u) run_sensor_poll();
        if (run_fan_phase != RUN_FANS_OFF) run_fan_poll();
        run_protect();
        run_sequence_tick();
        if (run_hash(run_notice) != before) {
            wifi_write("\r\n[run] ");
            wifi_write(run_notice);
            wifi_write("\r\ncirrus1> ");
        }
    }
    cyw43_arch_lwip_end();
}
