/*
 * Abstract MIDI backend of the mixer initialisation.
 *
 * The installed program uses the ALSA sequencer (src/midi_seq_alsa.c),
 * the tests a simulation. All functions return 0 or a negative errno.
 *
 *   list_ports   all clients and ports (read only)
 *   connect      subscribe to the destination port; the kernel opens the
 *                rawmidi output of the device only for a subscription
 *   send_sysex   one complete SysEx message to the connected port
 *
 * factory.close disconnects and closes the backend. write_enabled is 0
 * when the backend was built without FF802_ENABLE_MIXER_WRITE: then
 * connect and send_sysex refuse with -EPERM and are never called by the
 * flow (src/mixer_run.c).
 */
#ifndef FF802_MIDI_BACKEND_H
#define FF802_MIDI_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "ff802/mixer_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_SEQ_MAX_PORTS 128u

typedef struct ff802_midi_backend {
    void *self;
    int (*list_ports)(void *self, ff802_seq_port *out, size_t max, size_t *n);
    int (*connect)(void *self, int client, int port);
    int (*send_sysex)(void *self, const uint8_t *msg, size_t len);
} ff802_midi_backend;

typedef struct ff802_midi_factory {
    void *ctx;
    int  (*open)(void *ctx, ff802_midi_backend *be);
    void (*close)(void *ctx, ff802_midi_backend *be);
    void (*sleep_ms)(void *ctx, unsigned ms);
    int  write_enabled;
} ff802_midi_factory;

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIDI_BACKEND_H */
