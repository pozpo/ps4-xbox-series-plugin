/*
 * Virtual pad manager implementation. See virtual_pad.h for the rules.
 *
 * Locking: one mutex (g_lock) protects the entry table. While holding it we
 * may call usbm_snapshot() (which takes a per-controller lock). The USB
 * manager never calls back into this file, so there is no lock cycle.
 */

#include "virtual_pad.h"
#include "config.h"
#include "debug_log.h"
#include "translator.h"
#include "usb_manager.h"
#include "xbs_features.h"

#include <string.h>
#include <stdio.h>

#include <orbis/libkernel.h>
#include <orbis/_types/pthread.h>
#include <orbis/_types/errors.h>

typedef struct {
    int      used;
    int32_t  handle;          /* number the game uses                         */
    int      is_virtual;      /* 1 = we invented the handle                   */
    int32_t  user_id;
    int      login_pos;       /* 1..4 = position in the login list, 100+ = ?  */
    int      is_fg;           /* user that launched the game                  */
    int      slot;            /* Xbox slot bound to this entry, -1 = none     */
    uint64_t disc_since_us;   /* when the real pad first looked disconnected  */
    uint64_t last_seen_us;    /* last time the game talked to this entry      */
    uint64_t last_ts;         /* last timestamp we reported                   */
} VPadEntry;

static VPadEntry         g_ent[XBS_MAX_ENTRIES];
static int               g_owner[XBS_MAX_PADS];   /* entry index owning a slot */
static OrbisPthreadMutex g_lock;
static int               g_ready = 0;
static XbsTranslateConfig g_tcfg;

#define STALE_US 2000000ULL     /* ignore entries the game has not touched for 2 s */

/* ------------------------------------------------------------------ */

void vpad_init(void) {
    memset(g_ent, 0, sizeof(g_ent));
    for (int i = 0; i < XBS_MAX_PADS; i++) g_owner[i] = -1;
    xbs_translate_config_make(&g_tcfg, g_settings.stick_deadzone_pct,
                              g_settings.trigger_threshold_pct,
                              g_settings.view_to_touchpad);
    if (!g_ready) {
        if (scePthreadMutexInit(&g_lock, 0, "xbsVpadMtx") >= 0) g_ready = 1;
    }
}

void vpad_shutdown(void) {
    g_ready = 0;       /* hooks stop serving; the mutex is simply abandoned */
}

/* ---- helpers (all called with g_lock held) ------------------------- */

static int find_by_handle(int32_t handle) {
    for (int i = 0; i < XBS_MAX_ENTRIES; i++)
        if (g_ent[i].used && g_ent[i].handle == handle) return i;
    return -1;
}

static int policy_ok(int login_pos, int is_fg) {
    if (g_settings.player_mask != 0) {
        if (login_pos < 1 || login_pos > 4) return 0;
        return ((g_settings.player_mask >> login_pos) & 1u) ? 1 : 0;
    }
    if (is_fg && !g_settings.allow_foreground_user) return 0;
    return 1;
}

static void release_binding(int idx, const char* why) {
    VPadEntry* e = &g_ent[idx];
    if (e->slot >= 0) {
        XBS_LOG("Xbox slot %d released from user 0x%x (%s)", e->slot,
                (unsigned)e->user_id, why);
        if (g_owner[e->slot] == idx) g_owner[e->slot] = -1;
        e->slot = -1;
    }
}

/* Tries to give entry idx an Xbox controller. Returns 1 if it now has one.
 * On a new binding, 'msg' receives the text of a notification to show. */
static int try_bind(int idx, uint64_t now, char* msg, size_t cap) {
    VPadEntry* e = &g_ent[idx];
    if (e->slot >= 0) return 1;
    if (!policy_ok(e->login_pos, e->is_fg)) return 0;

    /* A user at a lower login position that also wants a pad goes first. */
    for (int j = 0; j < XBS_MAX_ENTRIES; j++) {
        if (j == idx || !g_ent[j].used) continue;
        VPadEntry* o = &g_ent[j];
        if (o->slot >= 0) continue;
        if (!policy_ok(o->login_pos, o->is_fg)) continue;
        if (now - o->last_seen_us > STALE_US) continue;
        if (!(o->is_virtual || o->disc_since_us != 0)) continue;  /* DS4 still looks connected */
        if (o->login_pos < e->login_pos ||
            (o->login_pos == e->login_pos && j < idx))
            return 0;
    }

    for (int s = 0; s < XBS_MAX_PADS; s++) {
        if (g_owner[s] < 0 && usbm_is_present(s)) {
            g_owner[s] = idx;
            e->slot = s;
            if (e->login_pos >= 1 && e->login_pos <= 4)
                snprintf(msg, cap, "Xbox #%d -> Player %d", s + 1, e->login_pos);
            else
                snprintf(msg, cap, "Xbox #%d -> user 0x%x", s + 1, (unsigned)e->user_id);
            return 1;
        }
    }
    return 0;
}

/* Decides whether the game should receive Xbox data for this entry right now.
 * Returns 1 = serve Xbox/virtual data, 0 = leave the real data alone. */
static int evaluate(int idx, int real_connected, uint64_t now, char* msg, size_t cap) {
    VPadEntry* e = &g_ent[idx];
    e->last_seen_us = now;

    if (e->is_virtual) {
        if (e->slot < 0) try_bind(idx, now, msg, cap);
        return 1;                                  /* a virtual pad always answers */
    }

    /* REAL entry */
    if (real_connected == 1) {
        e->disc_since_us = 0;
        release_binding(idx, "real pad connected");
        return 0;
    }
    if (real_connected == 0 && e->disc_since_us == 0) e->disc_since_us = now;

    if (e->slot < 0 && e->disc_since_us != 0 &&
        now - e->disc_since_us >= XBS_REAL_DISCONNECT_DEBOUNCE_US) {
        try_bind(idx, now, msg, cap);
    }
    return e->slot >= 0 ? 1 : 0;
}

static void fill_state(VPadEntry* e, OrbisPadData* out, uint64_t now) {
    XbsInput in;
    uint16_t latched = 0;
    int have = 0;
    int present = 0;

    memset(&in, 0, sizeof(in));
    if (e->slot >= 0) present = usbm_snapshot(e->slot, &in, &latched, &have);

    /* Timestamps: microseconds of process time, strictly increasing per pad. */
    uint64_t ts = now;
    if (ts <= e->last_ts) ts = e->last_ts + 1;
    e->last_ts = ts;

    if (present && have) {
        in.buttons = (uint16_t)(in.buttons | latched);
        xbs_fill_pad_data(out, &in, &g_tcfg, 1, ts);
    } else {
        /* Connected but nothing received yet (or unplugged): neutral data. */
        xbs_fill_pad_data(out, NULL, &g_tcfg, present, ts);
    }
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int vpad_lookup_user(int32_t user_id, int32_t* handle_out, int* is_virtual_out) {
    int found = 0;
    if (!g_ready) return 0;
    scePthreadMutexLock(&g_lock);
    for (int i = 0; i < XBS_MAX_ENTRIES; i++) {
        if (g_ent[i].used && g_ent[i].user_id == user_id) {
            if (handle_out) *handle_out = g_ent[i].handle;
            if (is_virtual_out) *is_virtual_out = g_ent[i].is_virtual;
            found = 1;
            break;
        }
    }
    scePthreadMutexUnlock(&g_lock);
    return found;
}

int vpad_register_real(int32_t handle, int32_t user_id, int login_pos, int is_foreground) {
    if (!g_ready) return -1;
    int rc = -1;
    scePthreadMutexLock(&g_lock);
    int idx = find_by_handle(handle);
    if (idx < 0) {
        for (int i = 0; i < XBS_MAX_ENTRIES; i++)
            if (!g_ent[i].used) { idx = i; break; }
    }
    if (idx >= 0) {
        VPadEntry* e = &g_ent[idx];
        if (e->used) release_binding(idx, "re-registered");
        memset(e, 0, sizeof(*e));
        e->used = 1;
        e->handle = handle;
        e->is_virtual = 0;
        e->user_id = user_id;
        e->login_pos = login_pos;
        e->is_fg = is_foreground;
        e->slot = -1;
        rc = 0;
    }
    scePthreadMutexUnlock(&g_lock);
    return rc;
}

int32_t vpad_register_virtual(int32_t user_id, int login_pos, int is_foreground) {
    if (!g_ready) return ORBIS_PAD_ERROR_DEVICE_NO_HANDLE;
    int32_t handle = ORBIS_PAD_ERROR_DEVICE_NO_HANDLE;
    scePthreadMutexLock(&g_lock);
    for (int i = 0; i < XBS_MAX_ENTRIES; i++) {
        if (!g_ent[i].used) {
            VPadEntry* e = &g_ent[i];
            memset(e, 0, sizeof(*e));
            e->used = 1;
            e->handle = XBS_VIRTUAL_HANDLE_BASE + i;
            e->is_virtual = 1;
            e->user_id = user_id;
            e->login_pos = login_pos;
            e->is_fg = is_foreground;
            e->slot = -1;
            handle = e->handle;
            break;
        }
    }
    scePthreadMutexUnlock(&g_lock);
    return handle;
}

int vpad_policy_allows(int login_pos, int is_foreground) {
    return policy_ok(login_pos, is_foreground);
}

int vpad_unregister(int32_t handle) {
    if (!g_ready) return 0;
    int tracked = 0;
    scePthreadMutexLock(&g_lock);
    int idx = find_by_handle(handle);
    if (idx >= 0) {
        release_binding(idx, "pad closed");
        g_ent[idx].used = 0;
        tracked = 1;
    }
    scePthreadMutexUnlock(&g_lock);
    return tracked;
}

int vpad_is_virtual(int32_t handle) {
    if (!g_ready) return 0;
    int v = 0;
    scePthreadMutexLock(&g_lock);
    int idx = find_by_handle(handle);
    if (idx >= 0) v = g_ent[idx].is_virtual;
    scePthreadMutexUnlock(&g_lock);
    return v;
}

int vpad_is_tracked(int32_t handle) {
    if (!g_ready) return 0;
    scePthreadMutexLock(&g_lock);
    int t = find_by_handle(handle) >= 0;
    scePthreadMutexUnlock(&g_lock);
    return t;
}

int vpad_overlay_state(int32_t handle, int real_connected, OrbisPadData* out) {
    if (!g_ready || !out) return 0;
    char msg[96];
    msg[0] = 0;
    int served = 0;

    scePthreadMutexLock(&g_lock);
    int idx = find_by_handle(handle);
    if (idx >= 0) {
        uint64_t now = sceKernelGetProcessTime();
        if (evaluate(idx, real_connected, now, msg, sizeof(msg))) {
            fill_state(&g_ent[idx], out, now);
            served = 1;
        }
    }
    scePthreadMutexUnlock(&g_lock);

    if (msg[0]) XBS_NOTIFY("%s", msg);      /* event: a pad was just assigned */
    return served;
}

int vpad_overlay_info(int32_t handle, int real_connected, OrbisPadInformation* info) {
    if (!g_ready || !info) return 0;
    char msg[96];
    msg[0] = 0;
    int served = 0;

    scePthreadMutexLock(&g_lock);
    int idx = find_by_handle(handle);
    if (idx >= 0) {
        uint64_t now = sceKernelGetProcessTime();
        if (evaluate(idx, real_connected, now, msg, sizeof(msg))) {
            int present = (g_ent[idx].slot >= 0) && usbm_is_present(g_ent[idx].slot);
            xbs_fill_pad_info(info, present);
            served = 1;
        }
    }
    scePthreadMutexUnlock(&g_lock);

    if (msg[0]) XBS_NOTIFY("%s", msg);
    return served;
}
