/*
 * scePad hooks.
 *
 * What the hooks do, in plain words:
 *
 *  scePadOpen    The game asks for a controller for a user. We ALWAYS ask the
 *                real PS4 first. If the PS4 gives a real handle we hand it
 *                back unchanged (and remember it). Only if the PS4 refuses do
 *                we invent a virtual handle (1000+n) - but only when an Xbox
 *                controller is actually plugged in.
 *  scePadClose   Forgets the handle (and releases any Xbox pad it used).
 *  scePadGetHandle   Games call this to ask "do I already have a pad for this
 *                user?". The old plugin did not hook it, so for a virtual pad
 *                the PS4 answered "no", the game opened the pad again and
 *                again, and never read it. Now we answer for virtual pads.
 *  scePadRead / scePadReadState / scePadGetControllerInformation
 *                Real handles: ask the PS4 for the real data. If the
 *                DualShock 4 is connected, return it untouched. If it is not,
 *                and an Xbox pad is assigned to this user, return Xbox data.
 *                Virtual handles: answer entirely from the Xbox controller.
 *  setters       (light bar, vibration, motion sensor ...) For virtual handles
 *                return "OK" instead of letting the PS4 reject a handle it
 *                never issued. Real handles pass through.
 *
 * None of these hooks ever waits for USB; Xbox data is read from a cache.
 *
 * Real-pad passthrough for Read/ReadState uses the same mechanism as the
 * original plugin and gamepad_helper (scePadReadExt / scePadReadStateExt plus
 * the two 5-byte patches). That part is deliberately unchanged.
 */

#include "hooks.h"
#include "config.h"
#include "debug_log.h"
#include "translator.h"
#include "usb_manager.h"
#include "virtual_pad.h"
#include "xbs_features.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/Usbd.h>
#include <orbis/UserService.h>
#include <orbis/_types/errors.h>

#include <GoldHEN.h>
#include <Detour.h>
#include <Patcher.h>
#include <Utilities.h>

/* ---- external functions (same declarations as the original plugin) ----- */
extern int32_t scePadReadExt(int32_t handle, OrbisPadData* pData, int32_t num);
extern int32_t scePadReadStateExt(int32_t handle, OrbisPadData* pData);
extern int sys_dynlib_load_prx(const char* path, int* handle);
extern const char* sceKernelGetFsSandboxRandomWord(void);

/* ---- layouts copied from remotePad (device-class queries) -------------- */
typedef struct {
    int32_t deviceClass;
    uint8_t reserved[4];
    uint8_t classData[12];
} XbsDeviceClassExtInfo;

typedef struct {
    int32_t deviceClass;
    bool    bDataValid;
    uint8_t classData[16];
} XbsDeviceClassData;

/* ---- function-pointer types for HOOK_CONTINUE -------------------------- */
typedef int32_t (*scePadOpen_t)(int32_t, int32_t, int32_t, void*);
typedef int32_t (*scePadClose_t)(int32_t);
typedef int32_t (*scePadGetControllerInformation_t)(int32_t, OrbisPadInformation*);

#if XBS_HAVE_FN_scePadGetHandle
typedef int32_t (*scePadGetHandle_t)(int32_t, uint32_t, uint32_t);
#endif
#if XBS_HAVE_FN_scePadSetLightBar
typedef int32_t (*scePadSetLightBar_t)(int32_t, OrbisPadColor*);
#endif
#if XBS_HAVE_FN_scePadResetLightBar
typedef int32_t (*scePadResetLightBar_t)(int32_t);
#endif
#if XBS_HAVE_FN_scePadSetVibration
typedef int32_t (*scePadSetVibration_t)(int32_t, const OrbisPadVibeParam*);
#endif
#if XBS_HAVE_FN_scePadResetOrientation
typedef int32_t (*scePadResetOrientation_t)(int32_t);
#endif
#if XBS_HAVE_FN_scePadSetMotionSensorState
typedef int32_t (*scePadSetMotionSensorState_t)(int32_t, bool);
#endif
#if XBS_HAVE_FN_scePadSetTiltCorrectionState
typedef int32_t (*scePadSetTiltCorrectionState_t)(int32_t, bool);
#endif
#if XBS_HAVE_FN_scePadSetAngularVelocityDeadbandState
typedef int32_t (*scePadSetAngularVelocityDeadbandState_t)(int32_t, bool);
#endif
#if XBS_HAVE_FN_scePadDeviceClassParseData
typedef int32_t (*scePadDeviceClassParseData_t)(int32_t, const OrbisPadData*, XbsDeviceClassData*);
#endif
#if XBS_HAVE_FN_scePadDeviceClassGetExtendedInformation
typedef int32_t (*scePadDeviceClassGetExtendedInformation_t)(int32_t, XbsDeviceClassExtInfo*);
#endif

/* ---- hook declarations ------------------------------------------------- */
HOOK_INIT(scePadRead);
HOOK_INIT(scePadReadState);
HOOK_INIT(scePadOpen);
HOOK_INIT(scePadClose);
HOOK_INIT(scePadGetControllerInformation);
#if XBS_HAVE_FN_scePadGetHandle
HOOK_INIT(scePadGetHandle);
#endif
#if XBS_HAVE_FN_scePadSetLightBar
HOOK_INIT(scePadSetLightBar);
#endif
#if XBS_HAVE_FN_scePadResetLightBar
HOOK_INIT(scePadResetLightBar);
#endif
#if XBS_HAVE_FN_scePadSetVibration
HOOK_INIT(scePadSetVibration);
#endif
#if XBS_HAVE_FN_scePadResetOrientation
HOOK_INIT(scePadResetOrientation);
#endif
#if XBS_HAVE_FN_scePadSetMotionSensorState
HOOK_INIT(scePadSetMotionSensorState);
#endif
#if XBS_HAVE_FN_scePadSetTiltCorrectionState
HOOK_INIT(scePadSetTiltCorrectionState);
#endif
#if XBS_HAVE_FN_scePadSetAngularVelocityDeadbandState
HOOK_INIT(scePadSetAngularVelocityDeadbandState);
#endif
#if XBS_HAVE_FN_scePadDeviceClassParseData
HOOK_INIT(scePadDeviceClassParseData);
#endif
#if XBS_HAVE_FN_scePadDeviceClassGetExtendedInformation
HOOK_INIT(scePadDeviceClassGetExtendedInformation);
#endif

/* ---- state ------------------------------------------------------------- */
static Patcher* g_padReadExtPatcher = NULL;
static Patcher* g_padReadStateExtPatcher = NULL;

static int g_hooks_installed = 0;
static int g_user_prx_loaded = 0;
static volatile int g_runtime_started = 0;
static volatile int g_unknown_user_counter = 0;

/* log counters (so the log file stays small) */
static int g_c_open = 0, g_c_close = 0, g_c_gethandle = 0, g_c_info = 0;
static int g_c_read = 0, g_c_readstate = 0, g_c_setter = 0;

/* ---- helpers ----------------------------------------------------------- */

/* First pad call: scan for controllers and start the USB threads.
 * Done here (inside the game process, from a game thread) rather than in
 * plugin_load. remotePad also starts its worker thread from inside a pad
 * hook (scePadInit), i.e. from a game thread, so that context is proven. */
static void xbs_runtime_start(void) {
    if (!__sync_bool_compare_and_swap(&g_runtime_started, 0, 1)) return;
    XBS_LOG("first pad call: starting USB manager");
    usbm_start();
}

/* Works out where a user sits in the PS4 login list (1..4) and whether this
 * is the user that launched the game. */
static void resolve_user(int32_t userId, int* login_pos, int* is_fg) {
    *login_pos = 100 + (int)__sync_fetch_and_add(&g_unknown_user_counter, 1);
    *is_fg = 0;
    if (!g_user_prx_loaded) return;

    OrbisUserServiceLoginUserIdList list;
    for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++)
        list.userId[i] = ORBIS_USER_SERVICE_USER_ID_INVALID;

    if (sceUserServiceGetLoginUserIdList(&list) == 0) {
        for (int i = 0; i < ORBIS_USER_SERVICE_MAX_LOGIN_USERS; i++) {
            if (list.userId[i] == userId) { *login_pos = i + 1; break; }
        }
    }

    int32_t fg = 0;
    if (sceUserServiceGetForegroundUser(&fg) == 0 && fg != 0) {
        *is_fg = (fg == userId) ? 1 : 0;
    } else if (*login_pos == 1) {
        *is_fg = 1;      /* could not ask: assume login position 1 launched the game */
    }
}

static int user_is_interceptable(int32_t userId, int32_t type, int32_t index) {
    if (!g_settings.enabled) return 0;
    if (type != 0 || index != 0) return 0;     /* only the standard pad, port 0 */
    if (userId <= 0 || userId == 255) return 0; /* invalid / system user */
    return 1;
}

/* ======================================================================== */
/* Hooks                                                                    */
/* ======================================================================== */


int32_t scePadOpen_hook(int32_t userId, int32_t type, int32_t index, void* param) {
    xbs_runtime_start();

    if (!user_is_interceptable(userId, type, index)) {
        return HOOK_CONTINUE(scePadOpen, scePadOpen_t, userId, type, index, param);
    }

    int32_t existing = 0;
    int is_virtual = 0;
    if (vpad_lookup_user(userId, &existing, &is_virtual)) {
        if (is_virtual) {
            /* Same answer the PS4 gives for a pad that is already open.
             * No notification: repeated opens must never spam the screen. */
            XBS_LOG_LIMITED(g_c_open, 60,
                            "scePadOpen user=0x%x: virtual pad already open (handle %d)",
                            (unsigned)userId, (int)existing);
            return ORBIS_PAD_ERROR_ALREADY_OPENED;
        }
        /* A real handle exists: let the PS4 answer (it knows about duplicates). */
        int32_t r = HOOK_CONTINUE(scePadOpen, scePadOpen_t, userId, type, index, param);
        XBS_LOG_LIMITED(g_c_open, 60, "scePadOpen user=0x%x again -> 0x%x",
                        (unsigned)userId, (unsigned)r);
        return r;
    }

    int login_pos = 0, is_fg = 0;
    resolve_user(userId, &login_pos, &is_fg);

    int32_t rh = HOOK_CONTINUE(scePadOpen, scePadOpen_t, userId, type, index, param);
    if (rh >= 0) {
        vpad_register_real(rh, userId, login_pos, is_fg);
        XBS_LOG_LIMITED(g_c_open, 60,
                        "scePadOpen user=0x%x pos=%d fg=%d -> real handle %d",
                        (unsigned)userId, login_pos, is_fg, (int)rh);
        return rh;
    }

    /* The PS4 refused (that user has no pad). Offer a virtual pad if an Xbox
     * controller is plugged in and policy allows this user to have one. */
    if (usbm_present_count() > 0 && vpad_policy_allows(login_pos, is_fg)) {
        int32_t vh = vpad_register_virtual(userId, login_pos, is_fg);
        if (vh > 0) {
            XBS_LOG_LIMITED(g_c_open, 60,
                            "scePadOpen user=0x%x pos=%d: PS4 said 0x%x -> virtual handle %d",
                            (unsigned)userId, login_pos, (unsigned)rh, (int)vh);
            return vh;
        }
    }

    XBS_LOG_LIMITED(g_c_open, 60, "scePadOpen user=0x%x pos=%d -> 0x%x (no Xbox pad offered)",
                    (unsigned)userId, login_pos, (unsigned)rh);
    return rh;
}

int32_t scePadClose_hook(int32_t handle) {
    if (vpad_is_virtual(handle)) {
        vpad_unregister(handle);
        XBS_LOG_LIMITED(g_c_close, 60, "scePadClose virtual handle %d", (int)handle);
        return 0;
    }
    if (vpad_unregister(handle)) {
        XBS_LOG_LIMITED(g_c_close, 60, "scePadClose real handle %d", (int)handle);
    }
    return HOOK_CONTINUE(scePadClose, scePadClose_t, handle);
}

#if XBS_HAVE_FN_scePadGetHandle
int32_t scePadGetHandle_hook(int32_t userId, uint32_t type, uint32_t index) {
    int32_t h = 0;
    int is_virtual = 0;
    if (vpad_lookup_user(userId, &h, &is_virtual) && is_virtual) {
        XBS_LOG_LIMITED(g_c_gethandle, 40, "scePadGetHandle user=0x%x -> virtual %d",
                        (unsigned)userId, (int)h);
        return h;
    }
    return HOOK_CONTINUE(scePadGetHandle, scePadGetHandle_t, userId, type, index);
}
#endif

int32_t scePadGetControllerInformation_hook(int32_t handle, OrbisPadInformation* info) {
    if (info == NULL) {
        return HOOK_CONTINUE(scePadGetControllerInformation,
                             scePadGetControllerInformation_t, handle, info);
    }

    if (vpad_is_virtual(handle)) {
        if (!vpad_overlay_info(handle, 0, info)) xbs_fill_pad_info(info, 0);
        XBS_LOG_LIMITED(g_c_info, 40, "GetControllerInformation virtual %d connected=%d",
                        (int)handle, (int)info->connected);
        return 0;
    }

    int32_t ret = HOOK_CONTINUE(scePadGetControllerInformation,
                                scePadGetControllerInformation_t, handle, info);
    if (vpad_is_tracked(handle)) {
        int rc = (ret == 0 && info->connected) ? 1 : 0;
        if (vpad_overlay_info(handle, rc, info)) {
            XBS_LOG_LIMITED(g_c_info, 40, "GetControllerInformation real %d -> Xbox overlay",
                            (int)handle);
            return 0;
        }
    }
    return ret;
}

int32_t scePadRead_hook(int32_t handle, OrbisPadData* pData, int32_t num) {
    if (vpad_is_virtual(handle)) {
        if (pData == NULL || num <= 0) return ORBIS_PAD_ERROR_INVALID_ARG;
        if (!vpad_overlay_state(handle, 0, &pData[0])) return ORBIS_PAD_ERROR_INVALID_HANDLE;
        XBS_LOG_LIMITED(g_c_read, 40, "scePadRead virtual %d (num=%d) buttons=0x%x",
                        (int)handle, (int)num, (unsigned)pData[0].buttons);
        return 1;      /* exactly one (the newest) sample */
    }

    int32_t ret = scePadReadExt(handle, pData, num);

    if (pData != NULL && num > 0 && vpad_is_tracked(handle)) {
        int rc;
        if (ret > 0) {
            rc = 0;
            for (int i = 0; i < ret && i < num; i++)
                if (pData[i].connected) { rc = 1; break; }
        } else if (ret == 0) {
            rc = -1;       /* no new sample: unknown, keep the earlier decision */
        } else {
            rc = 0;        /* the PS4 reported an error for this pad */
        }
        if (vpad_overlay_state(handle, rc, &pData[0])) {
            XBS_LOG_LIMITED(g_c_read, 40, "scePadRead real %d -> Xbox overlay buttons=0x%x",
                            (int)handle, (unsigned)pData[0].buttons);
            return 1;
        }
    }
    return ret;
}

int32_t scePadReadState_hook(int32_t handle, OrbisPadData* pData) {
    if (vpad_is_virtual(handle)) {
        if (pData == NULL) return ORBIS_PAD_ERROR_INVALID_ARG;
        if (!vpad_overlay_state(handle, 0, pData)) return ORBIS_PAD_ERROR_INVALID_HANDLE;
        XBS_LOG_LIMITED(g_c_readstate, 40, "scePadReadState virtual %d buttons=0x%x",
                        (int)handle, (unsigned)pData->buttons);
        return 0;
    }

    int32_t ret = scePadReadStateExt(handle, pData);

    if (pData != NULL && vpad_is_tracked(handle)) {
        int rc = (ret == 0 && pData->connected) ? 1 : 0;
        if (vpad_overlay_state(handle, rc, pData)) {
            XBS_LOG_LIMITED(g_c_readstate, 40, "scePadReadState real %d -> Xbox overlay buttons=0x%x",
                            (int)handle, (unsigned)pData->buttons);
            return 0;
        }
    }
    return ret;
}

/* ---- setters: virtual handles answer "OK", real handles pass through ---- */

#if XBS_HAVE_FN_scePadSetLightBar
int32_t scePadSetLightBar_hook(int32_t handle, OrbisPadColor* color) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "SetLightBar virtual"); return 0; }
    return HOOK_CONTINUE(scePadSetLightBar, scePadSetLightBar_t, handle, color);
}
#endif

#if XBS_HAVE_FN_scePadResetLightBar
int32_t scePadResetLightBar_hook(int32_t handle) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "ResetLightBar virtual"); return 0; }
    return HOOK_CONTINUE(scePadResetLightBar, scePadResetLightBar_t, handle);
}
#endif

#if XBS_HAVE_FN_scePadSetVibration
int32_t scePadSetVibration_hook(int32_t handle, const OrbisPadVibeParam* param) {
    /* Rumble on Xbox controllers is not implemented: accept and ignore. */
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "SetVibration virtual (ignored)"); return 0; }
    return HOOK_CONTINUE(scePadSetVibration, scePadSetVibration_t, handle, param);
}
#endif

#if XBS_HAVE_FN_scePadResetOrientation
int32_t scePadResetOrientation_hook(int32_t handle) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "ResetOrientation virtual"); return 0; }
    return HOOK_CONTINUE(scePadResetOrientation, scePadResetOrientation_t, handle);
}
#endif

#if XBS_HAVE_FN_scePadSetMotionSensorState
int32_t scePadSetMotionSensorState_hook(int32_t handle, bool enable) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "SetMotionSensorState virtual"); return 0; }
    return HOOK_CONTINUE(scePadSetMotionSensorState, scePadSetMotionSensorState_t, handle, enable);
}
#endif

#if XBS_HAVE_FN_scePadSetTiltCorrectionState
int32_t scePadSetTiltCorrectionState_hook(int32_t handle, bool enable) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "SetTiltCorrectionState virtual"); return 0; }
    return HOOK_CONTINUE(scePadSetTiltCorrectionState, scePadSetTiltCorrectionState_t, handle, enable);
}
#endif

#if XBS_HAVE_FN_scePadSetAngularVelocityDeadbandState
int32_t scePadSetAngularVelocityDeadbandState_hook(int32_t handle, bool enable) {
    if (vpad_is_virtual(handle)) { XBS_LOG_LIMITED(g_c_setter, 30, "SetAngularVelocityDeadbandState virtual"); return 0; }
    return HOOK_CONTINUE(scePadSetAngularVelocityDeadbandState,
                         scePadSetAngularVelocityDeadbandState_t, handle, enable);
}
#endif

#if XBS_HAVE_FN_scePadDeviceClassParseData
int32_t scePadDeviceClassParseData_hook(int32_t handle, const OrbisPadData* data,
                                        XbsDeviceClassData* classData) {
    if (vpad_is_virtual(handle) && classData != NULL) {
        memset(classData, 0, sizeof(*classData));
        classData->deviceClass = ORBIS_PAD_DEVICE_CLASS_PAD;
        return 0;
    }
    return HOOK_CONTINUE(scePadDeviceClassParseData, scePadDeviceClassParseData_t,
                         handle, data, classData);
}
#endif

#if XBS_HAVE_FN_scePadDeviceClassGetExtendedInformation
int32_t scePadDeviceClassGetExtendedInformation_hook(int32_t handle, XbsDeviceClassExtInfo* info) {
    if (vpad_is_virtual(handle) && info != NULL) {
        memset(info, 0, sizeof(*info));
        info->deviceClass = ORBIS_PAD_DEVICE_CLASS_PAD;
        return 0;
    }
    return HOOK_CONTINUE(scePadDeviceClassGetExtendedInformation,
                         scePadDeviceClassGetExtendedInformation_t, handle, info);
}
#endif

/* ======================================================================== */
/* Install / remove                                                         */
/* ======================================================================== */

int hooks_install(void) {
    if (g_hooks_installed) return 0;

    char module[256];
    int h = 0;
    int ret;

    /* libScePad MUST load, otherwise hooks cannot work (same as original). */
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libScePad.sprx");
    ret = sys_dynlib_load_prx(module, &h);
    if (ret < 0 || h == 0) {
        XBS_NOTIFY("Xbox: Pad lib failed");
        return -1;
    }

    /* libSceUserService: used to learn the player order. Not fatal. */
    snprintf(module, 256, "/%s/common/lib/%s", sceKernelGetFsSandboxRandomWord(), "libSceUserService.sprx");
    h = 0;
    ret = sys_dynlib_load_prx(module, &h);
    if (ret >= 0 && h != 0) g_user_prx_loaded = 1;
    else XBS_LOG("libSceUserService not loaded: player order unknown");

    if ((uint64_t)scePadReadExt == 0) {
        XBS_NOTIFY("Xbox: No PadReadExt");
        return -1;
    }

    /* The two patches below are unchanged from the original working plugin
     * (and gamepad_helper): they make scePadReadExt / scePadReadStateExt
     * usable as "the real read" for DualShock 4 passthrough. */
    g_padReadExtPatcher = (Patcher*)malloc(sizeof(Patcher));
    if (g_padReadExtPatcher) {
        Patcher_Construct(g_padReadExtPatcher);
        uint8_t xor_ecx_ecx[5] = {0x31, 0xC9, 0x90, 0x90, 0x90};
        Patcher_Install_Patch(g_padReadExtPatcher, (uint64_t)scePadReadExt, xor_ecx_ecx, sizeof(xor_ecx_ecx));
    }

    g_padReadStateExtPatcher = (Patcher*)malloc(sizeof(Patcher));
    if (g_padReadStateExtPatcher) {
        Patcher_Construct(g_padReadStateExtPatcher);
        uint8_t xor_edx_edx[5] = {0x31, 0xD2, 0x90, 0x90, 0x90};
        Patcher_Install_Patch(g_padReadStateExtPatcher, (uint64_t)scePadReadStateExt, xor_edx_edx, sizeof(xor_edx_edx));
    }

    /* Hooks the original plugin already used */
    HOOK32(scePadRead);
    HOOK32(scePadReadState);
    HOOK32(scePadOpen);
    HOOK32(scePadClose);
    HOOK32(scePadGetControllerInformation);

    /* Additional hooks (same set remotePad uses), only those the OpenOrbis
     * headers on this computer declare (detected by build.ps1). */
#if XBS_HAVE_FN_scePadGetHandle
    HOOK32(scePadGetHandle);
#endif
#if XBS_HAVE_FN_scePadSetLightBar
    HOOK32(scePadSetLightBar);
#endif
#if XBS_HAVE_FN_scePadResetLightBar
    HOOK32(scePadResetLightBar);
#endif
#if XBS_HAVE_FN_scePadSetVibration
    HOOK32(scePadSetVibration);
#endif
#if XBS_HAVE_FN_scePadResetOrientation
    HOOK32(scePadResetOrientation);
#endif
#if XBS_HAVE_FN_scePadSetMotionSensorState
    HOOK32(scePadSetMotionSensorState);
#endif
#if XBS_HAVE_FN_scePadSetTiltCorrectionState
    HOOK32(scePadSetTiltCorrectionState);
#endif
#if XBS_HAVE_FN_scePadSetAngularVelocityDeadbandState
    HOOK32(scePadSetAngularVelocityDeadbandState);
#endif
#if XBS_HAVE_FN_scePadDeviceClassParseData
    HOOK32(scePadDeviceClassParseData);
#endif
#if XBS_HAVE_FN_scePadDeviceClassGetExtendedInformation
    HOOK32(scePadDeviceClassGetExtendedInformation);
#endif

    g_hooks_installed = 1;
    XBS_LOG("hooks installed");
    return 0;
}

void hooks_remove(void) {
    if (!g_hooks_installed) return;

    /* Unhook first so no new calls enter our code. */
    UNHOOK(scePadRead);
    UNHOOK(scePadReadState);
    UNHOOK(scePadOpen);
    UNHOOK(scePadClose);
    UNHOOK(scePadGetControllerInformation);
#if XBS_HAVE_FN_scePadGetHandle
    UNHOOK(scePadGetHandle);
#endif
#if XBS_HAVE_FN_scePadSetLightBar
    UNHOOK(scePadSetLightBar);
#endif
#if XBS_HAVE_FN_scePadResetLightBar
    UNHOOK(scePadResetLightBar);
#endif
#if XBS_HAVE_FN_scePadSetVibration
    UNHOOK(scePadSetVibration);
#endif
#if XBS_HAVE_FN_scePadResetOrientation
    UNHOOK(scePadResetOrientation);
#endif
#if XBS_HAVE_FN_scePadSetMotionSensorState
    UNHOOK(scePadSetMotionSensorState);
#endif
#if XBS_HAVE_FN_scePadSetTiltCorrectionState
    UNHOOK(scePadSetTiltCorrectionState);
#endif
#if XBS_HAVE_FN_scePadSetAngularVelocityDeadbandState
    UNHOOK(scePadSetAngularVelocityDeadbandState);
#endif
#if XBS_HAVE_FN_scePadDeviceClassParseData
    UNHOOK(scePadDeviceClassParseData);
#endif
#if XBS_HAVE_FN_scePadDeviceClassGetExtendedInformation
    UNHOOK(scePadDeviceClassGetExtendedInformation);
#endif

    if (g_padReadExtPatcher) {
        Patcher_Destroy(g_padReadExtPatcher);
        free(g_padReadExtPatcher);
        g_padReadExtPatcher = NULL;
    }
    if (g_padReadStateExtPatcher) {
        Patcher_Destroy(g_padReadStateExtPatcher);
        free(g_padReadStateExtPatcher);
        g_padReadStateExtPatcher = NULL;
    }

    g_hooks_installed = 0;
    g_runtime_started = 0;
    g_user_prx_loaded = 0;
    g_unknown_user_counter = 0;
}
