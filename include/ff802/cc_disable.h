/*
 * Sequence of ff802-cc disable: class-compliant -> proprietary mode.
 *
 * Reproduces the transfer of the hardware test 2026-11-19 on the abstract
 * USB backend:
 *
 *   list -> exactly one Fireface 802 in CC mode (0424:3fdd) -> layout of
 *   interface 3 (bulk OUT 0x07, 512 bytes) -> open -> kernel driver state
 *   (snd-usb-audio expected, only logged) -> write gate -> confirmation
 *   (gets the exact bytes) -> claim with automatic kernel driver detach
 *   -> ONE bulk transfer of the 16 bytes, timeout 3000 ms, no retry ->
 *   release (libusb re-attaches the driver; failing is expected when the
 *   unit has already switched) -> close
 *
 * Separate from cc_switch.c: the enable sequence is not changed. The
 * results and exit codes are those of cc_switch.h.
 */
#ifndef FF802_CC_DISABLE_H
#define FF802_CC_DISABLE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ff802/cc_mode.h"
#include "ff802/cc_switch.h"
#include "ff802/usb_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_DIS_INTERFACE  3u
#define FF802_DIS_ALTSETTING 0u
#define FF802_DIS_EP_OUT     0x07u
#define FF802_DIS_MAX_PACKET 512u
#define FF802_DIS_TIMEOUT_MS 3000u

/* Word the user has to type to confirm the switch back. */
#define FF802_DISABLE_CONFIRM_WORD "DISABLE"

/* Called once right before the claim, with the exact bytes to be sent. */
typedef int (*ff802_confirm_data_fn)(void *user, const ff802_usb_dev *target,
                                     const uint8_t *data, size_t len);

typedef struct ff802_dis_options {
    int     select_location;   /* non-zero: only accept bus:address below */
    uint8_t bus;
    uint8_t address;
    ff802_confirm_data_fn confirm;   /* required */
    void   *confirm_user;
} ff802_dis_options;

ff802_sw_status ff802_disable_run(const ff802_usb_backend *be,
                                  const ff802_dis_options *opt,
                                  FILE *log, ff802_sw_result *res);

/* Interface 3 / altsetting 0 has bulk OUT 0x07 with 512 bytes. */
ff802_layout_status ff802_check_layout_disable(const ff802_dev_layout *layout);

const char *ff802_disable_layout_str(ff802_layout_status st);

/* Messages of the disable sequence (interface 3, endpoint 0x07). */
const char *ff802_disable_status_str(ff802_sw_status st);

/* Exactly FF802_DISABLE_CONFIRM_WORD, optionally with "\n" / "\r\n". */
int ff802_disable_confirm_answer_ok(const char *line);

#ifdef __cplusplus
}
#endif

#endif /* FF802_CC_DISABLE_H */
