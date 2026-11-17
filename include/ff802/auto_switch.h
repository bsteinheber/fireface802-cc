/*
 * T4: automatic switch of one Fireface 802 connection to class-compliant
 * mode, as started by ff802-cc-auto@.service for one udev "add" event.
 *
 *   lock -> sysfs snapshot of the event's connection (still the same
 *   bus/devnum?) -> identity -> CC mode: confirmation event if a switch
 *   was pending, nothing else -> automation on? -> record the connection
 *   (at most one attempt) -> create the USB backend -> the switch
 *   sequence of cc_switch (descriptor, open, kernel driver, write gate)
 *   -> right before the claim: re-read sysfs, same connection, still
 *   proprietary, same device as the opened one, automation still on ->
 *   one bulk write of 01 00 44 BD -> pending marker and event.
 *
 * There is no path that sends anything else, retries, resets the device
 * or switches back to proprietary mode. The USB backend is created only
 * when a write is actually going to be attempted, so the "automation
 * off", "already handled" and "CC mode" cases never initialise libusb.
 */
#ifndef FF802_AUTO_SWITCH_H
#define FF802_AUTO_SWITCH_H

#include <stdio.h>

#include "ff802/auto_state.h"
#include "ff802/auto_sysfs.h"
#include "ff802/cc_switch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The connection named by the systemd instance. */
typedef struct ff802_auto_event {
    char    kernel_name[FF802_SYSFS_NAME_MAX];
    uint8_t bus;
    uint8_t devnum;
} ff802_auto_event;

/*
 * Parse "<kernel name>:<busnum>:<devnum>" (numbers decimal, udev pads
 * them with zeros: "3-2.4:003:019"). Returns 0 on success.
 */
int ff802_auto_parse_instance(const char *s, ff802_auto_event *ev);

/* Creates the USB backend only when it is needed. */
typedef struct ff802_auto_backend_factory {
    void *ctx;
    ff802_usb_err (*open)(void *ctx, ff802_usb_backend *be);
    void (*close)(void *ctx, ff802_usb_backend *be);
} ff802_auto_backend_factory;

typedef enum ff802_auto_outcome {
    FF802_AUTO_OFF = 0,          /* automation off: nothing sent */
    FF802_AUTO_ALREADY_HANDLED,  /* this connection was handled before */
    FF802_AUTO_CC_ACTIVE,        /* device in CC mode: nothing sent */
    FF802_AUTO_SENT,             /* enable command transferred */
    FF802_AUTO_NOT_SENT,         /* failed before the write: nothing sent */
    FF802_AUTO_WRITE_FAILED,     /* write attempted and failed: state unknown */
    FF802_AUTO_REFUSED,          /* connection gone/changed or not identified */
    FF802_AUTO_ERROR             /* local error (state files): nothing sent */
} ff802_auto_outcome;

typedef struct ff802_auto_result {
    ff802_auto_outcome outcome;
    int             have_identity;
    ff802_identity  identity;
    int             backend_created;
    int             switch_ran;
    ff802_sw_result sw;
    long            event_seq;      /* 0 if no event was written */
    char            detail[192];
} ff802_auto_result;

ff802_auto_outcome ff802_auto_handle(const ff802_auto_paths *p,
                                     const ff802_auto_event *ev,
                                     const ff802_auto_backend_factory *f,
                                     FILE *log, ff802_auto_result *res);

/* Stable word for logs and the last-result file, e.g. "sent". */
const char *ff802_auto_outcome_str(ff802_auto_outcome o);

/* Exit codes of ff802-cc-autoswitch --service. */
enum {
    FF802_AUTO_EXIT_OK = 0,           /* off, handled, CC mode, sent */
    FF802_AUTO_EXIT_USAGE = 2,        /* bad arguments, not started by systemd */
    FF802_AUTO_EXIT_NOT_SENT = 3,     /* refused or failed before the write */
    FF802_AUTO_EXIT_WRITE_FAILED = 4  /* write failed: device state unknown */
};

int ff802_auto_exit_code(ff802_auto_outcome o);

#ifdef __cplusplus
}
#endif

#endif /* FF802_AUTO_SWITCH_H */
