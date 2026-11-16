/*
 * Internal helpers shared by the libusb based modules. Not public API.
 */
#ifndef FF802_USB_INTERNAL_H
#define FF802_USB_INTERNAL_H

#include <libusb.h>

#include "ff802/detect.h"

/*
 * Copy bus, address, port path and cached device descriptor fields of a
 * libusb device into a record. Performs no I/O. Implemented in usb_enum.c.
 */
void ff802_usb_fill_record(libusb_device *dev,
                           const struct libusb_device_descriptor *desc,
                           ff802_usb_dev *rec);

#endif /* FF802_USB_INTERNAL_H */
