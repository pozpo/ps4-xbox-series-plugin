/*
 * Process filter: decides, at the very start of plugin_load, whether the
 * plugin must stay completely inert in the current PS4 application.
 *
 * Why: the plugin is loaded for every process (plugins.ini [default]). Some
 * homebrew apps (e.g. ezRemote Client RMTC00001, Remote Package Installer
 * FLTZ00003) crash when the plugin patches / hooks the pad library, so for
 * those title IDs the plugin must do nothing at all.
 */

#ifndef XBS_PROCESS_FILTER_H
#define XBS_PROCESS_FILTER_H

#include <stddef.h>

/* Reads the title ID of the CURRENT process through the GoldHEN SDK
 * (sys_sdk_proc_info). Copies it (NUL-terminated) into out[cap].
 * Returns 1 on success, 0 if the title ID is unavailable (out is then ""). */
int xbs_process_get_title_id(char* out, size_t cap);

/* Pure function (no SDK, no side effects): returns 1 if titleid appears in
 * list. list = title IDs separated by commas and/or spaces, e.g.
 * "FLTZ00003,RMTC00001". Comparison is case-insensitive and exact (the whole
 * title ID must match). NULL / empty arguments and empty entries are ignored
 * (result 0). */
int xbs_title_excluded(const char* titleid, const char* list);

#endif /* XBS_PROCESS_FILTER_H */
