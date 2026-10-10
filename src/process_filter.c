/*
 * Process filter implementation. See process_filter.h.
 */

#include "process_filter.h"

#include <stddef.h>
#include <stdint.h>   /* GoldHEN.h does not include it itself (uint32_t / uint64_t) */
#include <string.h>

#include <GoldHEN.h>      /* struct proc_info, sys_sdk_proc_info() */

int xbs_process_get_title_id(char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;

    struct proc_info info;
    memset(&info, 0, sizeof(info));

    /* The return value is deliberately not relied on: the struct was zeroed,
     * so if the call failed titleid stays empty and we report "unavailable". */
    (void)sys_sdk_proc_info(&info);

    size_t n = 0;
    while (n < sizeof(info.titleid) && info.titleid[n] != 0 && n + 1 < cap) {
        out[n] = info.titleid[n];
        n++;
    }
    out[n] = 0;
    return n > 0 ? 1 : 0;
}

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
static int is_sep(char c) { return c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int xbs_title_excluded(const char* titleid, const char* list) {
    if (!titleid || titleid[0] == 0 || !list) return 0;

    size_t tl = strlen(titleid);
    const char* p = list;
    while (*p) {
        while (*p && is_sep(*p)) p++;          /* skip separators / empty entries */
        if (!*p) break;
        const char* start = p;
        while (*p && !is_sep(*p)) p++;
        size_t len = (size_t)(p - start);
        if (len != tl) continue;
        size_t i = 0;
        while (i < len && lc(start[i]) == lc(titleid[i])) i++;
        if (i == len) return 1;
    }
    return 0;
}
