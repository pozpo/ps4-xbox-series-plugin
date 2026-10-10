#ifndef FAKE_PLATFORM_H
#define FAKE_PLATFORM_H

#include <stdint.h>
#include <orbis/libkernel.h>
#include <orbis/Usbd.h>

#define HOST_MAX_NOTES 256
extern char host_notes[HOST_MAX_NOTES][128];
extern int host_note_count;
int host_count_notes(const char* needle);
void host_reset_notes(void);

/* Side-effect counters (used by test_exclusion.c). All are reset together. */
extern int host_patch_calls, host_detour_calls, host_usbd_init_calls,
           host_dynlib_calls, host_thread_creates, host_mutex_inits,
           host_log_opens, host_sdk_proc_info_calls;
void host_reset_counters(void);
int  host_counters_total(void);                 /* sum of every counter above except host_sdk_proc_info_calls */
void host_set_title(const char* titleid);       /* NULL = title ID unavailable */

libusb_device* fake_usb_plug(uint16_t vid, uint16_t pid);
void fake_usb_unplug(libusb_device* d);
void fake_usb_push(libusb_device* d, const uint8_t* data, int len);
int fake_usb_out_count(libusb_device* d);
const uint8_t* fake_usb_out_packet(libusb_device* d, int i, int* len);
int fake_usb_open_count(libusb_device* d);
void fake_usb_set_fail_claim(libusb_device* d, int v);
void fake_usb_reset(void);

#endif
