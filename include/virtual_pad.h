/*
 * Virtual pad manager (Layer 4).
 *
 * Keeps one "entry" for every pad handle the game opened through
 * scePadOpen, and decides which PS4 user (player) each Xbox controller
 * belongs to.
 *
 * Two kinds of entries:
 *   REAL    : the PS4 gave the game a real handle (the user may or may not
 *             have a DualShock 4 turned on). If that DualShock 4 is NOT
 *             connected, an Xbox controller can take its place: the plugin
 *             replaces the "disconnected" data the game reads with Xbox data.
 *             If the DualShock 4 is connected, the plugin never touches it.
 *   VIRTUAL : the PS4 refused to open a pad for this user (no controller at
 *             all), so the plugin invented a handle (1000 + n) and answers
 *             every call for it itself (like the original plugin did).
 *
 * Binding rule (automatic mode):
 *   - the user that launched the game (Player 1) never receives an Xbox
 *     controller unless allow_player1=1,
 *   - a REAL entry only receives one after its DualShock 4 has looked
 *     disconnected for 300 ms (so a slow DS4 start-up is not hijacked),
 *   - when several users want one, the lower login position (Player 2 before
 *     Player 3) is served first,
 *   - once the DualShock 4 reconnects the Xbox controller is released again.
 * With xbox_players=... in the .ini only the listed login positions qualify.
 */

#ifndef VIRTUAL_PAD_H
#define VIRTUAL_PAD_H

#include <stdint.h>
#include "ds4.h"

void vpad_init(void);
void vpad_shutdown(void);

/* ---- registration (called by the scePadOpen/scePadClose hooks) ---------- */

/* Finds the entry owned by a user. Returns 1 if found. */
int vpad_lookup_user(int32_t user_id, int32_t* handle_out, int* is_virtual_out);

/* Remembers a real handle the PS4 returned. Returns 0 on success, -1 if full. */
int vpad_register_real(int32_t handle, int32_t user_id, int login_pos, int is_foreground);

/* Creates a virtual pad. Returns the new handle (> 0) or a negative PS4 error. */
int32_t vpad_register_virtual(int32_t user_id, int login_pos, int is_foreground);

/* May this user (by position / foreground flag) ever receive an Xbox pad? */
int vpad_policy_allows(int login_pos, int is_foreground);

/* Forgets a handle and frees any Xbox controller it held. Returns 1 if tracked. */
int vpad_unregister(int32_t handle);

int vpad_is_virtual(int32_t handle);
int vpad_is_tracked(int32_t handle);

/* ---- serving data (called by the Read/ReadState/GetInfo hooks) ---------- */

/* real_connected: 1 = the real pad is connected, 0 = not connected (or the
 * real call failed), -1 = unknown (no new sample this time).
 * Returns 1 if *out was filled with Xbox/virtual data (caller must return
 * success), 0 if the caller should keep the real data untouched. */
int vpad_overlay_state(int32_t handle, int real_connected, OrbisPadData* out);
int vpad_overlay_info(int32_t handle, int real_connected, OrbisPadInformation* info);

#endif /* VIRTUAL_PAD_H */
