/*
 * Read-only USB enumeration backend based on libusb.
 *
 * This is the only module that talks to libusb. It lists the devices on
 * all buses and copies their cached device descriptor data into plain
 * ff802_usb_dev records. It never opens a device, never claims an
 * interface, never detaches kernel drivers, never resets a device and
 * never performs any USB transfer.
 */
#ifndef FF802_USB_ENUM_H
#define FF802_USB_ENUM_H

#include <stddef.h>

#include "ff802/detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enumerate all USB devices. On success returns 0 and stores a newly
 * allocated array in *devs (release with ff802_usb_free()) and its length
 * in *count. On failure returns a negative libusb error code and leaves
 * the outputs set to NULL / 0.
 */
int ff802_usb_enumerate(ff802_usb_dev **devs, size_t *count);

/* Release an array returned by ff802_usb_enumerate(). NULL is allowed. */
void ff802_usb_free(ff802_usb_dev *devs);

/* Human-readable text for an error returned by ff802_usb_enumerate(). */
const char *ff802_usb_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* FF802_USB_ENUM_H */
