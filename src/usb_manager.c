/*
 * USB controller manager implementation. See usb_manager.h.
 *
 * Threads
 *   scanner thread : every 0.5 s looks at the USB device list, opens new
 *                    controllers, removes unplugged/dead ones.
 *   reader threads : one per controller. Blocks in sceUsbdInterruptTransfer
 *                    (timeout 100 ms), parses packets, stores the newest state.
 *
 * Why the game never waits for USB: the game-facing hooks only call
 * usbm_snapshot(), which copies already-stored data under a tiny lock.
 *
 * Same USB calls and parameters as the original working plugin:
 *   sceUsbdOpen -> sceUsbdDetachKernelDriver(0) -> sceUsbdClaimInterface(0)
 *   -> sceUsbdSetInterfaceAltSetting(0,0) -> power-on packet to EP 0x02,
 *   input read from EP 0x82.
 */

#include "usb_manager.h"
#include "config.h"
#include "debug_log.h"
#include "xbox_series.h"
#include "xbs_features.h"

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <orbis/libkernel.h>
#include <orbis/_types/pthread.h>
#include <orbis/Usbd.h>

extern int sys_dynlib_load_prx(const char* path, int* handle);
extern const char* sceKernelGetFsSandboxRandomWord(void);

enum { SLOT_FREE = 0, SLOT_ACTIVE = 1, SLOT_DEAD = 2 };

/* Flags shared between the scanner, reader and game threads are read and
 * written with atomic operations (not just 'volatile'), so every thread
 * always sees a complete, up-to-date value. */
#define LD(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define ST(p, v)  __atomic_store_n((p), (v), __ATOMIC_RELEASE)

typedef struct {
    int                   state;        /* accessed with LD()/ST() only */
    int                   stop_req;     /* accessed with LD()/ST() only */
    libusb_device*        dev;          /* identity only; never dereferenced */
    libusb_device_handle* handle;
    OrbisPthread          thread;
    int                   thread_valid;
    OrbisPthreadMutex     lock;         /* protects the fields below         */
    int                   have_input;
    XbsInput              input;
    uint16_t              latched;
    uint32_t              reports_ok;
    uint32_t              reports_bad;
} Slot;

static Slot g_slots[XBS_MAX_PADS];
static int  g_locks_ready = 0;
static int  g_usb_ready = 0;
static int g_started = 0;       /* accessed with LD()/ST()/CAS only */
static int g_stop = 0;          /* accessed with LD()/ST() only */
static OrbisPthread g_scanner;
static int g_scanner_valid = 0;

/* remember a device that failed to open so we do not spam the user */
static libusb_device* g_last_fail_dev = NULL;
static int g_fail_streak = 0;

static uint64_t now_us(void) { return sceKernelGetProcessTime(); }

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static int is_series_pad(uint16_t vid, uint16_t pid) {
    return vid == XBS_VID_MICROSOFT && pid == XBS_PID_SERIES_USB;
}

static int write_endpoint(libusb_device_handle* h, uint8_t* data, int len) {
    int32_t transferred = 0;
    return sceUsbdInterruptTransfer(h, XBS_EP_OUT, data, len, &transferred,
                                    XBS_USB_WRITE_TIMEOUT_MS);
}

static void send_power_on(libusb_device_handle* h) {
    uint8_t cmd[8];
    int n = xbs_build_power_on(cmd, sizeof(cmd));
    if (n > 0) write_endpoint(h, cmd, n);
}

/* ------------------------------------------------------------------ */
/* reader thread                                                       */
/* ------------------------------------------------------------------ */

/* Writes the first bytes of a raw USB packet to the log (hex). This is how
 * you can check the controller's real packet layout on your own console. */
static int g_c_pkt_in = 0, g_c_pkt_other = 0;

static void log_packet(int idx, const char* what, const uint8_t* buf, int len) {
    char hex[3 * 24 + 1];
    int n = len > 24 ? 24 : len;
    for (int i = 0; i < n; i++) snprintf(hex + i * 3, 4, "%02X ", buf[i]);
    hex[n * 3] = 0;
    XBS_LOG("slot %d raw %s len=%d: %s", idx, what, len, hex);
}

static void handle_packet(int idx, const uint8_t* buf, int len) {
    Slot* s = &g_slots[idx];
    XbsPacket pkt;
    xbs_parse_packet(buf, len, &pkt);

    if (g_settings.log_verbose) {
        if (pkt.kind == XBS_PKT_INPUT) {
            if (LD(&g_c_pkt_in) < 8) {
                __atomic_fetch_add(&g_c_pkt_in, 1, __ATOMIC_RELAXED);
                log_packet(idx, "INPUT", buf, len);
            }
        } else if (LD(&g_c_pkt_other) < 12) {
            __atomic_fetch_add(&g_c_pkt_other, 1, __ATOMIC_RELAXED);
            log_packet(idx, pkt.kind == XBS_PKT_GUIDE ? "GUIDE" :
                            pkt.kind == XBS_PKT_INVALID ? "INVALID" : "OTHER", buf, len);
        }
    }

    if (pkt.kind == XBS_PKT_INPUT) {
        int first = 0;
        scePthreadMutexLock(&s->lock);
        s->input = pkt.input;
        s->latched |= (uint16_t)(pkt.input.buttons & XBS_LATCH_MASK);
        if (!s->have_input) { s->have_input = 1; first = 1; }
        s->reports_ok++;
        scePthreadMutexUnlock(&s->lock);
        if (first) {
            XBS_NOTIFY("Xbox #%d: input OK", idx + 1);
        }
    } else if (pkt.kind == XBS_PKT_GUIDE) {
        if (pkt.needs_ack && g_settings.guide_ack) {
            uint8_t ack[16];
            int n = xbs_build_guide_ack(pkt.seq, ack, sizeof(ack));
            if (n > 0) write_endpoint(s->handle, ack, n);
        }
    } else if (pkt.kind == XBS_PKT_INVALID) {
        scePthreadMutexLock(&s->lock);
        s->reports_bad++;
        scePthreadMutexUnlock(&s->lock);
    }
}

static void* reader_thread(void* arg) {
    int idx = (int)(intptr_t)arg;
    Slot* s = &g_slots[idx];
    uint8_t buf[64];
    int fast_err_streak = 0;
    int init_tries = 1;
    uint64_t last_init = now_us();
    const uint64_t timeout_us = (uint64_t)XBS_USB_READ_TIMEOUT_MS * 1000ULL;

    while (!LD(&s->stop_req) && !LD(&g_stop)) {
        int32_t transferred = 0;
        uint64_t t0 = now_us();
        int ret = sceUsbdInterruptTransfer(s->handle, XBS_EP_IN, buf,
                                           (int)sizeof(buf), &transferred,
                                           XBS_USB_READ_TIMEOUT_MS);
        uint64_t dt = now_us() - t0;

        if (ret == 0) {
            fast_err_streak = 0;
            if (transferred > 0) handle_packet(idx, buf, (int)transferred);
        } else {
            /* A timeout (nothing to report) takes about the full timeout.
             * A broken/unplugged device fails INSTANTLY. We count instant
             * failures; many in a row means the pad is gone or stuck. */
            if (dt < timeout_us / 2) {
                fast_err_streak++;
                if (fast_err_streak >= XBS_FAST_ERROR_LIMIT) {
                    XBS_LOG("slot %d: %d instant USB errors in a row (last 0x%x) -> dead",
                            idx, fast_err_streak, (unsigned)ret);
                    break;
                }
                sceKernelUsleep(10000);
            } else {
                fast_err_streak = 0;
            }
        }

        /* If no valid input has arrived yet, repeat the power-on command once
         * per second (up to 8 times) in case the first one was ignored. */
        if (init_tries < 8) {
            int got;
            scePthreadMutexLock(&s->lock);
            got = s->have_input;
            scePthreadMutexUnlock(&s->lock);
            if (got) {
                init_tries = 8;
            } else if (now_us() - last_init > 1000000ULL) {
                send_power_on(s->handle);
                init_tries++;
                last_init = now_us();
            }
        }
    }

    scePthreadMutexLock(&s->lock);
    ST(&s->state, SLOT_DEAD);
    scePthreadMutexUnlock(&s->lock);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* slot open / close                                                   */
/* ------------------------------------------------------------------ */

static int open_device_into_slot(libusb_device* dev, int idx) {
    Slot* s = &g_slots[idx];
    libusb_device_handle* h = NULL;

    int ret = sceUsbdOpen(dev, &h);
    if (ret != 0 || h == NULL) {
        XBS_LOG("sceUsbdOpen failed: 0x%x", (unsigned)ret);
        return -1;
    }

    sceUsbdDetachKernelDriver(h, XBS_INTERFACE);   /* result intentionally ignored */

    ret = sceUsbdClaimInterface(h, XBS_INTERFACE);
    if (ret != 0) {
        XBS_LOG("sceUsbdClaimInterface failed: 0x%x", (unsigned)ret);
        sceUsbdClose(h);
        return -2;
    }

    sceUsbdSetInterfaceAltSetting(h, XBS_INTERFACE, 0);
    send_power_on(h);

    scePthreadMutexLock(&s->lock);
    s->dev = dev;
    s->handle = h;
    ST(&s->stop_req, 0);
    s->have_input = 0;
    s->latched = 0;
    s->reports_ok = 0;
    s->reports_bad = 0;
    memset(&s->input, 0, sizeof(s->input));
    ST(&s->state, SLOT_ACTIVE);
    scePthreadMutexUnlock(&s->lock);

    ret = scePthreadCreate(&s->thread, 0, (void*)&reader_thread,
                           (void*)(intptr_t)idx, "XbsRead");
    if (ret < 0) {
        XBS_LOG("could not start reader thread: 0x%x", (unsigned)ret);
        sceUsbdReleaseInterface(h, XBS_INTERFACE);
        sceUsbdClose(h);
        scePthreadMutexLock(&s->lock);
        s->handle = NULL;
        s->dev = NULL;
        ST(&s->state, SLOT_FREE);
        scePthreadMutexUnlock(&s->lock);
        return -3;
    }
    s->thread_valid = 1;
    return 0;
}

/* Joins the reader thread and releases USB resources. Slot becomes FREE. */
static void slot_teardown(int idx) {
    Slot* s = &g_slots[idx];

    ST(&s->stop_req, 1);
    if (s->thread_valid) {
        scePthreadJoin(s->thread, 0);
        s->thread_valid = 0;
    }
    if (s->handle) {
        sceUsbdReleaseInterface(s->handle, XBS_INTERFACE);
        sceUsbdClose(s->handle);
        s->handle = NULL;
    }
    scePthreadMutexLock(&s->lock);
    s->dev = NULL;
    s->have_input = 0;
    s->latched = 0;
    ST(&s->state, SLOT_FREE);
    scePthreadMutexUnlock(&s->lock);
}

/* ------------------------------------------------------------------ */
/* scanning                                                            */
/* ------------------------------------------------------------------ */

static int active_count(void) {
    int n = 0;
    for (int i = 0; i < XBS_MAX_PADS; i++)
        if (LD(&g_slots[i].state) == SLOT_ACTIVE && !LD(&g_slots[i].stop_req)) n++;
    return n;
}

static int slot_of_device(libusb_device* dev) {
    for (int i = 0; i < XBS_MAX_PADS; i++)
        if (LD(&g_slots[i].state) == SLOT_ACTIVE && g_slots[i].dev == dev) return i;
    return -1;
}

static int find_free_slot(void) {
    for (int i = 0; i < XBS_MAX_PADS; i++)
        if (LD(&g_slots[i].state) == SLOT_FREE) return i;
    return -1;
}

static void scan_once(void) {
    /* 1. clean up pads whose reader thread has ended */
    for (int i = 0; i < XBS_MAX_PADS; i++) {
        if (LD(&g_slots[i].state) == SLOT_DEAD) {
            slot_teardown(i);
            XBS_NOTIFY("Xbox Series #%d disconnected", i + 1);
        }
    }

    /* 2. look at the USB bus */
    libusb_device** list = NULL;
    int32_t count = sceUsbdGetDeviceList(&list);
    if (count < 0 || list == NULL) return;

    int matching = 0;
    for (int32_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (sceUsbdGetDeviceDescriptor(list[i], &desc) != 0) continue;
        if (is_series_pad(desc.idVendor, desc.idProduct)) matching++;
    }

    /* 3. open controllers we do not have yet.
     *    Only when there are more matching devices on the bus than active
     *    slots; this prevents opening the same controller twice even if the
     *    USB library hands out different device pointers on every call. */
    for (int32_t i = 0; i < count; i++) {
        struct libusb_device_descriptor desc;
        if (sceUsbdGetDeviceDescriptor(list[i], &desc) != 0) continue;
        if (!is_series_pad(desc.idVendor, desc.idProduct)) continue;
        if (slot_of_device(list[i]) >= 0) continue;
        if (matching <= active_count()) continue;

        int idx = find_free_slot();
        if (idx < 0) break;                       /* all slots used */

        if (list[i] == g_last_fail_dev && (g_fail_streak % 4) != 0) {
            g_fail_streak++;                      /* back off: retry every 4th scan */
            continue;
        }

        if (open_device_into_slot(list[i], idx) == 0) {
            g_last_fail_dev = NULL;
            g_fail_streak = 0;
            XBS_NOTIFY("Xbox Series #%d connected", idx + 1);
        } else {
            if (list[i] != g_last_fail_dev) {
                XBS_NOTIFY("Xbox Series: USB open failed");
                g_last_fail_dev = list[i];
                g_fail_streak = 1;
            } else {
                g_fail_streak++;
            }
        }
    }

    /* 4. a pad that vanished from the bus: ask its reader to stop.
     *    Only when fewer matching devices remain than active slots. */
    if (matching < active_count()) {
        for (int i = 0; i < XBS_MAX_PADS; i++) {
            if (LD(&g_slots[i].state) != SLOT_ACTIVE || LD(&g_slots[i].stop_req)) continue;
            int found = 0;
            for (int32_t k = 0; k < count; k++)
                if (list[k] == g_slots[i].dev) { found = 1; break; }
            if (!found) {
                XBS_LOG("slot %d no longer on the USB bus", i);
                ST(&g_slots[i].stop_req, 1);
            }
        }
    }

    sceUsbdFreeDeviceList(list);
}

static void* scanner_thread(void* arg) {
    (void)arg;
    while (!LD(&g_stop)) {
        scan_once();
        for (int i = 0; i < 10 && !LD(&g_stop); i++)
            sceKernelUsleep((XBS_SCAN_INTERVAL_US) / 10);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public API                                                          */
/* ------------------------------------------------------------------ */

int usbm_init(void) {
    if (g_usb_ready) return 0;

    if (!g_locks_ready) {
        for (int i = 0; i < XBS_MAX_PADS; i++) {
            memset(&g_slots[i], 0, sizeof(g_slots[i]));
            if (scePthreadMutexInit(&g_slots[i].lock, 0, "xbsSlotMtx") < 0) {
                XBS_LOG("slot mutex init failed");
                return -3;
            }
        }
        g_locks_ready = 1;
    }

    /* Same sequence as the original plugin: load the USB library, init it. */
    char module[256];
    snprintf(module, sizeof(module), "/%s/common/lib/%s",
             sceKernelGetFsSandboxRandomWord(), "libSceUsbd.sprx");
    int h = 0;
    int ret = sys_dynlib_load_prx(module, &h);
    if (ret < 0 || h == 0) {
        XBS_LOG("libSceUsbd could not be loaded (0x%x)", (unsigned)ret);
        return -1;
    }

    ret = sceUsbdInit();
    if (ret != 0) {
        XBS_LOG("sceUsbdInit failed: 0x%x", (unsigned)ret);
        return -2;
    }
    g_usb_ready = 1;
    XBS_LOG("USB initialised");
    return 0;
}

int usbm_start(void) {
    if (!g_usb_ready) return -1;
    if (!__sync_bool_compare_and_swap(&g_started, 0, 1)) return 0;

    ST(&g_stop, 0);
    scan_once();                                  /* controllers present at start */

    int ret = scePthreadCreate(&g_scanner, 0, (void*)&scanner_thread, NULL, "XbsScan");
    if (ret < 0) {
        XBS_LOG("could not start scanner thread: 0x%x (hot-plug disabled)", (unsigned)ret);
        return -2;
    }
    g_scanner_valid = 1;
    return 0;
}

void usbm_stop(void) {
    ST(&g_stop, 1);
    if (g_scanner_valid) {
        scePthreadJoin(g_scanner, 0);
        g_scanner_valid = 0;
    }
    if (g_locks_ready) {
        for (int i = 0; i < XBS_MAX_PADS; i++) {
            if (LD(&g_slots[i].state) != SLOT_FREE || g_slots[i].handle)
                slot_teardown(i);
        }
    }
    if (g_usb_ready) {
        sceUsbdExit();
        g_usb_ready = 0;
    }
    ST(&g_started, 0);
}

int usbm_is_present(int slot) {
    if (slot < 0 || slot >= XBS_MAX_PADS) return 0;
    return LD(&g_slots[slot].state) == SLOT_ACTIVE && !LD(&g_slots[slot].stop_req);
}

int usbm_present_count(void) {
    int n = 0;
    for (int i = 0; i < XBS_MAX_PADS; i++)
        if (usbm_is_present(i)) n++;
    return n;
}

int usbm_snapshot(int slot, XbsInput* in, uint16_t* latched, int* have_input) {
    if (slot < 0 || slot >= XBS_MAX_PADS || !g_locks_ready) return 0;
    Slot* s = &g_slots[slot];

    scePthreadMutexLock(&s->lock);
    int present = (LD(&s->state) == SLOT_ACTIVE && !LD(&s->stop_req));
    if (present) {
        if (in) *in = s->input;
        if (latched) *latched = s->latched;
        if (have_input) *have_input = s->have_input;
        s->latched = 0;
    }
    scePthreadMutexUnlock(&s->lock);
    return present;
}
