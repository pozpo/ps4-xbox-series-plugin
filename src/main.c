/*
 * Xbox Series X|S Controller Plugin for PS4 (GoldHEN)
 *
 * Entry points called by GoldHEN when the plugin is loaded / unloaded.
 *
 * plugin_load:
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
#include "usb_manager.h"
#include "virtual_pad.h"

#include <orbis/libkernel.h>
#include <GoldHEN.h>

#define attr_public __attribute__((visibility("default")))

attr_public const char *g_pluginName = "xbox_series";
attr_public const char *g_pluginDesc = "Xbox Series X|S wired controller support";
attr_public const char *g_pluginAuth = "xbox_series_plugin";
attr_public uint32_t g_pluginVersion = 0x00000100;

int32_t attr_public plugin_load(int32_t argc, const char* argv[]) {
    (void)argc;
    (void)argv;

    settings_load();
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
