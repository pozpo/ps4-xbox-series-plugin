/*
 * Xbox Series X|S Controller Plugin for PS4 (GoldHEN)
 *
 * Entry points called by GoldHEN when the plugin is loaded / unloaded.
 *
 * plugin_load:
 *   0. if this process' title ID is in exclude_titles: return at once
 *      (see below) - nothing else is touched
 *   1. read /data/GoldHEN/xbox_series.ini (if present)
 *   2. start the log file
 *   3. prepare the virtual pad manager
 *   4. load + initialise the PS4 USB library (not fatal if it fails)
 *   5. install the pad hooks (fatal if it fails)
 * The USB scanning and controller threads start later, on the first pad call
 * made by the game (see hooks.c).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "debug_log.h"
#include "hooks.h"
#include "process_filter.h"
#include "usb_manager.h"
#include "virtual_pad.h"

#include <orbis/libkernel.h>
#include <GoldHEN.h>

#define attr_public __attribute__((visibility("default")))

attr_public const char *g_pluginName = "xbox_series";
attr_public const char *g_pluginDesc = "Xbox Series X|S wired controller support";
attr_public const char *g_pluginAuth = "xbox_series_plugin";
attr_public uint32_t g_pluginVersion = 0x00000100;

/* 1 while this process is excluded: plugin_unload must then do nothing too. */
static int g_excluded = 0;

int32_t attr_public plugin_load(int32_t argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    g_excluded = 0;

    /* Early process exclusion. Everything below this block (log file, virtual
     * pads, USB, library loads, code patches, hooks, threads, pop-ups) is
     * skipped for excluded title IDs. The only things done before the check
     * are reading the title ID and reading the .ini (read-only, to get the
     * exclusion list). If the title ID cannot be read, the plugin behaves
     * exactly as before (normal start-up). */
    char titleid[16];
    int have_title = xbs_process_get_title_id(titleid, sizeof(titleid));

    settings_load();

    if (have_title && xbs_title_excluded(titleid, g_settings.exclude_titles)) {
        g_excluded = 1;
        return 0;
    }

    xbs_log_init();

    if (!g_settings.enabled) {
        XBS_LOG("disabled in %s (enabled=0): doing nothing", XBS_CONFIG_PATH);
        return 0;
    }

    vpad_init();

    /* USB problems are not fatal: the plugin then simply has no Xbox pads. */
    if (usbm_init() < 0) {
        XBS_NOTIFY("Xbox: USB init failed");
    }

    if (hooks_install() < 0) {
        XBS_NOTIFY("Xbox: Hook install failed");
        return -1;      /* tell GoldHEN to unload us */
    }

    XBS_NOTIFY("Xbox Series plugin %s loaded", XBS_VERSION_STR);
    return 0;
}

int32_t attr_public plugin_unload(int32_t argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    if (g_excluded) return 0;     /* nothing was started, nothing to stop */

    hooks_remove();       /* stop game calls entering our code first */
    usbm_stop();          /* then stop threads and release USB       */
    vpad_shutdown();
    XBS_LOG("plugin unloaded");
    xbs_log_close();
    return 0;
}

int module_start(size_t argc, const void* argv) {
    (void)argc;
    (void)argv;
    return 0;
}

int module_stop(size_t argc, const void* argv) {
    (void)argc;
    (void)argv;
    return 0;
}
