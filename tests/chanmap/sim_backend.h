/*
 * Simulated Fireface 802 for ff802-chanmap tests: a full-duplex device
 * with a configurable routing matrix from playback to capture channels,
 * latency, noise, feedback, I/O errors, busy device, mixer changes and a
 * signal raised at a given frame. Records what was written.
 */
#ifndef SIM_BACKEND_H
#define SIM_BACKEND_H

#include "ff802/chanmap.h"

#include <stddef.h>

#define SIM_MAX_ROUTES 32
#define SIM_RING 65536u  /* frames of playback history (power of two) */

struct sim_route {
    unsigned play, capture;
    double gain;          /* negative: inverted polarity */
};

struct sim_config {
    struct sim_route routes[SIM_MAX_ROUTES];
    unsigned n_routes;
    unsigned latency;     /* frames from playback to capture            */
    double noise;         /* uniform noise amplitude on every capture   */
    double feedback;      /* capture -> capture gain after fb_delay     */
    unsigned fb_delay;    /* frames                                    */
    long fail_write_at;   /* frame index, -1: never                    */
    long fail_read_at;
    int fail_errno;       /* e.g. EPIPE                                */
    long raise_at;        /* raise(SIGINT) when writing this frame, -1 */
    int busy_opens;       /* number of opens returning -EBUSY (-1: all) */
    int open_error;       /* other open error (negative errno), 0: none */
    int mixer_change;     /* snapshot differs after the first call     */
};

struct sim_state {
    struct sim_config cfg;
    float *play_ring;     /* SIM_RING * CHANNELS                        */
    float *cap_ring;      /* SIM_RING * CHANNELS (for feedback)         */
    unsigned long long written, read;
    unsigned long long nonzero_after; /* nonzero frames written after abort request */
    unsigned long long last_nonzero;  /* last frame index with a nonzero sample     */
    double max_written;
    int opens, closes, is_open, snapshots;
    unsigned rng;
    long abort_frame;     /* frame at which raise() happened (-1)       */
};

void sim_config_default(struct sim_config *c);
int sim_init(struct sim_state *s, const struct sim_config *c);
void sim_free(struct sim_state *s);
void sim_backend(struct sim_state *s, struct chanmap_backend *be);

#endif
