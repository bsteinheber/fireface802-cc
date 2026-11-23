/*
 * USB hub port of a device: find it, cycle it (disable=1, wait,
 * disable=0), wait for the device to come back in a given mode.
 *
 * Used by ff802-cc enable and ff802-cc-autoswitch: the CC enable command
 * takes effect only when the USB connection is interrupted (hardware
 * test 2026-11-19). The port is found from the device's kernel name
 * (e.g. 3-2.4.1.1.3 -> hub
 * 3-2.4.1.1, port 3 -> <hub>/<hub>:1.0/<hub>-port3) and accepted only if
 * its "device" link points to exactly that device. Only the sysfs
 * attribute "disable" of that one port is written; no usbreset, no
 * "authorized", no libusb reset.
 *
 * The sysfs root is a parameter ("/sys" in the program, a private tree in
 * the tests). Writing needs root.
 */
#ifndef FF802_USB_PORT_H
#define FF802_USB_PORT_H

#include "ff802/auto_sysfs.h"
#include "ff802/device_id.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_PORT_PATH_MAX 320u

/*
 * The one port reset sequence after the CC enable command (hardware test
 * 2026-11-19), used by ff802-cc enable and ff802-cc-autoswitch alike:
 * disable=1, FF802_PORT_OFF_MS, disable=0, then wait up to
 * FF802_PORT_WAIT_MS for the class-compliant USB ID at the same port.
 */
#define FF802_PORT_OFF_MS   2000u
#define FF802_PORT_WAIT_MS 15000u

typedef struct ff802_usb_port {
    char     dir[FF802_PORT_PATH_MAX];      /* .../<hub>-port<N> */
    char     hub[FF802_SYSFS_NAME_MAX];     /* "3-2.4.1.1" or "usb3" */
    unsigned number;                        /* N */
} ff802_usb_port;

typedef enum ff802_port_status {
    FF802_PORT_OK = 0,
    FF802_PORT_BAD_NAME,      /* not a valid USB device kernel name */
    FF802_PORT_NO_DEVICE,     /* the device is not in sysfs */
    FF802_PORT_NOT_FOUND,     /* no port directory for it */
    FF802_PORT_MISMATCH,      /* the port's "device" link is another device */
    FF802_PORT_NO_DISABLE     /* the port has no "disable" attribute */
} ff802_port_status;

const char *ff802_port_status_str(ff802_port_status st);

/* Find the hub port the device <kernel_name> is connected to. */
ff802_port_status ff802_port_resolve(const char *sysfs_root,
                                     const char *kernel_name,
                                     ff802_usb_port *out);

/* Still the port of <kernel_name> (its "device" link resolves to it)? */
int ff802_port_belongs_to(const ff802_usb_port *port, const char *sysfs_root,
                          const char *kernel_name);

/* The port's "disable" attribute can be written by this process (root). */
int ff802_port_writable(const ff802_usb_port *port);

/* Waiting hook, so the tests neither sleep nor depend on timing. */
typedef struct ff802_port_ops {
    void (*sleep_ms)(void *user, unsigned ms);   /* NULL: nanosleep */
    void *user;
} ff802_port_ops;

typedef struct ff802_port_cycle_result {
    int disabled;          /* "1" written */
    int enabled;           /* "0" written afterwards */
    int err_disable;       /* errno of the failed "1" write, else 0 */
    int err_enable;        /* errno of the failed "0" write, else 0 */
} ff802_port_cycle_result;

/*
 * Write "1" to <port>/disable, wait off_ms, write "0". "0" is attempted
 * in every case once the "1" write was tried, also if it failed.
 * SIGINT, SIGTERM, SIGHUP and SIGQUIT are blocked for the whole cycle and
 * delivered afterwards, so an interrupt cannot leave the port disabled.
 * Returns 0 if both writes succeeded.
 */
int ff802_port_cycle(const ff802_usb_port *port, unsigned off_ms,
                     const ff802_port_ops *ops, ff802_port_cycle_result *r);

typedef enum ff802_wait_status {
    FF802_WAIT_OK = 0,      /* device with the wanted USB ID is back */
    FF802_WAIT_TIMEOUT,     /* nothing (readable) at that port in time */
    FF802_WAIT_OTHER        /* another USB ID at that port at the end */
} ff802_wait_status;

/*
 * Poll <sysfs_root>/bus/usb/devices/<kernel_name> every 100 ms for up to
 * timeout_ms until its USB ID classifies as want. *found holds the last
 * successful read (zeroed if none).
 */
ff802_wait_status ff802_port_wait_mode(const char *sysfs_root,
                                       const char *kernel_name,
                                       ff802_mode want, unsigned timeout_ms,
                                       const ff802_port_ops *ops,
                                       ff802_sysfs_dev *found);

/*
 * Devices in sysfs whose USB ID classifies as mode (and, with
 * select_location, that are at bus/devnum). Returns their number; *first
 * is the first one found (zeroed if none).
 */
size_t ff802_port_find_devices(const char *sysfs_root, ff802_mode mode,
                               int select_location, uint8_t bus, uint8_t devnum,
                               ff802_sysfs_dev *first);

/* After the CC enable command: port reset and check of the new mode. */
typedef enum ff802_psw_status {
    FF802_PSW_OK = 0,           /* port reset, device back with the wanted ID */
    FF802_PSW_CHANGED,          /* not the connection that was written: nothing done */
    FF802_PSW_PORT,             /* port not found / not this device's: nothing done */
    FF802_PSW_NOT_RESET,        /* "1" could not be written (port was re-enabled) */
    FF802_PSW_NOT_REENABLED,    /* "0" could not be written: port may stay disabled */
    FF802_PSW_NOT_BACK,         /* device did not come back in time */
    FF802_PSW_OTHER_MODE        /* device came back with another USB ID */
} ff802_psw_status;

typedef struct ff802_psw_result {
    ff802_psw_status        status;
    ff802_port_status       port_status;
    ff802_usb_port          port;
    ff802_port_cycle_result cycle;
    ff802_sysfs_dev         found;   /* device after the reset, if any */
} ff802_psw_result;

/*
 * written: the sysfs entry of the device the command was sent to. Re-reads
 * it (same kernel name, bus, devnum, USB ID), resolves its port, cycles it
 * (off_ms) and waits up to wait_ms for the USB ID of mode want at the same
 * kernel name.
 */
ff802_psw_status ff802_port_switch(const char *sysfs_root,
                                   const ff802_sysfs_dev *written,
                                   unsigned off_ms, unsigned wait_ms,
                                   ff802_mode want, const ff802_port_ops *ops,
                                   ff802_psw_result *r);

const char *ff802_psw_status_str(ff802_psw_status st);

#ifdef __cplusplus
}
#endif

#endif /* FF802_USB_PORT_H */
