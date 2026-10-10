/*
 * scePad / GoldHEN hooks (Layer 5).
 *
 * These hooks sit in front of the PS4 pad library. Real DualShock 4 handles
 * pass straight through to the PS4; the plugin only steps in for pads that
 * have no DualShock 4 connected.
 */

#ifndef HOOKS_H
#define HOOKS_H

/* Loads the needed PS4 libraries, installs the patches and hooks.
 * Returns 0 on success, negative on failure (plugin should unload). */
int hooks_install(void);

/* Removes every hook and patch that hooks_install() created. */
void hooks_remove(void);

#endif /* HOOKS_H */
