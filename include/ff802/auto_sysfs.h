/*
 * T4: identification of a Fireface 802 from the kernel's sysfs view.
 *
 * The automatic mode switch is started for one USB connection, named by
 * the kernel device name (e.g. "3-2.4.1.1.3") and the bus and device
 * numbers of the udev event. Everything here is read from sysfs
 * attributes that the kernel cached at enumeration: no device is opened
 * and no USB traffic is caused.
 *
 * The sysfs root is a parameter, so the tests use a fake tree.
 */
#ifndef FF802_AUTO_SYSFS_H
#define FF802_AUTO_SYSFS_H

#include <stddef.h>
#include <stdint.h>

#include "ff802/detect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_SYSFS_NAME_MAX 32u
#define FF802_SYSFS_STR_MAX  64u

/* Attributes of one USB device directory below <root>/bus/usb/devices. */
typedef struct ff802_sysfs_dev {
    char     kernel_name[FF802_SYSFS_NAME_MAX];  /* "<bus>-<port>[.<port>]..." */
    uint16_t vid;
    uint16_t pid;
    uint8_t  bus;
    uint8_t  devnum;
    char     speed[16];                          /* "480" = high speed */
    char     manufacturer[FF802_SYSFS_STR_MAX];  /* "" if absent */
    char     product[FF802_SYSFS_STR_MAX];       /* "" if absent */
} ff802_sysfs_dev;

typedef enum ff802_sysfs_status {
    FF802_SYSFS_OK = 0,
    FF802_SYSFS_BAD_NAME,     /* not a valid USB device kernel name */
    FF802_SYSFS_ABSENT,       /* no such device (unplugged) */
    FF802_SYSFS_UNREADABLE    /* attribute missing or malformed */
} ff802_sysfs_status;

/* Result of the identity check of one connection. */
typedef enum ff802_identity {
    FF802_ID_PROPRIETARY = 0,   /* original Fireface 802, proprietary mode */
    FF802_ID_CLASS_COMPLIANT,   /* original Fireface 802, CC mode */
    FF802_ID_UNKNOWN_DEVICE,    /* neither of the two USB IDs */
    FF802_ID_BAD_MANUFACTURER,  /* manufacturer string is not "RME" */
    FF802_ID_BAD_PRODUCT,       /* product string is not a Fireface 802 */
    FF802_ID_NOT_HIGH_SPEED     /* not connected at 480 Mbit/s */
} ff802_identity;

/*
 * True for "<bus>-<port>" with up to FF802_MAX_PORT_DEPTH dot-separated
 * ports, all decimal without leading zeros (bus 1..255, port 1..255).
 * Rejects everything else, so the name can be used in a path safely.
 */
int ff802_sysfs_valid_name(const char *name);

/* Read <root>/bus/usb/devices/<name>/... into *out. */
ff802_sysfs_status ff802_sysfs_read(const char *root, const char *name,
                                    ff802_sysfs_dev *out);

/*
 * "Fireface 802" or "Fireface 802 (<1 to 16 digits>)". Rejects the
 * Fireface 802 FS and any other product.
 */
int ff802_sysfs_product_ok(const char *product);

/*
 * Classify a connection. Both modes require the exact USB ID, the
 * manufacturer "RME", a Fireface 802 product string and high speed.
 */
ff802_identity ff802_sysfs_identify(const ff802_sysfs_dev *d);

const char *ff802_identity_str(ff802_identity id);

/* Same connection: name, bus, devnum, IDs and strings are equal. */
int ff802_sysfs_same_connection(const ff802_sysfs_dev *a,
                                const ff802_sysfs_dev *b);

/*
 * The libusb record describes the same device as the sysfs entry: bus,
 * address, IDs and port path (formatted as the kernel name) are equal.
 */
int ff802_sysfs_matches_usb_dev(const ff802_sysfs_dev *s,
                                const ff802_usb_dev *u);

#ifdef __cplusplus
}
#endif

#endif /* FF802_AUTO_SYSFS_H */
