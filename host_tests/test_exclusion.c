/*
 * Integration test for the early title-ID exclusion in plugin_load().
 *
 * It runs the REAL main.c / process_filter.c / settings.c / hooks.c /
 * usb_manager.c / virtual_pad.c / debug_log.c against the fake platform and
 * counts every side effect at the platform boundary (log file open/truncate,
 * mutex init, library load, sceUsbdInit, thread creation, code patch, hook,
 * pop-up). For an excluded title ALL of them must stay at zero.
 *
 * This proves the LOGIC only. It cannot prove how a real PS4 or the real
 * homebrew apps behave.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "usb_manager.h"
#include "fake_platform.h"

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/_types/errors.h>

int32_t plugin_load(int32_t argc, const char* argv[]);
int32_t plugin_unload(int32_t argc, const char* argv[]);

/* the two "real library" functions hooks.c links against */
int32_t scePadReadStateExt(int32_t h, OrbisPadData* d) { (void)h; (void)d; return 0; }
int32_t scePadReadExt(int32_t h, OrbisPadData* d, int32_t n) { (void)h; (void)d; (void)n; return 0; }
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList* l) { (void)l; return 0; }
int32_t sceUserServiceGetForegroundUser(int32_t* u) { *u = 0; return 0; }

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define LOGFILE "/tmp/xbs_host/xbox_series.log"
#define INIFILE "/tmp/xbs_host/xbox_series.ini"
#define SENTINEL "SENTINEL: must stay untouched by an excluded process\n"
#define N_HOOKS 15      /* 5 original + 10 additional, all enabled in the host build */

static void write_file(const char* path, const char* text) {
    FILE* f = fopen(path, "w");
    if (f) { fputs(text, f); fclose(f); }
}
static int file_equals(const char* path, const char* text) {
    char buf[512] = {0};
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strcmp(buf, text) == 0;
}
static int file_contains(const char* path, const char* needle) {
    char buf[2048] = {0};
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    return strstr(buf, needle) != NULL;
}

static void fresh(const char* title, const char* ini_text) {
    host_reset_notes();
    host_reset_counters();
    host_set_title(title);
    write_file(LOGFILE, SENTINEL);
    if (ini_text) write_file(INIFILE, ini_text); else unlink(INIFILE);
}

/* A load that must have done NOTHING. */
static void expect_inert(const char* title, const char* ini_text, const char* what) {
    libusb_device* xbox = fake_usb_plug(0x045E, 0x0B12);     /* an Xbox pad is plugged in */
    fresh(title, ini_text);
    int r = plugin_load(0, NULL);
    CHECK(r == 0, "%s: plugin_load returns 0 (stays loaded, inert), got %d", what, r);
    CHECK(host_sdk_proc_info_calls == 1, "%s: title ID asked exactly once", what);
    CHECK(host_log_opens == 0, "%s: no log file created/truncated", what);
    CHECK(file_equals(LOGFILE, SENTINEL), "%s: existing log file untouched", what);
    CHECK(host_mutex_inits == 0, "%s: no mutex (log / vpad / usb init)", what);
    CHECK(host_dynlib_calls == 0, "%s: no libScePad / libSceUserService / libSceUsbd load", what);
    CHECK(host_usbd_init_calls == 0, "%s: sceUsbdInit not called", what);
    CHECK(host_patch_calls == 0, "%s: no 5-byte patches", what);
    CHECK(host_detour_calls == 0, "%s: no scePad hooks", what);
    CHECK(host_thread_creates == 0, "%s: no threads", what);
    CHECK(host_note_count == 0, "%s: no pop-up notification", what);
    CHECK(host_counters_total() == 0, "%s: total side effects == 0 (got %d)", what, host_counters_total());
    usleep(50 * 1000);
    CHECK(usbm_present_count() == 0, "%s: no USB scanning (plugged Xbox pad not picked up)", what);
    r = plugin_unload(0, NULL);
    CHECK(r == 0 && host_counters_total() == 0 && host_note_count == 0 && file_equals(LOGFILE, SENTINEL),
          "%s: plugin_unload is inert as well", what);
    fake_usb_unplug(xbox);
    fake_usb_reset();
}

/* A load that must run the normal start-up path. */
static void expect_normal(const char* title, const char* ini_text, const char* what) {
    fresh(title, ini_text);
    int r = plugin_load(0, NULL);
    CHECK(r == 0, "%s: plugin_load returns 0, got %d", what, r);
    CHECK(host_log_opens == 1, "%s: log file opened (truncated) as before", what);
    CHECK(!file_equals(LOGFILE, SENTINEL) && file_contains(LOGFILE, "log started"), "%s: log file rewritten", what);
    CHECK(host_mutex_inits >= 1, "%s: vpad / usb / log mutexes created", what);
    CHECK(host_dynlib_calls == 3, "%s: libSceUsbd + libScePad + libSceUserService loaded (got %d)", what, host_dynlib_calls);
    CHECK(host_usbd_init_calls == 1, "%s: sceUsbdInit called once", what);
    CHECK(host_patch_calls == 2, "%s: both 5-byte patches installed (got %d)", what, host_patch_calls);
    CHECK(host_detour_calls == N_HOOKS, "%s: %d scePad hooks installed (got %d)", what, N_HOOKS, host_detour_calls);
    CHECK(host_count_notes("plugin 1.0.0 loaded") == 1, "%s: 'loaded' pop-up shown", what);
    CHECK(host_thread_creates == 0, "%s: USB threads still lazy (start on first pad call)", what);
    plugin_unload(0, NULL);
    fake_usb_reset();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    mkdir("/tmp/xbs_host", 0777);

    printf("[A] excluded titles with the default list: zero side effects\n");
    expect_inert("FLTZ00003", NULL, "FLTZ00003 (Remote Package Installer)");
    expect_inert("RMTC00001", NULL, "RMTC00001 (ezRemote Client)");
    expect_inert("rmtc00001", NULL, "rmtc00001 (lower case)");

    printf("[B] normal title: the full start-up path still happens\n");
    expect_normal("CUSA00000", NULL, "CUSA00000 (normal game)");
    expect_normal("CUSA03694", NULL, "CUSA03694 (another game)");

    printf("[C] title ID unavailable: behaves exactly like before (normal start-up)\n");
    expect_normal(NULL, NULL, "title ID unavailable");

    printf("[D] excluded after a normal load in the same process image (reload)\n");
    expect_inert("RMTC00001", NULL, "RMTC00001 after a normal load");
    expect_normal("CUSA00000", NULL, "normal again after an excluded load");

    printf("[E] configuration through xbox_series.ini\n");
    expect_inert("CUSA00000", "exclude_titles=CUSA00000\n", "ini: CUSA00000 excluded");
    expect_normal("FLTZ00003", "exclude_titles=CUSA00000\n", "ini replaces defaults: FLTZ00003 no longer excluded");
    expect_inert("RMTC00001", "exclude_titles = fltz00003 ,  RMTC00001   # my apps\n", "ini with spaces, lower case, comment");
    expect_inert("FLTZ00003", "exclude_titles = fltz00003 ,  RMTC00001   # my apps\n", "ini with spaces, lower case, comment (2)");
    expect_normal("FLTZ00003", "exclude_titles=\n", "ini: empty list excludes nothing");
    expect_inert("FLTZ00003", "stick_deadzone=20\nenabled=1\n", "ini without exclude_titles keeps defaults");
    expect_inert("RMTC00001", "exclude_titles=,,, ,FLTZ00003,,RMTC00001,,\nenabled=0\n", "malformed list + enabled=0 still inert");

    printf("[F] enabled=0 on a normal title still means 'does nothing' (existing behaviour)\n");
    fresh("CUSA00000", "enabled=0\n");
    plugin_load(0, NULL);
    CHECK(host_patch_calls == 0 && host_detour_calls == 0 && host_usbd_init_calls == 0 && host_thread_creates == 0 && host_dynlib_calls == 0,
          "enabled=0: no patches/hooks/usb");
    plugin_unload(0, NULL);

    unlink(INIFILE);
    printf("\ntest_exclusion: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
