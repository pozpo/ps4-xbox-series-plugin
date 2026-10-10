/*
 * Host-test platform: implements the OpenOrbis kernel functions with pthreads
 * and a fake USB bus so the REAL plugin sources can run on a normal PC.
 */
#define _GNU_SOURCE
#include "fake_platform.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <GoldHEN.h>

/* ---------------- side-effect counters ---------------- */
int host_patch_calls, host_detour_calls, host_usbd_init_calls, host_dynlib_calls,
    host_thread_creates, host_mutex_inits, host_log_opens, host_sdk_proc_info_calls;
#define BUMP(c) __atomic_fetch_add(&(c), 1, __ATOMIC_SEQ_CST)
void host_reset_counters(void) {
    host_patch_calls = host_detour_calls = host_usbd_init_calls = host_dynlib_calls = 0;
    host_thread_creates = host_mutex_inits = host_log_opens = host_sdk_proc_info_calls = 0;
}
int host_counters_total(void) {
    return host_patch_calls + host_detour_calls + host_usbd_init_calls + host_dynlib_calls +
           host_thread_creates + host_mutex_inits + host_log_opens;
}

/* ---------------- fake GoldHEN process info ---------------- */
static char g_fake_title[16];
static int  g_fake_title_ok = 0;
void host_set_title(const char* titleid) {
    memset(g_fake_title, 0, sizeof(g_fake_title));
    g_fake_title_ok = (titleid != NULL);
    if (titleid) snprintf(g_fake_title, sizeof(g_fake_title), "%s", titleid);
}
int sys_sdk_proc_info(struct proc_info* info) {
    BUMP(host_sdk_proc_info_calls);
    if (!g_fake_title_ok) return -1;                    /* failure: struct untouched */
    snprintf(info->name, sizeof(info->name), "eboot.bin");
    memcpy(info->titleid, g_fake_title, sizeof(info->titleid));
    return 0;
}

/* ---------------- kernel ---------------- */

int scePthreadMutexInit(OrbisPthreadMutex* m, const void* attr, const char* name) {
    (void)attr; (void)name;
    BUMP(host_mutex_inits);
    *m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(*m, NULL);
    return 0;
}
int scePthreadMutexLock(OrbisPthreadMutex* m) { return pthread_mutex_lock(*m); }
int scePthreadMutexUnlock(OrbisPthreadMutex* m) { return pthread_mutex_unlock(*m); }

int scePthreadCreate(OrbisPthread* t, const void* attr, void* fn, void* arg, const char* name) {
    (void)attr; (void)name;
    BUMP(host_thread_creates);
    return pthread_create(t, NULL, (void* (*)(void*))fn, arg);
}
int scePthreadJoin(OrbisPthread t, void** ret) { return pthread_join(t, ret); }

int sceKernelUsleep(unsigned int us) { return usleep(us); }

uint64_t sceKernelGetProcessTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* paths under /data/GoldHEN/ are redirected to /tmp/xbs_host/ */
static void map_path(const char* in, char* out, size_t cap) {
    const char* prefix = "/data/GoldHEN/";
    if (strncmp(in, prefix, strlen(prefix)) == 0)
        snprintf(out, cap, "/tmp/xbs_host/%s", in + strlen(prefix));
    else
        snprintf(out, cap, "%s", in);
}
int sceKernelOpen(const char* path, int flags, int mode) {
    char p[300];
    map_path(path, p, sizeof(p));
    if (flags & (0x0001 | 0x0200 | 0x0400)) BUMP(host_log_opens);   /* any write/create/truncate open */
    int f = 0;
    if (flags & 0x0001) f |= O_WRONLY;
    if (flags & 0x0200) f |= O_CREAT;
    if (flags & 0x0400) f |= O_TRUNC;
    return open(p, f, mode);
}
long sceKernelRead(int fd, void* buf, size_t n) { return read(fd, buf, n); }
long sceKernelWrite(int fd, const void* buf, size_t n) { return write(fd, buf, n); }
int sceKernelClose(int fd) { return close(fd); }

char host_notes[HOST_MAX_NOTES][128];
int host_note_count = 0;
static pthread_mutex_t g_note_mutex = PTHREAD_MUTEX_INITIALIZER;

int host_count_notes(const char* needle) {
    int n = 0;
    pthread_mutex_lock(&g_note_mutex);
    for (int i = 0; i < host_note_count; i++) if (strstr(host_notes[i], needle)) n++;
    pthread_mutex_unlock(&g_note_mutex);
    return n;
}
void host_reset_notes(void) { pthread_mutex_lock(&g_note_mutex); host_note_count = 0; pthread_mutex_unlock(&g_note_mutex); }

int sceKernelSendNotificationRequest(int dev, OrbisNotificationRequest* r, size_t sz, int blocking) {
    (void)dev; (void)sz; (void)blocking;
    pthread_mutex_lock(&g_note_mutex);
    if (host_note_count < HOST_MAX_NOTES) {
        snprintf(host_notes[host_note_count], sizeof(host_notes[0]), "%.100s", r->message);
        host_note_count++;
    }
    pthread_mutex_unlock(&g_note_mutex);
    return 0;
}

int sys_dynlib_load_prx(const char* path, int* handle) { (void)path; BUMP(host_dynlib_calls); *handle = 1; return 0; }
const char* sceKernelGetFsSandboxRandomWord(void) { return "randomword"; }

/* ---------------- fake USB ---------------- */

#define QMAX 512

struct libusb_device {
    uint16_t vid, pid;
    int attached;
    int claimed;
    int open_count;
    pthread_mutex_t m;
    uint8_t q[QMAX][64];
    int qlen[QMAX];
    int head, tail;
    uint8_t out_log[16][16];
    int out_len[16];
    int out_count;
    int fail_claim;
};
struct libusb_device_handle { libusb_device* dev; };

static libusb_device* g_devs[16];
static int g_dev_count = 0;
static pthread_mutex_t g_bus_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_usb_inited = 0;
int fake_usb_unstable_pointers = 0;   /* 1: GetDeviceList hands out fresh pointers each call */

libusb_device* fake_usb_plug(uint16_t vid, uint16_t pid) {
    libusb_device* d = (libusb_device*)calloc(1, sizeof(*d));
    d->vid = vid; d->pid = pid; d->attached = 1;
    pthread_mutex_init(&d->m, NULL);
    pthread_mutex_lock(&g_bus_mutex);
    g_devs[g_dev_count++] = d;
    pthread_mutex_unlock(&g_bus_mutex);
    return d;
}
void fake_usb_unplug(libusb_device* d) {
    pthread_mutex_lock(&g_bus_mutex);
    __atomic_store_n(&d->attached, 0, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&g_bus_mutex);
}
void fake_usb_push(libusb_device* d, const uint8_t* data, int len) {
    pthread_mutex_lock(&d->m);
    int next = (d->tail + 1) % QMAX;
    if (next != d->head) {
        memcpy(d->q[d->tail], data, (size_t)len);
        d->qlen[d->tail] = len;
        d->tail = next;
    }
    pthread_mutex_unlock(&d->m);
}
int fake_usb_out_count(libusb_device* d) { pthread_mutex_lock(&d->m); int n = d->out_count; pthread_mutex_unlock(&d->m); return n; }
const uint8_t* fake_usb_out_packet(libusb_device* d, int i, int* len) { pthread_mutex_lock(&d->m); *len = d->out_len[i]; pthread_mutex_unlock(&d->m); return d->out_log[i]; }
int fake_usb_open_count(libusb_device* d) { return __atomic_load_n(&d->open_count, __ATOMIC_SEQ_CST); }
void fake_usb_set_fail_claim(libusb_device* d, int v) { d->fail_claim = v; }
void fake_usb_reset(void) {
    pthread_mutex_lock(&g_bus_mutex);
    for (int i = 0; i < g_dev_count; i++) { free(g_devs[i]); g_devs[i] = NULL; }
    g_dev_count = 0;
    pthread_mutex_unlock(&g_bus_mutex);
}

int sceUsbdInit(void) { BUMP(host_usbd_init_calls); g_usb_inited = 1; return 0; }
void sceUsbdExit(void) { g_usb_inited = 0; }

int32_t sceUsbdGetDeviceList(libusb_device*** list) {
    pthread_mutex_lock(&g_bus_mutex);
    libusb_device** arr = (libusb_device**)calloc((size_t)g_dev_count + 1, sizeof(*arr));
    int n = 0;
    for (int i = 0; i < g_dev_count; i++)
        if (__atomic_load_n(&g_devs[i]->attached, __ATOMIC_SEQ_CST)) arr[n++] = g_devs[i];
    pthread_mutex_unlock(&g_bus_mutex);
    *list = arr;
    return n;
}
void sceUsbdFreeDeviceList(libusb_device** list) { free(list); }
int sceUsbdGetDeviceDescriptor(libusb_device* dev, struct libusb_device_descriptor* d) {
    d->idVendor = dev->vid; d->idProduct = dev->pid; return 0;
}
int sceUsbdOpen(libusb_device* dev, libusb_device_handle** h) {
    if (!__atomic_load_n(&dev->attached, __ATOMIC_SEQ_CST)) return (int)0x80240004;
    libusb_device_handle* hh = (libusb_device_handle*)calloc(1, sizeof(*hh));
    hh->dev = dev;
    __atomic_fetch_add(&dev->open_count, 1, __ATOMIC_SEQ_CST);
    *h = hh;
    return 0;
}
void sceUsbdClose(libusb_device_handle* h) { __atomic_fetch_sub(&h->dev->open_count, 1, __ATOMIC_SEQ_CST); free(h); }
int sceUsbdDetachKernelDriver(libusb_device_handle* h, int i) { (void)h; (void)i; return 0; }
int sceUsbdClaimInterface(libusb_device_handle* h, int i) {
    (void)i;
    if (h->dev->fail_claim) return (int)0x80240006;
    if (__atomic_exchange_n(&h->dev->claimed, 1, __ATOMIC_SEQ_CST)) return (int)0x80240006;   /* busy: cannot claim twice */
    return 0;
}
int sceUsbdReleaseInterface(libusb_device_handle* h, int i) { (void)i; __atomic_store_n(&h->dev->claimed, 0, __ATOMIC_SEQ_CST); return 0; }
int sceUsbdSetInterfaceAltSetting(libusb_device_handle* h, int i, int a) { (void)h; (void)i; (void)a; return 0; }

int sceUsbdInterruptTransfer(libusb_device_handle* h, uint8_t ep, uint8_t* data, int len,
                             int32_t* transferred, unsigned timeout_ms) {
    libusb_device* d = h->dev;
    *transferred = 0;
    if (ep == 0x02) {
        if (!__atomic_load_n(&d->attached, __ATOMIC_SEQ_CST)) return (int)0x80240004;
        pthread_mutex_lock(&d->m);
        if (d->out_count < 16) {
            int n = len > 16 ? 16 : len;
            memcpy(d->out_log[d->out_count], data, (size_t)n);
            d->out_len[d->out_count] = n;
            d->out_count++;
        }
        pthread_mutex_unlock(&d->m);
        *transferred = len;
        return 0;
    }
    /* IN endpoint: wait up to timeout for a queued packet */
    uint64_t start = sceKernelGetProcessTime();
    for (;;) {
        if (!__atomic_load_n(&d->attached, __ATOMIC_SEQ_CST)) return (int)0x80240004;   /* fails instantly */
        pthread_mutex_lock(&d->m);
        if (d->head != d->tail) {
            int n = d->qlen[d->head];
            if (n > len) n = len;
            memcpy(data, d->q[d->head], (size_t)n);
            d->head = (d->head + 1) % QMAX;
            pthread_mutex_unlock(&d->m);
            *transferred = n;
            return 0;
        }
        pthread_mutex_unlock(&d->m);
        if (sceKernelGetProcessTime() - start >= (uint64_t)timeout_ms * 1000ULL)
            return (int)0x80240007;                          /* timeout */
        usleep(500);
    }
}
