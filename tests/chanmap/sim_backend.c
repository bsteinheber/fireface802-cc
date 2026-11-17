#include "sim_backend.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CH CHANMAP_CHANNELS
#define MASK (SIM_RING - 1u)

void sim_config_default(struct sim_config *c)
{
    memset(c, 0, sizeof *c);
    c->latency = 4410;  /* ~92 ms, like a 4096 frame buffer plus USB */
    c->fail_write_at = -1;
    c->fail_read_at = -1;
    c->raise_at = -1;
    c->fail_errno = EPIPE;
}

int sim_init(struct sim_state *s, const struct sim_config *c)
{
    memset(s, 0, sizeof *s);
    s->cfg = *c;
    s->rng = 12345u;
    s->abort_frame = -1;
    s->play_ring = calloc((size_t)SIM_RING * CH, sizeof(float));
    s->cap_ring = calloc((size_t)SIM_RING * CH, sizeof(float));
    return (s->play_ring && s->cap_ring) ? 0 : -1;
}

void sim_free(struct sim_state *s)
{
    free(s->play_ring);
    free(s->cap_ring);
    s->play_ring = s->cap_ring = NULL;
}

static double noise(struct sim_state *s)
{
    s->rng = s->rng * 1103515245u + 12345u;
    return ((double)((s->rng >> 8) & 0xffff) / 32768.0 - 1.0) * s->cfg.noise;
}

static int sim_open(void *ctx)
{
    struct sim_state *s = ctx;
    s->opens++;
    if (s->cfg.open_error)
        return s->cfg.open_error;
    if (s->cfg.busy_opens < 0 || s->opens <= s->cfg.busy_opens)
        return -EBUSY;
    s->is_open = 1;
    return 0;
}

static int sim_write(void *ctx, const float *buf, size_t frames)
{
    struct sim_state *s = ctx;
    if (!s->is_open)
        return -EBADF;
    for (size_t f = 0; f < frames; f++) {
        unsigned long long g = s->written;
        int nz = 0;
        if (s->cfg.fail_write_at >= 0 && g == (unsigned long long)s->cfg.fail_write_at)
            return -s->cfg.fail_errno;
        if (s->cfg.raise_at >= 0 && g == (unsigned long long)s->cfg.raise_at && s->abort_frame < 0) {
            s->abort_frame = (long)g;
            raise(SIGINT);
        }
        for (unsigned c = 0; c < CH; c++) {
            float v = buf[f * CH + c];
            s->play_ring[(g & MASK) * CH + c] = v;
            if (v != 0.0f)
                nz = 1;
            if (fabs((double)v) > s->max_written)
                s->max_written = fabs((double)v);
        }
        if (nz) {
            s->last_nonzero = g;
            if (s->abort_frame >= 0 && g > (unsigned long long)s->abort_frame)
                s->nonzero_after++;
        }
        s->written++;
    }
    return 0;
}

static int sim_read(void *ctx, float *buf, size_t frames)
{
    struct sim_state *s = ctx;
    if (!s->is_open)
        return -EBADF;
    for (size_t f = 0; f < frames; f++) {
        unsigned long long g = s->read;
        if (s->cfg.fail_read_at >= 0 && g == (unsigned long long)s->cfg.fail_read_at)
            return -s->cfg.fail_errno;
        for (unsigned c = 0; c < CH; c++)
            buf[f * CH + c] = (float)noise(s);
        if (g >= s->cfg.latency) {
            unsigned long long src = g - s->cfg.latency;
            for (unsigned r = 0; r < s->cfg.n_routes; r++) {
                const struct sim_route *rt = &s->cfg.routes[r];
                buf[f * CH + rt->capture] +=
                    (float)(rt->gain * (double)s->play_ring[(src & MASK) * CH + rt->play]);
            }
        }
        if (s->cfg.feedback != 0.0 && g >= s->cfg.fb_delay) {
            unsigned long long src = g - s->cfg.fb_delay;
            for (unsigned c = 0; c < CH; c++)
                buf[f * CH + c] += (float)(s->cfg.feedback * (double)s->cap_ring[(src & MASK) * CH + c]);
        }
        for (unsigned c = 0; c < CH; c++) {
            float v = buf[f * CH + c];
            /* a real converter clips at full scale */
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            buf[f * CH + c] = v;
            s->cap_ring[(g & MASK) * CH + c] = v;
        }
        s->read++;
    }
    return 0;
}

static void sim_close(void *ctx)
{
    struct sim_state *s = ctx;
    if (s->is_open)
        s->closes++;
    s->is_open = 0;
}

static int sim_mixer(void *ctx, char *out, size_t len)
{
    struct sim_state *s = ctx;
    s->snapshots++;
    snprintf(out, len, "1 'PCM Playback Volume' 0: %d\n2 'Clock Source' 0: #0\n",
             (s->cfg.mixer_change && s->snapshots > 1) ? 100 : 127);
    return 0;
}

void sim_backend(struct sim_state *s, struct chanmap_backend *be)
{
    be->ctx = s;
    be->open = sim_open;
    be->write = sim_write;
    be->read = sim_read;
    be->close = sim_close;
    be->mixer_snapshot = sim_mixer;
}
