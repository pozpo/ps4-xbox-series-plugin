/*
 * Xbox Series X|S -> PS4 pad data translator. See translator.h.
 *
 * Mapping (verified against the GIP layout in xbox_series.h):
 *   A -> Cross      B -> Circle     X -> Square     Y -> Triangle
 *   LB -> L1        RB -> R1        LT -> L2 (analog + digital)
 *   RT -> R2 (analog + digital)     L3 -> L3        R3 -> R3
 *   Menu -> Options                 View -> Share (or Touchpad click)
 *   D-pad -> D-pad                  Sticks -> Sticks
 *
 * Stick maths:
 *   Xbox : -32768..32767, centre 0, +Y is UP
 *   PS4  : 0..255, centre 128, +Y is DOWN (0 = up)
 *   so Y is negated, the dead zone is applied in the 16-bit domain, then
 *   the value is scaled to 8 bits (v + 32768) >> 8.
 *
 * Trigger maths:
 *   Xbox : 10 bit 0..1023  ->  PS4: 8 bit 0..255 (value >> 2)
 */

#include "translator.h"
#include "xbs_features.h"
#include <string.h>

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void xbs_translate_config_make(XbsTranslateConfig* cfg, int stick_deadzone_pct,
                               int trigger_threshold_pct, int view_to_touchpad) {
    if (!cfg) return;
    int dz_pct = clamp_int(stick_deadzone_pct, 0, 40);
    int tr_pct = clamp_int(trigger_threshold_pct, 1, 100);
    cfg->stick_deadzone_raw = (dz_pct * 32767) / 100;
    cfg->trigger_threshold8 = (uint8_t)clamp_int((tr_pct * 255) / 100, 1, 255);
    cfg->view_to_touchpad = view_to_touchpad ? 1 : 0;
}

/* value: -32768..32767. Returns -32767..32767 (dead zone removed and the
 * remaining travel stretched so full deflection still reaches full range). */
int xbs_axis_deadzone(int value, int deadzone) {
    int a = value < 0 ? -value : value;
    if (a > 32767) a = 32767;
    if (deadzone < 0) deadzone = 0;
    if (deadzone > 32000) deadzone = 32000;
    if (a <= deadzone) return 0;
    int r = ((a - deadzone) * 32767) / (32767 - deadzone);
    return value < 0 ? -r : r;
}

uint8_t xbs_axis_to_u8(int value) {
    int t = clamp_int(value, -32767, 32767) + 32768;
    return (uint8_t)(t >> 8);
}

static uint8_t trigger_to_u8(uint16_t raw) {
    int v = raw >> 2;
    return (uint8_t)(v > 255 ? 255 : v);
}

void xbs_fill_pad_data(OrbisPadData* d, const XbsInput* in,
                       const XbsTranslateConfig* cfg, int connected,
                       uint64_t timestamp) {
    if (!d) return;
    memset(d, 0, sizeof(*d));

    d->leftStick.x = 128;
    d->leftStick.y = 128;
    d->rightStick.x = 128;
    d->rightStick.y = 128;

    if (in && cfg) {
        int dz = cfg->stick_deadzone_raw;

        d->leftStick.x  = xbs_axis_to_u8(xbs_axis_deadzone(in->lx, dz));
        d->rightStick.x = xbs_axis_to_u8(xbs_axis_deadzone(in->rx, dz));
        /* Y: negate (Xbox up = positive, PS4 up = 0) */
        d->leftStick.y  = xbs_axis_to_u8(xbs_axis_deadzone(-(int)in->ly, dz));
        d->rightStick.y = xbs_axis_to_u8(xbs_axis_deadzone(-(int)in->ry, dz));

        uint8_t lt8 = trigger_to_u8(in->lt);
        uint8_t rt8 = trigger_to_u8(in->rt);
        d->analogButtons.l2 = lt8;
        d->analogButtons.r2 = rt8;

        uint32_t b = 0;
        uint16_t x = in->buttons;

        if (x & XBS_BTN_A) b |= DS4_BUTTON_CROSS;
        if (x & XBS_BTN_B) b |= DS4_BUTTON_CIRCLE;
        if (x & XBS_BTN_X) b |= DS4_BUTTON_SQUARE;
        if (x & XBS_BTN_Y) b |= DS4_BUTTON_TRIANGLE;

        if (x & XBS_BTN_LB) b |= DS4_BUTTON_L1;
        if (x & XBS_BTN_RB) b |= DS4_BUTTON_R1;
        if (lt8 >= cfg->trigger_threshold8) b |= DS4_BUTTON_L2;
        if (rt8 >= cfg->trigger_threshold8) b |= DS4_BUTTON_R2;

        if (x & XBS_BTN_L3) b |= DS4_BUTTON_L3;
        if (x & XBS_BTN_R3) b |= DS4_BUTTON_R3;

        if (x & XBS_BTN_MENU) b |= DS4_BUTTON_OPTIONS;
        if (x & XBS_BTN_VIEW) b |= cfg->view_to_touchpad ? DS4_BUTTON_TOUCHPAD
                                                          : DS4_BUTTON_SHARE;

        if (x & XBS_BTN_UP)    b |= DS4_BUTTON_DPAD_UP;
        if (x & XBS_BTN_DOWN)  b |= DS4_BUTTON_DPAD_DOWN;
        if (x & XBS_BTN_LEFT)  b |= DS4_BUTTON_DPAD_LEFT;
        if (x & XBS_BTN_RIGHT) b |= DS4_BUTTON_DPAD_RIGHT;

        d->buttons = b;
    }

    d->connected = connected ? 1 : 0;
    d->timestamp = timestamp;

#if XBS_HAVE_PADDATA_COUNT
    d->count = 1;
#endif

#if XBS_HAVE_PADDATA_MOTION
    /* No gyro / accelerometer on an Xbox pad: report "resting, flat". */
    d->quat.x = 0.0f;
    d->quat.y = 0.0f;
    d->quat.z = 0.0f;
    d->quat.w = 1.0f;
    d->vel.x = 0.0f;
    d->vel.y = 0.0f;
    d->vel.z = 0.0f;
    d->acell.x = 0.0f;
    d->acell.y = 0.0f;
    d->acell.z = 1.0f;
    d->touch.fingers = 0;
#endif
}

void xbs_fill_pad_info(OrbisPadInformation* info, int connected) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    info->connected = connected ? 1 : 0;
    info->connectionType = ORBIS_PAD_CONNECTION_TYPE_STANDARD;
    info->deviceClass = ORBIS_PAD_DEVICE_CLASS_PAD;
    /* Values a real DualShock 4 reports (same as remotePad). */
    info->touchpadDensity = 44.86f;
    info->touchResolutionX = 1920;
    info->touchResolutionY = 942;
#if XBS_HAVE_PADINFO_COUNT
    info->count = 1;
#endif
#if XBS_HAVE_PADINFO_DEADZONE
    info->stickDeadzoneL = 0x0d;
    info->stickDeadzoneR = 0x0d;
#endif
}
