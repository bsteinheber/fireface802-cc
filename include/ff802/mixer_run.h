/*
 * Mixer initialisation of one USB connection of the Fireface 802 in
 * class-compliant mode (called by the CC branch of the automatic switch,
 * src/auto_switch.c, when "ff802-cc-auto mixer-init on").
 *
 *   register table check (formulas, checksum) -> wait for the sound card
 *   of the connection (sysfs) -> open the MIDI backend -> wait for the
 *   sequencer port "Port 2" of that card -> writes compiled in? -> check
 *   the connection again (same bus/devnum, still CC mode, same card) ->
 *   subscribe -> the 24 SysEx messages of mixer_init.h in order, one
 *   journal line each -> close.
 *
 * No retry, no rollback (the previous values are not known: nothing is
 * read from the device). An error while sending stops at that message
 * and reports how many were passed to ALSA. "done" means that all 24
 * messages were passed to ALSA; it does not confirm that the device
 * applied them.
 */
#ifndef FF802_MIXER_RUN_H
#define FF802_MIXER_RUN_H

#include <stdio.h>

#include "ff802/auto_sysfs.h"
#include "ff802/midi_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_MIXER_TIMEOUT_MS 10000u   /* wait for card and port */
#define FF802_MIXER_POLL_MS      100u
#define FF802_MIXER_GAP_MS         1u   /* pause between two messages */

typedef enum ff802_mixer_outcome {
    FF802_MIXER_OFF = 0,        /* mixer-init is not on: nothing done */
    FF802_MIXER_DONE,           /* all 24 messages passed to ALSA */
    FF802_MIXER_DISABLED_BUILD, /* writes not compiled in: nothing sent */
    FF802_MIXER_UNAVAILABLE,    /* no MIDI backend: nothing sent */
    FF802_MIXER_NO_PORT,        /* card or port did not appear in time */
    FF802_MIXER_REFUSED,        /* not unambiguous, changed, or table check */
    FF802_MIXER_NOT_SENT,       /* backend or subscription failed */
    FF802_MIXER_INCOMPLETE      /* sending failed after <written> messages */
} ff802_mixer_outcome;

typedef struct ff802_mixer_options {
    unsigned timeout_ms;
    unsigned poll_ms;
    unsigned gap_ms;
} ff802_mixer_options;

typedef struct ff802_mixer_result {
    ff802_mixer_outcome outcome;
    unsigned written;            /* messages passed to ALSA */
    int  card, client, port;     /* -1 if not found */
    char port_name[FF802_SEQ_NAME_MAX];
    char detail[192];
} ff802_mixer_result;

/* Defaults: FF802_MIXER_TIMEOUT_MS, _POLL_MS, _GAP_MS. */
void ff802_mixer_default_options(ff802_mixer_options *o);

/*
 * Run the initialisation for the connection d (as identified by the
 * caller: CC mode, bus/devnum of the event). opt NULL: defaults.
 */
ff802_mixer_outcome ff802_mixer_run(const char *sysfs_root,
                                    const ff802_sysfs_dev *d,
                                    const ff802_midi_factory *f,
                                    const ff802_mixer_options *opt,
                                    FILE *log, ff802_mixer_result *res);

/* Stable word for logs and the mixer-result file, e.g. "done". */
const char *ff802_mixer_outcome_str(ff802_mixer_outcome o);

/* 1 for the outcomes that are reported as a failed initialisation. */
int ff802_mixer_outcome_failed(ff802_mixer_outcome o);

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIXER_RUN_H */
