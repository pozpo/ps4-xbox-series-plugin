/*
 * Run-time settings: defaults + a tiny parser for /data/GoldHEN/xbox_series.ini
 *
 * File format (one setting per line, '#' or ';' starts a comment):
 *     stick_deadzone=12
 *     view_button=share
 *     xbox_players=2,3
 */

#include "config.h"
#include "xbs_features.h"
#include <string.h>
#include <stdint.h>

#if XBS_HAVE_KFILE
#include <orbis/libkernel.h>
#endif

XbsSettings g_settings;

/* Bounded copy into a fixed buffer (always NUL-terminated). */
static void set_str(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    if (cap == 0) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void settings_set_defaults(XbsSettings* s) {
    if (!s) return;
    s->enabled = 1;
    s->stick_deadzone_pct = 12;
    s->trigger_threshold_pct = 12;
    s->view_to_touchpad = 0;
    s->allow_foreground_user = 0;
    s->player_mask = 0;
    s->guide_ack = 1;
    s->notifications = 1;
    s->log_to_file = 1;
    s->log_verbose = 1;
    set_str(s->exclude_titles, sizeof(s->exclude_titles), XBS_DEFAULT_EXCLUDE_TITLES);
}

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* Parses an optional-sign decimal number. Returns 1 if at least one digit. */
static int parse_int(const char* p, int* out) {
    int neg = 0, any = 0;
    long v = 0;
    while (is_space(*p)) p++;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (*p - '0');
        if (v > 100000) v = 100000;
        any = 1;
        p++;
    }
    if (!any) return 0;
    *out = (int)(neg ? -v : v);
    return 1;
}

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int streq(const char* a, const char* b) {
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static void apply(XbsSettings* s, const char* key, const char* val) {
    int n;
    if (streq(key, "enabled")) {
        if (parse_int(val, &n)) s->enabled = n ? 1 : 0;
    } else if (streq(key, "stick_deadzone")) {
        if (parse_int(val, &n)) s->stick_deadzone_pct = clampi(n, 0, 40);
    } else if (streq(key, "trigger_threshold")) {
        if (parse_int(val, &n)) s->trigger_threshold_pct = clampi(n, 1, 100);
    } else if (streq(key, "view_button")) {
        if (streq(val, "touchpad")) s->view_to_touchpad = 1;
        else if (streq(val, "share")) s->view_to_touchpad = 0;
    } else if (streq(key, "allow_player1")) {
        if (parse_int(val, &n)) s->allow_foreground_user = n ? 1 : 0;
    } else if (streq(key, "xbox_players")) {
        /* e.g. "2,3" -> login positions 2 and 3 may receive Xbox pads */
        uint32_t mask = 0;
        const char* p = val;
        while (*p) {
            if (*p >= '1' && *p <= '4') mask |= (1u << (*p - '0'));
            p++;
        }
        s->player_mask = mask;
    } else if (streq(key, "guide_ack")) {
        if (parse_int(val, &n)) s->guide_ack = n ? 1 : 0;
    } else if (streq(key, "notifications")) {
        if (parse_int(val, &n)) s->notifications = n ? 1 : 0;
    } else if (streq(key, "log_to_file")) {
        if (parse_int(val, &n)) s->log_to_file = n ? 1 : 0;
    } else if (streq(key, "log_verbose")) {
        if (parse_int(val, &n)) s->log_verbose = n ? 1 : 0;
    } else if (streq(key, "exclude_titles")) {
        /* replaces the whole list; an empty value means "exclude nothing" */
        set_str(s->exclude_titles, sizeof(s->exclude_titles), val);
    }
    /* unknown keys are silently ignored */
}

void settings_parse_text(XbsSettings* s, const char* text) {
    if (!s || !text) return;

    char line[160];
    const char* p = text;
    while (*p) {
        /* copy one line */
        size_t n = 0;
        while (*p && *p != '\n') {
            if (n < sizeof(line) - 1) line[n++] = *p;
            p++;
        }
        if (*p == '\n') p++;
        line[n] = 0;

        /* trim leading space */
        char* l = line;
        while (is_space(*l)) l++;
        if (*l == 0 || *l == '#' || *l == ';' || *l == '[') continue;

        char* eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char* key = l;
        char* val = eq + 1;

        /* trim key end */
        size_t kl = strlen(key);
        while (kl > 0 && is_space(key[kl - 1])) key[--kl] = 0;

        /* cut trailing comment and spaces from value */
        char* c = val;
        while (*c) {
            if (*c == '#' || *c == ';') { *c = 0; break; }
            c++;
        }
        while (is_space(*val)) val++;
        size_t vl = strlen(val);
        while (vl > 0 && is_space(val[vl - 1])) val[--vl] = 0;

        apply(s, key, val);
    }
}

void settings_load(void) {
    settings_set_defaults(&g_settings);

#if XBS_HAVE_KFILE
    int fd = sceKernelOpen(XBS_CONFIG_PATH, 0 /* read only */, 0);
    if (fd < 0) return;                       /* no file: keep defaults */

    char buf[2048];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int r = (int)sceKernelRead(fd, buf + total, sizeof(buf) - 1 - total);
        if (r <= 0) break;
        total += r;
    }
    sceKernelClose(fd);
    buf[total] = 0;
    settings_parse_text(&g_settings, buf);
#endif
}
