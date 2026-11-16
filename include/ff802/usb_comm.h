/*
 * libusb implementation of the USB backend (usb_backend.h).
 *
 * This is the only module that is allowed to open devices, claim
 * interfaces and perform transfers. It never detaches kernel drivers,
 * never resets devices and never changes the USB configuration or
 * alternate setting.
 *
 * Claiming and writing are compiled in only if the project is configured
 * with -DFF802_ENABLE_USB_WRITE=ON. Otherwise the corresponding backend
 * functions return FF802_USB_ERR_WRITE_DISABLED and the libusb claim and
 * transfer functions are not even referenced by the binary.
 */
#ifndef FF802_USB_COMM_H
#define FF802_USB_COMM_H

#include "ff802/usb_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-zero if this build is able to claim interfaces and write. */
int ff802_usb_comm_write_enabled(void);

/* Initialise a libusb context and fill *be. */
ff802_usb_err ff802_usb_comm_init(ff802_usb_backend *be);

/* Release everything acquired by ff802_usb_comm_init(). */
void ff802_usb_comm_exit(ff802_usb_backend *be);

#ifdef __cplusplus
}
#endif

#endif /* FF802_USB_COMM_H */
