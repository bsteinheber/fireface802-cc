/*
 * T4: automatic switch of one Fireface 802 connection to class-compliant
 * mode, as started for one udev "add" event by ff802-cc-auto-enable@<k>
 * (proprietary mode; the connection at that port when it runs) or
 * ff802-cc-auto@<k>:<bus>:<devnum> (class-compliant mode).
 *
 *   lock -> sysfs snapshot of the event's connection (still the same
 *   bus/devnum?) -> identity -> CC mode: confirmation event if a switch
 *   was pending, nothing else -> automation on? -> record the connection
 *   (at most one attempt) -> can its hub port be reset (usb_port.h:
 *   resolved, "device" link, writable; not blocked on this boot)? ->
 *   create the USB backend -> the switch sequence of cc_switch
 *   (descriptor, open, kernel driver, write gate) -> right before the
 *   claim: re-read sysfs, same connection, still proprietary, same device
 *   as the opened one, automation still on -> one bulk write of
 *   01 00 44 BD -> pending marker -> if the port can be reset: the port
 *   reset of ff802-cc enable (ff802_port_switch, FF802_PORT_OFF_MS,
 *   FF802_PORT_WAIT_MS) and its result, otherwise the "prepared" event
 *   (reconnect by hand).
 *
 * The lock is held during the port reset; the udev event of the
 * re-enumerated device starts its own instance, which waits for the lock
 * and then confirms the mode and runs the mixer initialisation. Nothing
 * here waits for that instance. A port reset that does not end with the
 * device back in class-compliant mode blocks further automatic resets on
 * this boot (auto_state.h, reset-blocked).
 *
 * There is no path that sends anything else, retries, resets the device
 * in any other way or switches back to proprietary mode. The USB
 * backend is created only
 * when a write is actually going to be attempted, so the "automation
 * off", "already handled" and "CC mode" cases never initialise libusb.
 *
 * CC mode, in addition and independent of the automation mode: if
 * "ff802-cc-auto mixer-init on", the mixer initialisation of
 * mixer_run.h for this connection, on every "add" event (no USB access,
 * only the ALSA sequencer). A failure is recorded in mixer-result and
 * mixer-event; it never changes the outcome "cc-active" of the switch.
 */
#ifndef FF802_AUTO_SWITCH_H
#define FF802_AUTO_SWITCH_H

#include <stdio.h>

#include "ff802/auto_state.h"
#include "ff802/auto_sysfs.h"
#include "ff802/cc_switch.h"
#include "ff802/midi_backend.h"
#include "ff802/mixer_run.h"
#include "ff802/usb_port.h"

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

/*
 * Parse the instance of ff802-cc-auto-enable@.service: the kernel name
 * alone ("3-2.4.1.1.3"). bus and devnum are 0: the connection is the one
 * at that port when the service runs. Returns 0 on success.
 */
int ff802_auto_parse_port_instance(const char *s, ff802_auto_event *ev);

/*
 * Creates the USB backend only when it is needed. midi is the backend of
 * the mixer initialisation (NULL: not available in this program);
 * mixer_opt NULL: the defaults of mixer_run.h.
 */
typedef struct ff802_auto_backend_factory {
    void *ctx;
    ff802_usb_err (*open)(void *ctx, ff802_usb_backend *be);
    void (*close)(void *ctx, ff802_usb_backend *be);
    const ff802_midi_factory  *midi;
    const ff802_mixer_options *mixer_opt;
    const ff802_port_ops      *port_ops;   /* NULL: real sleeps */
} ff802_auto_backend_factory;

typedef enum ff802_auto_outcome {
    FF802_AUTO_OFF = 0,          /* automation off: nothing sent */
    FF802_AUTO_ALREADY_HANDLED,  /* this connection was handled before */
    FF802_AUTO_CC_ACTIVE,        /* device in CC mode: nothing sent */
    FF802_AUTO_SENT,             /* enable sent, no automatic reset: reconnect */
    FF802_AUTO_NOT_SENT,         /* failed before the write: nothing sent */
    FF802_AUTO_WRITE_FAILED,     /* write attempted and failed: state unknown */
    FF802_AUTO_REFUSED,          /* connection gone/changed or not identified */
    FF802_AUTO_ERROR,            /* local error (state files): nothing sent */
    FF802_AUTO_SWITCHED,         /* enable sent, port reset, back as 0424:3fdd */
    FF802_AUTO_RESET_FAILED,     /* enable sent, port reset not done/incomplete */
    FF802_AUTO_NOT_REENUMERATED, /* enable sent, port reset, device not back */
    FF802_AUTO_REENUMERATED_NOT_CC /* enable sent, port reset, back, not CC */
} ff802_auto_outcome;

typedef struct ff802_auto_result {
    ff802_auto_outcome outcome;
    int             have_identity;
    ff802_identity  identity;
    int             backend_created;
    int             switch_ran;
    ff802_sw_result sw;
    long            event_seq;      /* 0 if no event was written */
    char            detail[256];
    int             mixer_ran;      /* mixer-init on, initialisation attempted */
    ff802_mixer_result mixer;       /* outcome FF802_MIXER_OFF if not run */
    long            mixer_event_seq;
    int             reset_planned;  /* port resolved and writable before the write */
    int             reset_ran;      /* ff802_port_switch was called */
    ff802_psw_result reset;
} ff802_auto_result;

ff802_auto_outcome ff802_auto_handle(const ff802_auto_paths *p,
                                     const ff802_auto_event *ev,
                                     const ff802_auto_backend_factory *f,
                                     FILE *log, ff802_auto_result *res);

/* Stable word for logs and the last-result file, e.g. "sent". */
const char *ff802_auto_outcome_str(ff802_auto_outcome o);

/* Exit codes of ff802-cc-autoswitch --service. */
enum {
    FF802_AUTO_EXIT_OK = 0,           /* off, handled, CC mode, sent, switched */
    FF802_AUTO_EXIT_USAGE = 2,        /* bad arguments, not started by systemd */
    FF802_AUTO_EXIT_NOT_SENT = 3,     /* refused or failed before the write */
    FF802_AUTO_EXIT_WRITE_FAILED = 4, /* write failed: device state unknown */
    FF802_AUTO_EXIT_UNCONFIRMED = 5   /* sent, port reset did not end in CC mode */
};

int ff802_auto_exit_code(ff802_auto_outcome o);

/*
 * Exit code of a complete run: that of the outcome, except in CC mode
 * with a failed mixer initialisation: 4 if it stopped while sending
 * (incomplete), 3 if nothing was sent.
 */
int ff802_auto_result_exit_code(const ff802_auto_result *r);

#ifdef __cplusplus
}
#endif

#endif /* FF802_AUTO_SWITCH_H */
