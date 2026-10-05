/*
 * Xbox Series X|S Controller Plugin for PS4 (GoldHEN)
 * Compile-time constants and the run-time settings structure.
 *
 * Everything in this file is a plain number or a simple structure.
 * You normally do NOT need to edit this file - run-time options live in
 * /data/GoldHEN/xbox_series.ini on the PS4 (see GUIDE.md).
 */

#ifndef XBS_CONFIG_H
#define XBS_CONFIG_H

#include <stdint.h>

#define XBS_VERSION_STR          "1.0.0"

/* ---- USB identification (Xbox Series X|S controller, wired) ----------- */
/* VID 0x045E = Microsoft. PID 0x0B12 = Xbox Series X|S controller (USB).  */
#define XBS_VID_MICROSOFT        0x045E
#define XBS_PID_SERIES_USB       0x0B12

/* Endpoints / interface used by the existing, known-working code.         */
#define XBS_EP_IN                0x82   /* controller -> PS4 (input)       */
#define XBS_EP_OUT               0x02   /* PS4 -> controller (commands)    */
#define XBS_INTERFACE            0

/* ---- Limits ------------------------------------------------------------ */
/* How many Xbox controllers we can hold at once. The PS4 itself only has
 * two USB ports and only four user slots, so 4 is already generous.       */
#define XBS_MAX_PADS             4

/* How many pad handles we track at the same time (real + virtual).        */
#define XBS_MAX_ENTRIES          8

/* Handles that WE invent start here (same convention as the old plugin
 * and remotePad). Real PS4 handles are small numbers.                      */
#define XBS_VIRTUAL_HANDLE_BASE  1000

/* ---- Timing ------------------------------------------------------------ */
#define XBS_USB_READ_TIMEOUT_MS  100     /* one USB read waits at most this */
#define XBS_USB_WRITE_TIMEOUT_MS 100
#define XBS_SCAN_INTERVAL_US     500000  /* look for plug/unplug twice/sec  */
#define XBS_REAL_DISCONNECT_DEBOUNCE_US 300000 /* a real DS4 must look "off"
                                                  this long before an Xbox
                                                  pad may take its place    */
#define XBS_FAST_ERROR_LIMIT     100     /* consecutive instant USB errors
                                            before a pad is declared dead   */

/* ---- File locations on the PS4 ---------------------------------------- */
#define XBS_CONFIG_PATH          "/data/GoldHEN/xbox_series.ini"
#define XBS_LOG_PATH             "/data/GoldHEN/xbox_series.log"

/* ---- Run-time settings (filled from the .ini file) -------------------- */
typedef struct {
    int      enabled;                /* 0 = plugin does nothing at all      */
    int      stick_deadzone_pct;     /* 0..40  (% of full stick travel)     */
    int      trigger_threshold_pct;  /* 1..100 (% of trigger travel at which
                                        the digital L2/R2 button turns on)  */
    int      view_to_touchpad;       /* 0: View -> Share, 1: View -> Touchpad
                                        click                               */
    int      allow_foreground_user;  /* 1: an Xbox pad may be given to the
                                        user that launched the game (P1)    */
    uint32_t player_mask;            /* bit n set = the user at login
                                        position n may receive an Xbox pad.
                                        0 = automatic                       */
    int      guide_ack;              /* 1: acknowledge Xbox-button reports  */
    int      notifications;          /* 1: show PS4 pop-up messages         */
    int      log_to_file;            /* 1: write /data/GoldHEN/xbox_series.log */
    int      log_verbose;            /* 1: log pad API calls (first few)    */
} XbsSettings;

extern XbsSettings g_settings;

void settings_set_defaults(XbsSettings* s);

/* Parses text in the .ini format into *s (unknown keys are ignored). */
void settings_parse_text(XbsSettings* s, const char* text);

/* Reads XBS_CONFIG_PATH (if it exists) into g_settings. */
void settings_load(void);

#endif /* XBS_CONFIG_H */
