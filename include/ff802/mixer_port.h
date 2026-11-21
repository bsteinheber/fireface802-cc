/*
 * Mixer initialisation: finding the MIDI port of ONE USB connection of
 * the Fireface 802 in class-compliant mode, without fixed ALSA numbers.
 *
 * 1. Sound card of the connection, from sysfs only: an interface
 *    directory <k>:1.<n> of the USB device <k> has sound/card<N> with
 *    the MIDI device midiC<N>D0. The path goes through the kernel name
 *    of the connection, so two units are never confused, and the card
 *    number is whatever the kernel assigned this time.
 *
 * 2. ALSA sequencer port, from a list of all clients and ports: the one
 *    kernel client whose card is N, its port 1 (the second rawmidi
 *    subdevice, "Port 2"), writable and subscribable, with a name that
 *    ends in "Port 2" (seen on the unit: client 'Fireface 802 (24047128)'
 *    [type=Kernel,card=1], port 1 'Fireface 802 (24047128) Port 2').
 *    Index and name must agree; anything else is refused.
 *
 * No device node is opened here; the list of ports comes from the MIDI
 * backend. The sysfs root is a parameter, so the tests use a fake tree.
 */
#ifndef FF802_MIXER_PORT_H
#define FF802_MIXER_PORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_MIXER_PORT_INDEX   1
#define FF802_MIXER_PORT_SUFFIX  "Port 2"
#define FF802_SEQ_NAME_MAX       64u

typedef enum ff802_mport_status {
    FF802_MPORT_FOUND = 0,
    FF802_MPORT_NOT_YET,      /* no card with MIDI device (yet) */
    FF802_MPORT_AMBIGUOUS,    /* more than one card for the connection */
    FF802_MPORT_BAD           /* invalid name or unreadable sysfs */
} ff802_mport_status;

/* Card number of the connection <kernel_name> (see 1. above). */
ff802_mport_status ff802_mixer_find_card(const char *sysfs_root,
                                         const char *kernel_name, int *card);

/* One sequencer port with the attributes of its client. */
typedef struct ff802_seq_port {
    int  client;
    int  port;
    int  kernel_client;     /* client type is kernel */
    int  card;              /* card of the client, -1 if none */
    int  writable;          /* WRITE and SUBS_WRITE capabilities */
    char name[FF802_SEQ_NAME_MAX];
} ff802_seq_port;

typedef enum ff802_seqsel {
    FF802_SEQSEL_FOUND = 0,
    FF802_SEQSEL_NO_CLIENT,   /* no kernel client of the card (yet) */
    FF802_SEQSEL_NO_PORT,     /* the client has no port 1 (yet) */
    FF802_SEQSEL_AMBIGUOUS,   /* more than one kernel client of the card */
    FF802_SEQSEL_MISMATCH     /* port 1 not writable or name does not match */
} ff802_seqsel;

/* Select the port (see 2. above); *index is its position in ports. */
ff802_seqsel ff802_mixer_select_port(const ff802_seq_port *ports, size_t n,
                                     int card, size_t *index);

const char *ff802_mport_status_str(ff802_mport_status s);
const char *ff802_seqsel_str(ff802_seqsel s);

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIXER_PORT_H */
