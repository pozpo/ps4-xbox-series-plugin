/*
 * Debug output implementation. See debug_log.h.
 *
 * Notifications are rate limited (max 16 per 10 seconds) so a bug can never
 * flood the PS4 screen. A normal start-up with four controllers produces at
 * most about 13 messages (loaded + 4 x connected / input OK / assigned).
 * Everything, including dropped messages, is still written to the log file.
 */

#include "debug_log.h"
#include "config.h"
#include "xbs_features.h"

#include <string.h>
#include <stdint.h>

#include <orbis/libkernel.h>
#include <orbis/_types/pthread.h>

#define XBS_LOG_MAX_BYTES   (256 * 1024)

static OrbisPthreadMutex g_log_mutex;
static int g_log_mutex_ready = 0;
static int g_log_fd = -1;
static int g_log_bytes = 0;

static uint64_t g_notify_window_start = 0;
static int g_notify_in_window = 0;

void xbs_log_init(void) {
    g_notify_window_start = 0;
    g_notify_in_window = 0;
    if (!g_log_mutex_ready) {
        if (scePthreadMutexInit(&g_log_mutex, 0, "xbsLogMtx") >= 0)
            g_log_mutex_ready = 1;
    }

#if XBS_HAVE_KFILE
    if (g_settings.log_to_file && g_log_fd < 0) {
        /* write-only | create | truncate  (FreeBSD flag values) */
        g_log_fd = sceKernelOpen(XBS_LOG_PATH, 0x0001 | 0x0200 | 0x0400, 0666);
        g_log_bytes = 0;
        if (g_log_fd >= 0) {
            XBS_LOG("xbox_series plugin %s log started", XBS_VERSION_STR);
        }
    }
#endif
}

void xbs_log_close(void) {
#if XBS_HAVE_KFILE
    if (g_log_fd >= 0) {
        if (g_log_mutex_ready) scePthreadMutexLock(&g_log_mutex);
        sceKernelClose(g_log_fd);
        g_log_fd = -1;
        if (g_log_mutex_ready) scePthreadMutexUnlock(&g_log_mutex);
    }
#endif
}

void xbs_log_str(const char* line) {
#if XBS_HAVE_KFILE
    if (g_log_fd < 0 || !line || !g_log_mutex_ready) return;

    char out[256];
    uint64_t t = sceKernelGetProcessTime();
    int n = snprintf(out, sizeof(out), "[%llu.%03llu] %s\n",
                     (unsigned long long)(t / 1000000ULL),
                     (unsigned long long)((t / 1000ULL) % 1000ULL), line);
    if (n <= 0) return;
    if (n >= (int)sizeof(out)) n = (int)sizeof(out) - 1;

    scePthreadMutexLock(&g_log_mutex);
    if (g_log_fd >= 0 && g_log_bytes + n < XBS_LOG_MAX_BYTES) {
        sceKernelWrite(g_log_fd, out, (size_t)n);
        g_log_bytes += n;
    }
    scePthreadMutexUnlock(&g_log_mutex);
#else
    (void)line;
#endif
}

void xbs_notify_str(const char* message) {
    if (!message || !g_settings.notifications) return;

    /* rate limit (several threads may notify at once, so take the lock) */
    uint64_t now = sceKernelGetProcessTime();
    int allowed;
    if (g_log_mutex_ready) scePthreadMutexLock(&g_log_mutex);
    if (g_notify_window_start == 0 || now - g_notify_window_start > 10000000ULL) {
        g_notify_window_start = now ? now : 1;
        g_notify_in_window = 0;
    }
    allowed = (g_notify_in_window < 16);
    if (allowed) g_notify_in_window++;
    if (g_log_mutex_ready) scePthreadMutexUnlock(&g_log_mutex);
    if (!allowed) return;

    OrbisNotificationRequest req;
    memset(&req, 0, sizeof(req));
    req.type = NotificationRequest;
    req.targetId = -1;
    strncpy(req.message, message, sizeof(req.message) - 1);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}
