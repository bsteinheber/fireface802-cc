/*
 * ALSA backend of ff802-chanmap: full-duplex hw: access to one card.
 *
 * Only the card selected by its ALSA id is opened ("hw:CARD=<id>,DEV=0",
 * never "default" or a plug device). Before any PCM is opened, the control
 * device must report the identity of the original Fireface 802. Mixer
 * controls are only read, never written.
 */
#ifndef FF802_CHANMAP_ALSA_H
#define FF802_CHANMAP_ALSA_H

#include "ff802/chanmap.h"

#include <alsa/asoundlib.h>

struct chanmap_alsa {
    char pcm_name[64];
    char ctl_name[64];
    int verify_card;            /* 1: require Fireface 802 card info */
    snd_pcm_t *play;
    snd_pcm_t *cap;
    snd_ctl_t *ctl;
    int linked;                 /* playback and capture start together */
    snd_pcm_format_t format;
    unsigned bytes;             /* bytes per sample */
    void *raw;                  /* conversion buffer, one period */
    char info[256];             /* opened format, for the log */
};

/* Select the card by ALSA id (letters, digits, '_' only). 0 or -EINVAL. */
int chanmap_alsa_setup(struct chanmap_alsa *a, const char *card_id);

/* Test helper: arbitrary PCM name without card verification and without
 * mixer (used only by the container tests with the "null" plugin). */
int chanmap_alsa_setup_unverified(struct chanmap_alsa *a, const char *pcm_name);

void chanmap_alsa_backend(struct chanmap_alsa *a, struct chanmap_backend *be);

/* Sample conversion between float and the device format. encode refuses
 * (-EDOM) any sample above -40 dBFS after conversion: the second output
 * guard, independent of the one in the measurement flow. */
int chanmap_alsa_encode(snd_pcm_format_t format, const float *in, size_t n, void *out);
int chanmap_alsa_decode(snd_pcm_format_t format, const void *in, size_t n, float *out);

#endif /* FF802_CHANMAP_ALSA_H */
