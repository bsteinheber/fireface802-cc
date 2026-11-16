/*
 * Fireface 802 detection and reporting on plain USB device records.
 *
 * The records are filled by a backend (see usb_enum.h for the libusb
 * implementation) or by tests with fake data. This module itself performs
 * no USB access, which keeps detection testable without hardware.
 */
#ifndef FF802_DETECT_H
#define FF802_DETECT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ff802/device_id.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USB allows at most 7 tiers of ports below the root hub. */
#define FF802_MAX_PORT_DEPTH 7u

/* Information about one USB device, gathered without opening it. */
typedef struct ff802_usb_dev {
    uint8_t  bus;                           /* bus number */
    uint8_t  address;                       /* device address on the bus */
    uint16_t vid;                           /* idVendor */
    uint16_t pid;                           /* idProduct */
    uint16_t bcd_device;                    /* bcdDevice (BCD release number) */
    uint8_t  port_count;                    /* valid entries in ports[] */
    uint8_t  ports[FF802_MAX_PORT_DEPTH];   /* port path from the root hub */
} ff802_usb_dev;

/* A device recognised as RME hardware, with its classified mode. */
typedef struct ff802_match {
    const ff802_usb_dev *dev;
    ff802_mode mode;
} ff802_match;

/* Process exit codes of the diagnostic tool. */
enum {
    FF802_EXIT_FOUND = 0,       /* at least one Fireface 802 found */
    FF802_EXIT_NOT_FOUND = 1,   /* enumeration worked, no Fireface 802 */
    FF802_EXIT_ERROR = 2        /* usage or USB enumeration error */
};

/*
 * Find all devices whose classification is not FF802_MODE_NONE (i.e.
 * Fireface 802 in either mode, or other RME devices), in input order.
 * Up to max_out matches are written to out (which may be NULL if max_out
 * is 0). Returns the total number of matches, which may exceed max_out.
 */
size_t ff802_find_devices(const ff802_usb_dev *devs, size_t n,
                          ff802_match *out, size_t max_out);

/*
 * Format the sysfs-style port path (e.g. "2-3.1") into buf.
 * Returns the length that would have been written (snprintf semantics).
 */
int ff802_format_port_path(const ff802_usb_dev *dev, char *buf, size_t size);

/*
 * Format a one-line summary of a device, e.g.
 *   "Bus 002 Device 005: ID 2a39:3fcd mode=proprietary port=2-3 bcdDevice=1.06"
 * Returns the length that would have been written (snprintf semantics).
 */
int ff802_format_device(const ff802_usb_dev *dev, ff802_mode mode,
                        char *buf, size_t size);

/*
 * Write a full diagnostic report for the given device list to out and
 * return the matching FF802_EXIT_* code.
 */
int ff802_report(const ff802_usb_dev *devs, size_t n, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* FF802_DETECT_H */
