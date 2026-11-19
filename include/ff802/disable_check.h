/*
 * Preconditions of ff802-cc disable, read from sysfs, /proc/asound and the
 * T4 state (read only, before any USB access):
 *
 *  - exactly one original Fireface 802 in CC mode (0424:3fdd, "RME",
 *    "Fireface 802 (...)", high speed), or the one at --device BUS:ADDRESS
 *  - interface 3 of the active configuration: USB-MIDI streaming
 *    (class 01, subclass 03) with bulk OUT endpoint 0x07, 512 bytes
 *  - kernel driver of interface 3: exactly snd-usb-audio
 *  - the T4 automation is not on (it would switch the unit straight back)
 *  - no PCM substream of the unit's ALSA card is prepared or running
 *
 * Roots are parameters (/sys, /proc/asound, /var/lib/ff802-cc-auto in the
 * program; private trees in the tests).
 */
#ifndef FF802_DISABLE_CHECK_H
#define FF802_DISABLE_CHECK_H

#include <stddef.h>
#include <stdint.h>

#include "ff802/auto_state.h"
#include "ff802/auto_sysfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ff802_dchk_status {
    FF802_DCHK_OK = 0,
    FF802_DCHK_NOT_FOUND,       /* no Fireface 802 in CC mode */
    FF802_DCHK_AMBIGUOUS,       /* more than one, none selected */
    FF802_DCHK_IDENTITY,        /* 0424:3fdd but not an original 802 */
    FF802_DCHK_INTERFACE,       /* interface 3 / endpoint 0x07 not as expected */
    FF802_DCHK_DRIVER,          /* interface 3 not bound to snd-usb-audio */
    FF802_DCHK_AUTOMATION_ON,   /* ff802-cc-auto is on */
    FF802_DCHK_AUDIO_ACTIVE,    /* a PCM of the card is prepared/running */
    FF802_DCHK_ERROR            /* sysfs not readable */
} ff802_dchk_status;

typedef struct ff802_dchk_target {
    ff802_sysfs_dev dev;       /* the connection to switch */
    int             card;      /* ALSA card index, -1 if none was found */
} ff802_dchk_target;

typedef struct ff802_dchk_roots {
    ff802_auto_paths paths;    /* sysfs_root, state_dir (T4 mode) */
    char proc_asound[FF802_AUTO_PATH_MAX];
} ff802_dchk_roots;

/* /sys, /var/lib/ff802-cc-auto, /proc/asound */
void ff802_dchk_default_roots(ff802_dchk_roots *r);

ff802_dchk_status ff802_disable_check(const ff802_dchk_roots *r,
                                      int select_location, uint8_t bus,
                                      uint8_t devnum, ff802_dchk_target *t,
                                      char *why, size_t why_size);

const char *ff802_dchk_status_str(ff802_dchk_status st);

#ifdef __cplusplus
}
#endif

#endif /* FF802_DISABLE_CHECK_H */
