/*
 * USB controller manager (Layer 1).
 *
 * Finds Xbox Series X|S controllers, opens them, keeps one background thread
 * per controller that reads USB packets and stores the newest state, and
 * handles unplug / replug. The game-facing code NEVER waits on USB: it only
 * calls usbm_snapshot(), which copies the newest stored state.
 *
 * Slot numbers (0..XBS_MAX_PADS-1) identify controllers. A slot keeps its
 * number while the controller stays plugged in. When a controller is removed
 * its slot becomes free and the next controller that is plugged in takes the
 * lowest free slot.
 */

#ifndef USB_MANAGER_H
#define USB_MANAGER_H

#include <stdint.h>
#include "xbox_series.h"

/* Loads libSceUsbd and calls sceUsbdInit. Call once from plugin_load.
 * Returns 0 on success, negative if USB is unavailable (not fatal). */
int usbm_init(void);

/* Does the first scan and starts the background scanner thread.
 * Safe to call more than once; only the first call does anything. */
int usbm_start(void);

/* Stops all threads, releases/closes every controller, calls sceUsbdExit. */
void usbm_stop(void);

/* 1 if a controller is open in this slot and its reader is alive. */
int usbm_is_present(int slot);

/* Number of slots currently present. */
int usbm_present_count(void);

/* Copies the newest state of a slot.
 *   *in          newest parsed input (valid only if *have_input == 1)
 *   *latched     buttons seen pressed since the previous snapshot (XBS_BTN_*),
 *                the stored latch is cleared by this call
 *   *have_input  1 once at least one valid input packet has arrived
 * Returns 1 if the slot is present, 0 otherwise. Never blocks on USB. */
int usbm_snapshot(int slot, XbsInput* in, uint16_t* latched, int* have_input);

#endif /* USB_MANAGER_H */
