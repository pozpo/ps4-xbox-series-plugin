/*
 * System tests: the REAL usb_manager / virtual_pad / hooks / translator code
 * running against a fake USB bus and a fake "PS4 pad library".
 *
 * These tests prove the LOGIC (assignment, lifecycle, no blocking, hot-plug,
 * no notification spam, timestamps). They cannot prove how a real PS4,
 * a real Series controller, or a real game behaves - that needs the hardware.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

#include "config.h"
#include "debug_log.h"
#include "hooks.h"
#include "usb_manager.h"
#include "virtual_pad.h"
#include "translator.h"
#include "xbox_series.h"
#include "fake_platform.h"

#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/_types/errors.h>

/* hook entry points and "real library" pointers that hooks.c exposes */
int32_t scePadOpen_hook(int32_t, int32_t, int32_t, void*);
int32_t scePadClose_hook(int32_t);
int32_t scePadGetHandle_hook(int32_t, uint32_t, uint32_t);
int32_t scePadGetControllerInformation_hook(int32_t, OrbisPadInformation*);
int32_t scePadRead_hook(int32_t, OrbisPadData*, int32_t);
int32_t scePadReadState_hook(int32_t, OrbisPadData*);
int32_t scePadSetLightBar_hook(int32_t, OrbisPadColor*);
int32_t scePadSetVibration_hook(int32_t, const OrbisPadVibeParam*);
int32_t scePadSetMotionSensorState_hook(int32_t, bool);
extern void *real_scePadOpen, *real_scePadClose, *real_scePadGetControllerInformation,
            *real_scePadGetHandle, *real_scePadSetLightBar, *real_scePadSetVibration,
            *real_scePadSetMotionSensorState, *real_scePadResetLightBar,
            *real_scePadResetOrientation, *real_scePadSetTiltCorrectionState,
            *real_scePadSetAngularVelocityDeadbandState, *real_scePadDeviceClassParseData,
            *real_scePadDeviceClassGetExtendedInformation, *real_scePadRead, *real_scePadReadState;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void msleep(int ms) { usleep((useconds_t)ms * 1000); }

/* ======================= fake PS4 pad library ========================= */

#define U1 0x10000001
#define U2 0x10000002
#define U3 0x10000003
#define U4 0x10000004
#define DS4_MARK 0x00A5A500u        /* distinctive button pattern a real DS4 "sends" */

typedef struct { int32_t user; int32_t handle; int opened; volatile int ds4; } RealPad;
static RealPad g_pads[4];
static int g_next_handle = 1;
static int g_refuse_open_without_ds4 = 0;
static int g_real_open_calls = 0;
static int32_t g_login[4];
static int32_t g_fg;

static RealPad* by_user(int32_t u) { for (int i = 0; i < 4; i++) if (g_pads[i].user == u) return &g_pads[i]; return NULL; }
static RealPad* by_handle(int32_t h) { for (int i = 0; i < 4; i++) if (g_pads[i].opened && g_pads[i].handle == h) return &g_pads[i]; return NULL; }

static int32_t fake_open(int32_t user, int32_t type, int32_t index, void* param) {
    (void)type; (void)index; (void)param;
    g_real_open_calls++;
    RealPad* p = by_user(user);
    if (!p) return ORBIS_PAD_ERROR_DEVICE_NO_HANDLE;
    if (p->opened) return ORBIS_PAD_ERROR_ALREADY_OPENED;
    if (g_refuse_open_without_ds4 && !p->ds4) return ORBIS_PAD_ERROR_DEVICE_NO_HANDLE;
    p->opened = 1;
    p->handle = g_next_handle++;
    return p->handle;
}
static int32_t fake_close(int32_t h) {
    RealPad* p = by_handle(h);
    if (!p) return ORBIS_PAD_ERROR_INVALID_HANDLE;
    p->opened = 0;
    return 0;
}
static int32_t fake_info(int32_t h, OrbisPadInformation* i) {
    RealPad* p = by_handle(h);
    if (!p) return ORBIS_PAD_ERROR_INVALID_HANDLE;
    memset(i, 0, sizeof(*i));
    i->connected = p->ds4 ? 1 : 0;
    return 0;
}
static int32_t fake_gethandle(int32_t user, uint32_t t, uint32_t i) {
    (void)t; (void)i;
    RealPad* p = by_user(user);
    if (p && p->opened) return p->handle;
    return ORBIS_PAD_ERROR_INVALID_HANDLE;
}
static void fill_real(RealPad* p, OrbisPadData* d) {
    memset(d, 0, sizeof(*d));
    d->connected = p->ds4 ? 1 : 0;
    if (p->ds4) {
        d->buttons = DS4_MARK;
        d->leftStick.x = 100; d->leftStick.y = 101;
        d->rightStick.x = 102; d->rightStick.y = 103;
    }
    d->timestamp = sceKernelGetProcessTime();
}
int32_t scePadReadStateExt(int32_t h, OrbisPadData* d) {
    RealPad* p = by_handle(h);
    if (!p) return ORBIS_PAD_ERROR_INVALID_HANDLE;
    fill_real(p, d);
    return 0;
}
int32_t scePadReadExt(int32_t h, OrbisPadData* d, int32_t num) {
    (void)num;
    RealPad* p = by_handle(h);
    if (!p) return ORBIS_PAD_ERROR_INVALID_HANDLE;
    fill_real(p, d);
    return 1;
}
int32_t sceUserServiceGetLoginUserIdList(OrbisUserServiceLoginUserIdList* l) {
    for (int i = 0; i < 4; i++) l->userId[i] = g_login[i];
    return 0;
}
int32_t sceUserServiceGetForegroundUser(int32_t* u) { *u = g_fg; return 0; }

static int32_t ok_setter(void) { return 0; }
static int32_t real_setter_marker(int32_t h) { (void)h; return 0x1234; }
static int32_t real_lightbar(int32_t h, OrbisPadColor* c) { (void)h; (void)c; return 0x1234; }

/* ============================== helpers ================================ */

static int count_notes(const char* needle) { return host_count_notes(needle); }
static void dump_notes(void) {
    for (int i = 0; i < host_note_count; i++) printf("      note: %s\n", host_notes[i]);
}

static void put16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (uint8_t)(v >> 8); }
static void push_input(libusb_device* d, uint16_t btn, uint16_t lt, uint16_t rt,
                       int16_t lx, int16_t ly, int16_t rx, int16_t ry) {
    uint8_t p[18];
    memset(p, 0, sizeof(p));
    p[0] = 0x20; p[3] = 14;
    put16(p + 4, btn); put16(p + 6, lt); put16(p + 8, rt);
    put16(p + 10, (uint16_t)lx); put16(p + 12, (uint16_t)ly);
    put16(p + 14, (uint16_t)rx); put16(p + 16, (uint16_t)ry);
    fake_usb_push(d, p, 18);
}

static int wait_present(int n, int timeout_ms) {
    for (int t = 0; t < timeout_ms; t += 5) {
        if (usbm_present_count() == n) return 1;
        msleep(5);
    }
    return usbm_present_count() == n;
}

static void reset_fake_ps4(int refuse_open) {
    memset(g_pads, 0, sizeof(g_pads));
    g_next_handle = 1;
    g_real_open_calls = 0;
    g_refuse_open_without_ds4 = refuse_open;
    g_pads[0].user = U1; g_pads[1].user = U2; g_pads[2].user = U3; g_pads[3].user = U4;
    g_login[0] = U1; g_login[1] = U2; g_login[2] = U3; g_login[3] = U4;
    g_fg = U1;
}

static void sys_up(void) {
    host_reset_notes();
    xbs_log_init();
    vpad_init();
    usbm_init();
    real_scePadOpen = (void*)fake_open;
    real_scePadClose = (void*)fake_close;
    real_scePadGetControllerInformation = (void*)fake_info;
    real_scePadGetHandle = (void*)fake_gethandle;
    real_scePadSetLightBar = (void*)real_lightbar;
    real_scePadSetVibration = (void*)real_lightbar;
    real_scePadSetMotionSensorState = (void*)real_lightbar;
    real_scePadResetLightBar = (void*)real_setter_marker;
    real_scePadResetOrientation = (void*)real_setter_marker;
    real_scePadSetTiltCorrectionState = (void*)real_lightbar;
    real_scePadSetAngularVelocityDeadbandState = (void*)real_lightbar;
    (void)ok_setter;
    hooks_install();
}
static void sys_down_keep_bus(void) {
    hooks_remove();
    usbm_stop();
    vpad_shutdown();
    xbs_log_close();
}
static void sys_down(void) {
    sys_down_keep_bus();
    fake_usb_reset();
}
static void fresh_settings(void) {
    settings_set_defaults(&g_settings);
    g_settings.log_to_file = 1;
}

static int32_t read_state(int32_t h, OrbisPadData* d) { memset(d, 0xEE, sizeof(*d)); return scePadReadState_hook(h, d); }

/* polls a pad (like a game at ~125 Hz) until it shows Xbox data (connected
 * while its real DS4 is off); returns 1 on success */
static int poll_until_connected(int32_t h, int timeout_ms, OrbisPadData* last) {
    for (int t = 0; t < timeout_ms; t += 4) {
        read_state(h, last);
        if (last->connected) return 1;
        msleep(4);
    }
    return 0;
}

/* ============================== scenarios ============================== */

static void scenario_ds4_p1_xbox_p2(void) {
    printf("[A] DS4 = Player 1, Xbox #1 = Player 2 (PS4 hands out a 'disconnected' real handle)\n");
    fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();

    int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    CHECK(h1 > 0 && h1 < 1000 && h2 > 0 && h2 < 1000 && h1 != h2, "real handles returned (%d,%d)", h1, h2);
    CHECK(wait_present(1, 1500), "Xbox pad detected");

    OrbisPadData d;
    CHECK(read_state(h1, &d) == 0 && d.buttons == DS4_MARK && d.connected == 1 && d.leftStick.x == 100,
          "DS4 data passes through untouched");
    CHECK(read_state(h2, &d) == 0 && d.connected == 0, "right after open, P2 is still its real disconnected data (debounce)");

    push_input(x, XBS_BTN_A | XBS_BTN_MENU, 1023, 0, 20000, 0, 0, 0);
    OrbisPadData last;
    CHECK(poll_until_connected(h2, 1500, &last), "P2 receives Xbox data after debounce");
    CHECK((last.buttons & DS4_BUTTON_CROSS) && (last.buttons & DS4_BUTTON_OPTIONS), "A->Cross, Menu->Options, got 0x%x", last.buttons);
    CHECK(last.analogButtons.l2 == 255 && (last.buttons & DS4_BUTTON_L2), "LT full");
    CHECK(last.leftStick.x > 185, "left stick pushed right, got %d", last.leftStick.x);

    /* DS4 for P1 never affected */
    CHECK(read_state(h1, &d) == 0 && d.buttons == DS4_MARK, "P1 DS4 still untouched");

    /* the 'assigned' message appears exactly once no matter how many reads */
    for (int i = 0; i < 300; i++) { read_state(h2, &d); read_state(h1, &d); }
    CHECK(count_notes("-> Player 2") == 1, "assignment notification exactly once (got %d)", count_notes("-> Player 2"));
    CHECK(count_notes("ready") == 0, "the old 'Xbox Player 2 ready!' message no longer exists");

    /* timestamps strictly increasing */
    uint64_t prev = 0; int mono = 1;
    for (int i = 0; i < 2000; i++) {
        read_state(h2, &d);
        if (d.timestamp <= prev) mono = 0;
        prev = d.timestamp;
    }
    CHECK(mono, "timestamps strictly increase over 2000 reads");

    /* scePadRead returns exactly one newest sample */
    OrbisPadData arr[8];
    memset(arr, 0, sizeof(arr));
    int32_t r = scePadRead_hook(h2, arr, 8);
    CHECK(r == 1 && arr[0].connected == 1 && (arr[0].buttons & DS4_BUTTON_CROSS), "scePadRead(P2) returns 1 Xbox sample");
    r = scePadRead_hook(h1, arr, 8);
    CHECK(r == 1 && arr[0].buttons == DS4_MARK, "scePadRead(P1) is the real DS4 sample");

    /* controller information */
    OrbisPadInformation info;
    CHECK(scePadGetControllerInformation_hook(h2, &info) == 0 && info.connected == 1, "P2 info says connected");
    CHECK(scePadGetControllerInformation_hook(h1, &info) == 0 && info.connected == 1, "P1 info real");

    sys_down();

    /* the log file must contain the diagnostics promised in GUIDE.md */
    FILE* lf = fopen("/tmp/xbs_host/xbox_series.log", "r");
    char logtxt[65536] = {0};
    if (lf) { size_t n = fread(logtxt, 1, sizeof(logtxt) - 1, lf); logtxt[n] = 0; fclose(lf); }
    CHECK(lf != NULL, "log file was written");
    CHECK(strstr(logtxt, "hooks installed") && strstr(logtxt, "USB initialised"), "log: startup lines");
    CHECK(strstr(logtxt, "scePadOpen user=0x10000002") != NULL, "log: scePadOpen line");
    CHECK(strstr(logtxt, "raw INPUT len=18: 20 00 00 0E") != NULL, "log: raw packet hex dump (%s)", strstr(logtxt, "raw INPUT") ? "found other" : "missing");
    CHECK(strstr(logtxt, "Xbox #1 -> Player 2") != NULL, "log: assignment line");
}

static void scenario_stickfight(void) {
    printf("[B] 'Stick Fight' pattern: PS4 refuses to open a pad for a user without DS4\n");
    fresh_settings(); reset_fake_ps4(1); g_pads[0].ds4 = 1;
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();

    int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(h1 > 0 && h1 < 1000, "P1 real handle");
    CHECK(wait_present(1, 1500), "Xbox pad detected");

    int32_t hv = scePadOpen_hook(U2, 0, 0, NULL);
    CHECK(hv >= XBS_VIRTUAL_HANDLE_BASE, "P2 gets a virtual handle (%d)", hv);

    /* the game keeps asking: GetHandle must now answer, Open must say 'already' */
    int bad = 0;
    for (int i = 0; i < 200; i++) {
        if (scePadGetHandle_hook(U2, 0, 0) != hv) bad++;
        if (scePadOpen_hook(U2, 0, 0, NULL) != ORBIS_PAD_ERROR_ALREADY_OPENED) bad++;
    }
    CHECK(bad == 0, "GetHandle returns the virtual handle and repeated Open returns ALREADY_OPENED (bad=%d)", bad);
    CHECK(count_notes("ready") == 0, "no 'ready' spam");
    CHECK(host_count_notes("") <= 4, "total notifications stay small after 200 repeated opens (got %d)", host_count_notes(""));
    if (host_count_notes("") > 4) dump_notes();

    push_input(x, XBS_BTN_X, 0, 1023, 0, 0, 0, 0);
    OrbisPadData d;
    CHECK(read_state(hv, &d) == 0 && d.connected == 1, "ReadState(virtual) succeeds and is connected");
    msleep(30);
    int ok = 0;
    for (int i = 0; i < 50 && !ok; i++) { read_state(hv, &d); ok = (d.buttons & DS4_BUTTON_SQUARE) && (d.buttons & DS4_BUTTON_R2); msleep(2); }
    CHECK(ok, "X->Square and RT->R2 arrive through the virtual handle");
    CHECK(count_notes("input OK") == 1, "'input OK' appears once real data has arrived (%d)", count_notes("input OK"));

    OrbisPadInformation info;
    CHECK(scePadGetControllerInformation_hook(hv, &info) == 0 && info.connected == 1, "info for virtual pad");

    /* setters */
    OrbisPadColor c = {1, 2, 3, 4};
    OrbisPadVibeParam v = {1, 1};
    CHECK(scePadSetLightBar_hook(hv, &c) == 0, "SetLightBar(virtual) OK");
    CHECK(scePadSetVibration_hook(hv, &v) == 0, "SetVibration(virtual) OK");
    CHECK(scePadSetMotionSensorState_hook(hv, true) == 0, "SetMotionSensorState(virtual) OK");
    CHECK(scePadSetLightBar_hook(h1, &c) == 0x1234, "SetLightBar(real) passes through");

    /* lifecycle: close, then everything about that handle is gone */
    CHECK(scePadClose_hook(hv) == 0, "close virtual");
    CHECK(scePadGetHandle_hook(U2, 0, 0) == ORBIS_PAD_ERROR_INVALID_HANDLE, "GetHandle after close falls through to PS4 (no pad)");
    CHECK(read_state(hv, &d) == ORBIS_PAD_ERROR_INVALID_HANDLE, "reading a closed virtual handle is an error");
    int32_t hv2 = scePadOpen_hook(U2, 0, 0, NULL);
    CHECK(hv2 >= XBS_VIRTUAL_HANDLE_BASE, "reopen works (%d)", hv2);

    /* argument checks */
    CHECK(scePadReadState_hook(hv2, NULL) == ORBIS_PAD_ERROR_INVALID_ARG, "NULL data rejected");
    CHECK(scePadRead_hook(hv2, &d, 0) == ORBIS_PAD_ERROR_INVALID_ARG, "num=0 rejected");
    sys_down();
}

static void scenario_two_xbox(void) {
    printf("[C] DS4 = P1, Xbox #1 = P2, Xbox #2 = P3 (both polling orders)\n");
    for (int order = 0; order < 2; order++) {
        fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
        libusb_device* xa = fake_usb_plug(0x045E, 0x0B12);
        sys_up();
        int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
        CHECK(wait_present(1, 1500), "first pad detected");
        libusb_device* xb = fake_usb_plug(0x045E, 0x0B12);
        CHECK(wait_present(2, 2500), "second pad detected (hot-plug, same VID/PID)");
        CHECK(fake_usb_open_count(xa) == 1 && fake_usb_open_count(xb) == 1, "each physical pad opened exactly once");

        int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
        int32_t h3 = scePadOpen_hook(U3, 0, 0, NULL);
        push_input(xa, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
        push_input(xb, XBS_BTN_B, 0, 0, 0, 0, 0, 0);

        OrbisPadData d2, d3;
        memset(&d2, 0, sizeof(d2)); memset(&d3, 0, sizeof(d3));
        int ok2 = 0, ok3 = 0;
        for (int t = 0; t < 1500 && !(ok2 && ok3); t += 4) {
            if (order == 0) { read_state(h2, &d2); read_state(h3, &d3); }
            else            { read_state(h3, &d3); read_state(h2, &d2); }
            ok2 = d2.connected && (d2.buttons & DS4_BUTTON_CROSS);
            ok3 = d3.connected && (d3.buttons & DS4_BUTTON_CIRCLE);
            msleep(4);
        }
        CHECK(ok2, "order %d: Player 2 got Xbox #1 (Cross), buttons=0x%x", order, d2.buttons);
        CHECK(ok3, "order %d: Player 3 got Xbox #2 (Circle), buttons=0x%x", order, d3.buttons);
        CHECK(!(d2.buttons & DS4_BUTTON_CIRCLE) && !(d3.buttons & DS4_BUTTON_CROSS), "no cross-talk between pads");
        CHECK(count_notes("Xbox #1 -> Player 2") == 1 && count_notes("Xbox #2 -> Player 3") == 1,
              "one assignment message each");
        OrbisPadData d1;
        read_state(h1, &d1);
        CHECK(d1.buttons == DS4_MARK, "DS4 P1 unaffected");
        if (!(ok2 && ok3)) dump_notes();
        sys_down();
    }
}

static void scenario_ds4_p1_ds4_p3_xbox_p2(void) {
    printf("[D] DS4 = P1, Xbox = P2, DS4 = P3 (P3 opens/reads first)\n");
    fresh_settings(); reset_fake_ps4(0);
    g_pads[0].ds4 = 1; g_pads[2].ds4 = 1;
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    int32_t h3 = scePadOpen_hook(U3, 0, 0, NULL);     /* P3 opens BEFORE P2 */
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad detected");
    push_input(x, XBS_BTN_Y, 0, 0, 0, 0, 0, 0);
    OrbisPadData d2, d3, d1;
    int ok = 0;
    for (int t = 0; t < 1500 && !ok; t += 4) {
        read_state(h3, &d3); read_state(h2, &d2); read_state(h1, &d1);
        ok = d2.connected && (d2.buttons & DS4_BUTTON_TRIANGLE);
        msleep(4);
    }
    CHECK(ok, "Xbox goes to P2 even though P3 opened first");
    CHECK(d3.buttons == DS4_MARK && d1.buttons == DS4_MARK, "both real DS4 pads untouched");
    CHECK(count_notes("Player 3") == 0, "P3 never gets an Xbox pad");
    sys_down();
}

static void scenario_late_plug_unplug(void) {
    printf("[E/F] Xbox plugged in AFTER the game started, then unplugged and re-plugged\n");
    fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    sys_up();
    int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    CHECK(usbm_present_count() == 0, "no Xbox yet");
    OrbisPadData d;
    for (int i = 0; i < 20; i++) { read_state(h2, &d); msleep(5); }
    CHECK(d.connected == 0, "P2 reads as disconnected while no Xbox is plugged");

    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    push_input(x, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    CHECK(wait_present(1, 2500), "Xbox detected by the background scanner");
    CHECK(poll_until_connected(h2, 2000, &d), "P2 starts receiving Xbox data without any new scePadOpen");
    CHECK(d.buttons & DS4_BUTTON_CROSS, "data is correct");
    CHECK(count_notes("Xbox Series #1 connected") == 1, "connect notification once");

    /* unplug */
    fake_usb_unplug(x);
    CHECK(wait_present(0, 3000), "unplug noticed");
    OrbisPadData g;
    int disc = 0;
    for (int i = 0; i < 50 && !disc; i++) { read_state(h2, &g); disc = (g.connected == 0); msleep(5); }
    CHECK(disc && g.buttons == 0, "after unplug the game sees a disconnected, neutral pad");
    msleep(700);
    CHECK(count_notes("disconnected") == 1, "disconnect notification once (%d)", count_notes("disconnected"));

    /* replug a controller */
    libusb_device* y = fake_usb_plug(0x045E, 0x0B12);
    push_input(y, XBS_BTN_B, 0, 0, 0, 0, 0, 0);
    CHECK(wait_present(1, 3000), "re-plugged pad detected");
    int ok = 0;
    for (int i = 0; i < 400 && !ok; i++) { read_state(h2, &d); ok = d.connected && (d.buttons & DS4_BUTTON_CIRCLE); msleep(4); }
    CHECK(ok, "P2 gets the replugged pad back");
    CHECK(count_notes("-> Player 2") == 1, "no second assignment message after replug (%d)", count_notes("-> Player 2"));
    CHECK(read_state(h1, &d) == 0 && d.buttons == DS4_MARK, "P1 DS4 unaffected throughout");
    sys_down();
}

static void scenario_ds4_reconnect(void) {
    printf("[G] DS4 turns on for P2 later: Xbox must step aside, then return when DS4 turns off\n");
    fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    push_input(x, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    OrbisPadData d;
    CHECK(poll_until_connected(h2, 1500, &d), "Xbox bound to P2");
    g_pads[1].ds4 = 1;                       /* P2 turns on a real DS4 */
    msleep(20);
    read_state(h2, &d);
    CHECK(d.buttons == DS4_MARK, "real DS4 wins as soon as it is connected");
    g_pads[1].ds4 = 0;
    CHECK(poll_until_connected(h2, 2000, &d), "Xbox takes over again after the DS4 turns off");
    sys_down();
}

static void scenario_policy(void) {
    printf("[H] Policy: Player 1 protected by default; allow_player1; xbox_players mask\n");
    /* default: P1 without DS4 does NOT get the Xbox pad */
    fresh_settings(); reset_fake_ps4(0);
    fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    int32_t h1 = scePadOpen_hook(U1, 0, 0, NULL);
    OrbisPadData d;
    CHECK(wait_present(1, 1500), "pad");
    int got = 0;
    for (int t = 0; t < 800; t += 4) { read_state(h1, &d); if (d.connected) got = 1; msleep(4); }
    CHECK(!got, "foreground user never gets an Xbox pad by default");
    sys_down();

    /* allow_player1=1 */
    fresh_settings(); g_settings.allow_foreground_user = 1; reset_fake_ps4(0);
    fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    h1 = scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    CHECK(poll_until_connected(h1, 1500, &d), "allow_player1=1: P1 can use the Xbox pad");
    sys_down();

    /* xbox_players=3 : only the user at login position 3 */
    fresh_settings(); g_settings.player_mask = (1u << 3); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    int32_t h3 = scePadOpen_hook(U3, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    int p2 = 0, p3 = 0;
    for (int t = 0; t < 1200 && !p3; t += 4) {
        read_state(h2, &d); if (d.connected) p2 = 1;
        read_state(h3, &d); if (d.connected) p3 = 1;
        msleep(4);
    }
    CHECK(p3 && !p2, "xbox_players=3: Player 3 gets it, Player 2 does not");
    sys_down();

    /* enabled=0 : nothing is intercepted */
    fresh_settings(); g_settings.enabled = 0; reset_fake_ps4(0); g_pads[0].ds4 = 1;
    sys_up();
    int before = g_real_open_calls;
    int32_t hh = scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(hh > 0 && g_real_open_calls == before + 1 && !vpad_is_tracked(hh), "enabled=0: plain passthrough, nothing tracked");
    sys_down();

    /* non-standard pad type passes straight through */
    fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    sys_up();
    before = g_real_open_calls;
    hh = scePadOpen_hook(U1, 1 /* special pad type */, 0, NULL);
    CHECK(hh > 0 && g_real_open_calls == before + 1 && !vpad_is_tracked(hh), "special pad type is not intercepted");
    sys_down();
}

static void scenario_input_quality(void) {
    printf("[I] Input quality: latch, garbage packets, no blocking, latency\n");
    fresh_settings(); reset_fake_ps4(1); g_pads[0].ds4 = 1;
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    int32_t hv = scePadOpen_hook(U2, 0, 0, NULL);
    OrbisPadData d;

    /* a very short tap: pressed then released before the game polls */
    push_input(x, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    push_input(x, 0, 0, 0, 0, 0, 0, 0);
    msleep(40);
    read_state(hv, &d);
    CHECK(d.buttons & DS4_BUTTON_CROSS, "tap shorter than one poll is NOT lost (latched)");
    read_state(hv, &d);
    CHECK(!(d.buttons & DS4_BUTTON_CROSS), "and it is released on the next read");

    /* D-pad is not latched */
    push_input(x, XBS_BTN_LEFT, 0, 0, 0, 0, 0, 0);
    push_input(x, 0, 0, 0, 0, 0, 0, 0);
    msleep(40);
    read_state(hv, &d);
    CHECK(!(d.buttons & DS4_BUTTON_DPAD_LEFT), "D-pad is deliberately not latched");

    /* garbage between valid packets must not change the state (old bug) */
    push_input(x, XBS_BTN_Y, 0, 0, 0, 0, 0, 0);
    uint8_t announce[28] = {0x02, 0x20, 1, 24};
    uint8_t shortp[5] = {0x20, 0, 1, 14, 0xFF};
    uint8_t zero[1] = {0};
    fake_usb_push(x, announce, 28);
    fake_usb_push(x, shortp, 5);
    fake_usb_push(x, zero, 1);
    msleep(40);
    read_state(hv, &d); read_state(hv, &d);
    CHECK((d.buttons & DS4_BUTTON_TRIANGLE) && !(d.buttons & DS4_BUTTON_CROSS),
          "non-input / truncated packets do not corrupt the state (buttons=0x%x)", d.buttons);

    /* game-facing calls never wait for USB, even with an idle controller */
    uint64_t t0 = sceKernelGetProcessTime();
    for (int i = 0; i < 20000; i++) read_state(hv, &d);
    uint64_t dt = sceKernelGetProcessTime() - t0;
    CHECK(dt < 400000, "20000 ReadState calls took %llu us (old design: >= 2 ms each when idle)", (unsigned long long)dt);

    /* latency from USB packet arrival to visible in ReadState */
    push_input(x, 0, 0, 0, 0, 0, 0, 0);
    msleep(30); read_state(hv, &d); read_state(hv, &d);
    uint64_t worst = 0; int ok = 1;
    for (int n = 0; n < 20; n++) {
        uint64_t s = sceKernelGetProcessTime();
        push_input(x, XBS_BTN_B, 0, 0, 0, 0, 0, 0);
        int seen = 0;
        while (sceKernelGetProcessTime() - s < 100000) {
            read_state(hv, &d);
            if (d.buttons & DS4_BUTTON_CIRCLE) { seen = 1; break; }
        }
        uint64_t lat = sceKernelGetProcessTime() - s;
        if (!seen) ok = 0;
        if (lat > worst) worst = lat;
        push_input(x, 0, 0, 0, 0, 0, 0, 0);
        msleep(20); read_state(hv, &d); read_state(hv, &d);
    }
    CHECK(ok && worst < 50000, "worst packet->game latency over 20 presses (limit 50 ms, loaded CI machines jitter): %llu us", (unsigned long long)worst);
    printf("      (worst latency %llu us)\n", (unsigned long long)worst);
    sys_down();
}

static void scenario_usb_protocol(void) {
    printf("[J] USB protocol behaviour: power-on, retry, guide ack, claim failure\n");
    fresh_settings(); reset_fake_ps4(0);
    libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
    libusb_device* other = fake_usb_plug(0x1234, 0x5678);   /* not an Xbox pad */
    (void)other;
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    int len = 0;
    const uint8_t* p = fake_usb_out_packet(x, 0, &len);
    CHECK(fake_usb_out_count(x) >= 1 && len == 5 && memcmp(p, "\x05\x20\x00\x01\x00", 5) == 0, "power-on sent first");
    msleep(2300);
    CHECK(fake_usb_out_count(x) >= 3, "power-on is repeated while no input arrives (sent %d)", fake_usb_out_count(x));
    int before = fake_usb_out_count(x);
    push_input(x, 0, 0, 0, 0, 0, 0, 0);
    msleep(1300);
    CHECK(fake_usb_out_count(x) <= before + 1, "no more repeats once input arrived");

    /* guide report asking for an ack */
    uint8_t guide[6] = {0x07, 0x30, 0x2A, 0x02, 0x01, 0x5B};
    int n0 = fake_usb_out_count(x);
    fake_usb_push(x, guide, 6);
    msleep(60);
    CHECK(fake_usb_out_count(x) == n0 + 1, "one ack sent for a guide report");
    p = fake_usb_out_packet(x, n0, &len);
    CHECK(len == 13 && p[0] == 0x01 && p[2] == 0x2A && p[5] == 0x07, "ack content/sequence");
    sys_down();

    /* a pad that cannot be claimed: one notification only, no spam */
    fresh_settings(); reset_fake_ps4(0);
    libusb_device* bad = fake_usb_plug(0x045E, 0x0B12);
    fake_usb_set_fail_claim(bad, 1);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    msleep(4200);
    CHECK(usbm_present_count() == 0, "unclaimable pad is not present");
    CHECK(count_notes("USB open failed") == 1, "claim failure reported once, not every scan (%d)", count_notes("USB open failed"));
    CHECK(fake_usb_open_count(bad) == 0, "failed device handle was closed again (leak check)");
    sys_down();

    /* guide ack can be disabled */
    fresh_settings(); g_settings.guide_ack = 0; reset_fake_ps4(0);
    x = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "pad");
    n0 = fake_usb_out_count(x);
    fake_usb_push(x, guide, 6);
    msleep(60);
    CHECK(fake_usb_out_count(x) == n0, "guide_ack=0 sends nothing");
    sys_down();
}

static void scenario_four_pads_and_limits(void) {
    printf("[K] Limits: five controllers, table full, repeated plugin reload\n");
    fresh_settings(); reset_fake_ps4(0);
    libusb_device* d[5];
    for (int i = 0; i < 5; i++) d[i] = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(XBS_MAX_PADS, 2000), "up to %d pads are opened", XBS_MAX_PADS);
    msleep(1200);
    CHECK(usbm_present_count() == XBS_MAX_PADS, "the fifth pad is ignored, no crash");
    int opened = 0;
    for (int i = 0; i < 5; i++) opened += fake_usb_open_count(d[i]);
    CHECK(opened == XBS_MAX_PADS, "exactly %d USB handles open (got %d)", XBS_MAX_PADS, opened);
    sys_down();
    for (int round = 0; round < 3; round++) {          /* reload cycles must not leak/crash */
        fresh_settings(); reset_fake_ps4(0);
        libusb_device* x = fake_usb_plug(0x045E, 0x0B12);
        sys_up();
        scePadOpen_hook(U1, 0, 0, NULL);
        CHECK(wait_present(1, 1500), "reload round %d: pad found", round);
        sys_down_keep_bus();
        CHECK(fake_usb_open_count(x) == 0, "reload round %d: USB handle released on unload", round);
        fake_usb_reset();
    }
}


static void scenario_two_pads_partial_unplug(void) {
    printf("[L] Two Xbox pads: unplug the FIRST one, the second must keep working; replug goes back to Player 2\n");
    fresh_settings(); reset_fake_ps4(0); g_pads[0].ds4 = 1;
    libusb_device* xa = fake_usb_plug(0x045E, 0x0B12);
    sys_up();
    scePadOpen_hook(U1, 0, 0, NULL);
    CHECK(wait_present(1, 1500), "first pad");
    libusb_device* xb = fake_usb_plug(0x045E, 0x0B12);
    CHECK(wait_present(2, 2500), "second pad");
    int32_t h2 = scePadOpen_hook(U2, 0, 0, NULL);
    int32_t h3 = scePadOpen_hook(U3, 0, 0, NULL);
    push_input(xa, XBS_BTN_A, 0, 0, 0, 0, 0, 0);
    push_input(xb, XBS_BTN_B, 0, 0, 0, 0, 0, 0);
    OrbisPadData d2, d3;
    int ok = 0;
    for (int t = 0; t < 1500 && !ok; t += 4) {
        read_state(h2, &d2); read_state(h3, &d3);
        ok = d2.connected && d3.connected && (d2.buttons & DS4_BUTTON_CROSS) && (d3.buttons & DS4_BUTTON_CIRCLE);
        msleep(4);
    }
    CHECK(ok, "both players have their own pad");

    fake_usb_unplug(xa);
    int gone = 0;
    for (int t = 0; t < 3000 && !gone; t += 20) { gone = (usbm_present_count() == 1); msleep(20); }
    CHECK(gone, "exactly one pad remains after unplugging the first");
    int p2_off = 0;
    for (int i = 0; i < 100 && !p2_off; i++) { read_state(h2, &d2); p2_off = !d2.connected; msleep(5); }
    CHECK(p2_off, "Player 2 sees a disconnected pad");
    read_state(h3, &d3);
    CHECK(d3.connected && (d3.buttons & DS4_BUTTON_CIRCLE), "Player 3 is completely unaffected");
    push_input(xb, XBS_BTN_X, 0, 0, 0, 0, 0, 0);
    int ok3 = 0;
    for (int i = 0; i < 200 && !ok3; i++) { read_state(h3, &d3); ok3 = (d3.buttons & DS4_BUTTON_SQUARE); msleep(4); }
    CHECK(ok3, "Player 3 still receives new input");

    libusb_device* xc = fake_usb_plug(0x045E, 0x0B12);
    push_input(xc, XBS_BTN_Y, 0, 0, 0, 0, 0, 0);
    CHECK(wait_present(2, 3000), "replugged pad detected");
    int ok2 = 0;
    for (int i = 0; i < 500 && !ok2; i++) { read_state(h2, &d2); ok2 = d2.connected && (d2.buttons & DS4_BUTTON_TRIANGLE); msleep(4); }
    CHECK(ok2, "the new pad goes to Player 2 again (slot and user kept)");
    read_state(h3, &d3);
    CHECK(!(d3.buttons & DS4_BUTTON_TRIANGLE), "and not to Player 3");
    CHECK(fake_usb_open_count(xb) == 1 && fake_usb_open_count(xc) == 1, "each live pad is open exactly once");
    sys_down();
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    mkdir("/tmp/xbs_host", 0777);
    scenario_ds4_p1_xbox_p2();
    scenario_stickfight();
    scenario_two_xbox();
    scenario_ds4_p1_ds4_p3_xbox_p2();
    scenario_late_plug_unplug();
    scenario_ds4_reconnect();
    scenario_policy();
    scenario_input_quality();
    scenario_usb_protocol();
    scenario_two_pads_partial_unplug();
    scenario_four_pads_and_limits();
    printf("\ntest_system: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
