/*
 * Debug output: PS4 pop-up notifications (rare, event based) and a log file
 * (/data/GoldHEN/xbox_series.log) that records what the plugin is doing.
 *
 * Use the macros:
 *     XBS_LOG("scePadOpen user=0x%x -> %d", user, ret);
 *     XBS_NOTIFY("Xbox Series #%d connected", n);
 *     XBS_LOG_LIMITED(counter, 40, "...");   // only the first 40 times
 */

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdio.h>

void xbs_log_init(void);
void xbs_log_close(void);

/* Low-level functions: use the macros below instead. */
void xbs_log_str(const char* line);
void xbs_notify_str(const char* message);

#define XBS_LOG(...) do { \
        char xbs_tmp_[200]; \
        snprintf(xbs_tmp_, sizeof(xbs_tmp_), __VA_ARGS__); \
        xbs_log_str(xbs_tmp_); \
    } while (0)

#define XBS_NOTIFY(...) do { \
        char xbs_ntmp_[120]; \
        snprintf(xbs_ntmp_, sizeof(xbs_ntmp_), __VA_ARGS__); \
        xbs_log_str(xbs_ntmp_); \
        xbs_notify_str(xbs_ntmp_); \
    } while (0)

/* Logs only while 'counter' (a static int) is below 'limit'. */
#define XBS_LOG_LIMITED(counter, limit, ...) do { \
        if (__atomic_load_n(&(counter), __ATOMIC_RELAXED) < (limit)) { \
            __atomic_fetch_add(&(counter), 1, __ATOMIC_RELAXED); \
            XBS_LOG(__VA_ARGS__); \
        } \
    } while (0)

#endif /* DEBUG_LOG_H */
