/*
 * Orchestration of the class-compliant mode switch.
 *
 * Runs the full sequence against an abstract USB backend:
 *
 *   list -> select -> layout check -> open -> kernel driver check
 *        -> [check only: stop here]
 *        -> write gate -> user confirmation -> claim -> one bulk write
 *        -> release -> close
 *
 * Every step that can fail aborts the sequence; resources acquired so far
 * are released. A failed write is never retried. This module has no
 * libusb dependency and is fully testable with a simulated backend.
 */
#ifndef FF802_CC_SWITCH_H
#define FF802_CC_SWITCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ff802/reg_codec.h"
#include "ff802/usb_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ff802_sw_action {
    FF802_SW_CHECK,       /* diagnose only: no claim, no write */
    FF802_SW_ENABLE_CC    /* send the "enable CC mode" command */
} ff802_sw_action;

typedef enum ff802_sw_status {
    FF802_SW_OK = 0,              /* check passed / command transferred */
    FF802_SW_LIST_FAILED,         /* USB enumeration failed */
    FF802_SW_NOT_FOUND,           /* no matching device in proprietary mode */
    FF802_SW_AMBIGUOUS,           /* several devices, none selected */
    FF802_SW_LAYOUT_FAILED,       /* descriptor could not be read */
    FF802_SW_UNEXPECTED_LAYOUT,   /* descriptor differs from expectation */
    FF802_SW_ACCESS_DENIED,       /* no permission to open/claim */
    FF802_SW_DISCONNECTED,        /* device vanished during the sequence */
    FF802_SW_OPEN_FAILED,         /* open failed for another reason */
    FF802_SW_KERNEL_DRIVER,       /* a kernel driver is bound to interface 1 */
    FF802_SW_DRIVER_CHECK_FAILED, /* kernel driver state unknown */
    FF802_SW_WRITE_DISABLED,      /* build does not allow USB writes */
    FF802_SW_ABORTED,             /* user did not confirm */
    FF802_SW_INTERFACE_BUSY,      /* interface claimed by another program */
    FF802_SW_CLAIM_FAILED,        /* claim failed for another reason */
    FF802_SW_TIMEOUT,             /* bulk transfer timed out */
    FF802_SW_SHORT_WRITE,         /* fewer bytes transferred than requested */
    FF802_SW_STALL,               /* endpoint stalled */
    FF802_SW_WRITE_FAILED,        /* other transfer error */
    FF802_SW_INTERNAL             /* invalid arguments or internal error */
} ff802_sw_status;

/* Steps of the sequence, used to report where a failure happened. */
typedef enum ff802_sw_step {
    FF802_STEP_NONE = 0,
    FF802_STEP_LIST,
    FF802_STEP_SELECT,
    FF802_STEP_LAYOUT,
    FF802_STEP_OPEN,
    FF802_STEP_DRIVER_CHECK,
    FF802_STEP_WRITE_GATE,
    FF802_STEP_CONFIRM,
    FF802_STEP_CLAIM,
    FF802_STEP_WRITE,
    FF802_STEP_RELEASE
} ff802_sw_step;

/*
 * Confirmation callback, called once right before the interface is
 * claimed. Must return non-zero only after an explicit user action.
 */
typedef int (*ff802_confirm_fn)(void *user, const ff802_usb_dev *target,
                                const uint8_t cmd[FF802_REG_WORD_SIZE]);

typedef struct ff802_sw_options {
    ff802_sw_action action;
    int     select_location;   /* non-zero: only accept bus:address below */
    uint8_t bus;
    uint8_t address;
    ff802_confirm_fn confirm;  /* required for FF802_SW_ENABLE_CC */
    void   *confirm_user;
} ff802_sw_options;

typedef struct ff802_sw_result {
    ff802_sw_status     status;
    ff802_sw_step       failed_step;   /* FF802_STEP_NONE on success */
    ff802_usb_err       usb_err;       /* backend error of the failed step */
    ff802_layout_status layout;        /* result of the layout check */
    int                 have_target;
    ff802_usb_dev       target;        /* selected device, if have_target */
    int                 write_attempted;
    size_t              transferred;   /* bytes reported by the transfer */
    int                 release_failed;/* release error after the write */
    ff802_usb_err       release_err;
} ff802_sw_result;

/*
 * Run the sequence. Progress messages are written to log (may be NULL).
 * Returns res->status.
 */
ff802_sw_status ff802_switch_run(const ff802_usb_backend *be,
                                 const ff802_sw_options *opt,
                                 FILE *log, ff802_sw_result *res);

const char *ff802_sw_status_str(ff802_sw_status st);

/* Additional advice for the user, or NULL. */
const char *ff802_sw_status_hint(ff802_sw_status st);

/* Process exit codes of ff802-cc. */
enum {
    FF802_CC_EXIT_OK = 0,            /* check passed / command transferred */
    FF802_CC_EXIT_NOT_FOUND = 1,     /* no suitable device */
    FF802_CC_EXIT_USAGE = 2,         /* invalid command line */
    FF802_CC_EXIT_CHECK_FAILED = 3,  /* precondition failed, nothing sent */
    FF802_CC_EXIT_WRITE_FAILED = 4,  /* transfer failed, device state unknown */
    FF802_CC_EXIT_NOT_SENT = 5,      /* aborted by user or writes disabled */
    FF802_CC_EXIT_SWITCH_UNCONFIRMED = 6 /* transferred, new mode not detected */
};

int ff802_sw_exit_code(ff802_sw_status st);

/* Word the user has to type to confirm the switch. */
#define FF802_CONFIRM_WORD "ENABLE"

/*
 * True if a line read from the terminal is exactly FF802_CONFIRM_WORD,
 * optionally followed by "\n" or "\r\n".
 */
int ff802_confirm_answer_ok(const char *line);

/*
 * Parse "BUS:ADDRESS" (decimal, as printed by lsusb/ff802-detect, leading
 * zeros allowed). Bus 1..255, address 1..127. Returns 0 on success.
 */
int ff802_parse_location(const char *s, uint8_t *bus, uint8_t *address);

#ifdef __cplusplus
}
#endif

#endif /* FF802_CC_SWITCH_H */
