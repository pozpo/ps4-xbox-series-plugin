/*
 * PS4 (DualShock 4) button bit definitions used by the translator.
 * The values come from the OpenOrbis header orbis/_types/pad.h.
 */

#ifndef DS4_H
#define DS4_H

#include <stdint.h>
#include <orbis/_types/pad.h>

#define DS4_BUTTON_L3           ORBIS_PAD_BUTTON_L3
#define DS4_BUTTON_R3           ORBIS_PAD_BUTTON_R3
#define DS4_BUTTON_OPTIONS      ORBIS_PAD_BUTTON_OPTIONS
#define DS4_BUTTON_DPAD_UP      ORBIS_PAD_BUTTON_UP
#define DS4_BUTTON_DPAD_RIGHT   ORBIS_PAD_BUTTON_RIGHT
#define DS4_BUTTON_DPAD_DOWN    ORBIS_PAD_BUTTON_DOWN
#define DS4_BUTTON_DPAD_LEFT    ORBIS_PAD_BUTTON_LEFT
#define DS4_BUTTON_L2           ORBIS_PAD_BUTTON_L2
#define DS4_BUTTON_R2           ORBIS_PAD_BUTTON_R2
#define DS4_BUTTON_L1           ORBIS_PAD_BUTTON_L1
#define DS4_BUTTON_R1           ORBIS_PAD_BUTTON_R1
#define DS4_BUTTON_TRIANGLE     ORBIS_PAD_BUTTON_TRIANGLE
#define DS4_BUTTON_CIRCLE       ORBIS_PAD_BUTTON_CIRCLE
#define DS4_BUTTON_CROSS        ORBIS_PAD_BUTTON_CROSS
#define DS4_BUTTON_SQUARE       ORBIS_PAD_BUTTON_SQUARE
#define DS4_BUTTON_TOUCHPAD     ORBIS_PAD_BUTTON_TOUCH_PAD

/* Share button: not in the OpenOrbis enum; bit 0 (same as the old plugin). */
#define DS4_BUTTON_SHARE        0x00000001

#endif /* DS4_H */
