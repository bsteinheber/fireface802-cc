/*
 * ALSA sequencer backend of the mixer initialisation (midi_backend.h).
 *
 * The only module that talks to ALSA for the mixer. It opens the
 * sequencer ("hw") as client "ff802-cc-auto" and lists clients and
 * ports. Creating a port, subscribing to the Fireface port and sending
 * SysEx events are compiled in only with FF802_ENABLE_MIXER_WRITE
 * (CMake option, default OFF); without it connect and send_sysex return
 * -EPERM and the program does not reference any ALSA output function.
 *
 * Sending: one direct (unqueued) SysEx event per register to the
 * subscribed port; the kernel's snd-seq-midi client passes it to the
 * rawmidi output of the card. Closing unsubscribes and closes the client.
 */
#ifndef FF802_MIDI_SEQ_ALSA_H
#define FF802_MIDI_SEQ_ALSA_H

#include "ff802/midi_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 if this build can subscribe and send (FF802_ENABLE_MIXER_WRITE). */
int ff802_midi_seq_write_enabled(void);

/* Factory for ff802_mixer_run(); ctx is unused. */
ff802_midi_factory ff802_midi_seq_factory(void);

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIDI_SEQ_ALSA_H */
