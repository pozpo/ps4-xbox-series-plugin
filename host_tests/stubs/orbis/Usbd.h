#ifndef STUB_USBD_H
#define STUB_USBD_H
#include <stdint.h>
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;
struct libusb_device_descriptor { uint16_t idVendor; uint16_t idProduct; };
int sceUsbdInit(void);
void sceUsbdExit(void);
int32_t sceUsbdGetDeviceList(libusb_device*** list);
void sceUsbdFreeDeviceList(libusb_device** list);
int sceUsbdGetDeviceDescriptor(libusb_device* dev, struct libusb_device_descriptor* d);
int sceUsbdOpen(libusb_device* dev, libusb_device_handle** h);
void sceUsbdClose(libusb_device_handle* h);
int sceUsbdDetachKernelDriver(libusb_device_handle* h, int iface);
int sceUsbdClaimInterface(libusb_device_handle* h, int iface);
int sceUsbdReleaseInterface(libusb_device_handle* h, int iface);
int sceUsbdSetInterfaceAltSetting(libusb_device_handle* h, int iface, int alt);
int sceUsbdInterruptTransfer(libusb_device_handle* h, uint8_t ep, uint8_t* data, int len, int32_t* transferred, unsigned timeout_ms);
#endif
