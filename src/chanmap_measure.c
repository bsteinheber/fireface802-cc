/*
 * Measurement flow of ff802-chanmap against an abstract backend.
 *
 * Playback index s (0..TEST_PLAY-1) gets the test signal in step s, all
 * other channels stay silent. All CHANMAP_CHANNELS capture channels are
 * recorded and analysed after each step.
 */
#include "ff802/chanmap.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define CH CHANMAP_CHANNELS
#define STEP CHANMAP_STEP_FRAMES
#define CHIRP_START CHANMAP_PRE_FRAMES
#define TONE_START (CHANMAP_PRE_FRAMES + CHANMAP_CHIRP_FRAMES + CHANMAP_GAP_FRAMES)
#define TAIL_START (TONE_START + CHANMAP_TONE_FRAMES)

/* Analysis windows (frames within a step), all multiples of one 1 kHz
 * period (48 frames). */
#define NOISE_BEGIN 0u
#define NOISE_LEN   CHANMAP_PRE_FRAMES
#define TONE_BEGIN  (TONE_START + 14400u)              /* after latency + fade */
#define TONE_LEN    (CHANMAP_TONE_FRAMES - 14400u - 2400u)
#define DECAY_BEGIN (TAIL_START + CHANMAP_MAX_LATENCY)
#define DECAY_LEN   (STEP - DECAY_BEGIN)

/* Silence written after the last step and on every abort path. */
#define END_SILENCE_PERIODS 4u

#define MIXER_SNAPSHOT_LEN 65536u

const char *chanmap_status_text(enum chanmap_status s)
{
    switch (s) {
    case CHANMAP_OK: return "ok";
    case CHANMAP_ERR_OPEN: return "device could not be opened";
    case CHANMAP_ERR_BUSY: return "device stayed busy";
    case CHANMAP_ERR_RESERVE: return "device reservation (pw-reserve) failed";
    case CHANMAP_ERR_IO: return "audio read/write error";
    case CHANMAP_ERR_NOMEM: return "out of memory";
    case CHANMAP_ABORT_SIGNAL: return "aborted by signal";
    case CHANMAP_ABORT_INPUT_LEVEL: return "aborted: input above -20 dBFS";
    case CHANMAP_ABORT_FEEDBACK: return "aborted: signal did not decay (feedback)";
    case CHANMAP_ABORT_OUTPUT_GUARD: return "aborted: output sample above -40 dBFS";
    case CHANMAP_ABORT_MIXER_CHANGED: return "mixer state changed during the measurement";
    }
    return "unknown";
}

static void set_detail(struct chanmap_result *res, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->detail, sizeof res->detail, fmt, ap);
    va_end(ap);
}

static void logf_(const struct chanmap_config *cfg, const char *fmt, ...)
{
    va_list ap;
    if (cfg == NULL || cfg->log == NULL)
        return;
    va_start(ap, fmt);
    vfprintf(cfg->log, fmt, ap);
    va_end(ap);
    fflush(cfg->log);
}

static double rms(const float *x, size_t n, size_t stride)
{
    double s = 0.0;
    for (size_t i = 0; i < n; i++) {
        double v = (double)x[i * stride];
        s += v * v;
    }
    return n ? sqrt(s / (double)n) : 0.0;
}

/* Analyse one recorded step. Returns CHANMAP_OK or CHANMAP_ABORT_FEEDBACK. */
static enum chanmap_status analyse_step(unsigned play, const float *cap,
                                        struct chanmap_result *res,
                                        const struct chanmap_config *cfg)
{
    for (unsigned c = 0; c < CH; c++) {
        const float *x = cap + c;
        double noise = chanmap_tone_amplitude(x + (size_t)NOISE_BEGIN * CH, NOISE_LEN, CH);
        double tone = chanmap_tone_amplitude(x + (size_t)TONE_BEGIN * CH, TONE_LEN, CH);
        double decay = chanmap_tone_amplitude(x + (size_t)DECAY_BEGIN * CH, DECAY_LEN, CH);
        double rms_noise = rms(x + (size_t)NOISE_BEGIN * CH, NOISE_LEN, CH);
        double rms_decay = rms(x + (size_t)DECAY_BEGIN * CH, DECAY_LEN, CH);
        double level = chanmap_dbfs(tone);
        double snr = level - chanmap_dbfs(noise > 1e-10 ? noise : 1e-10);
        double decay_limit;
        struct chanmap_hit *h;
        int pol;

        /* Broadband check: anything that keeps ringing after the tone. */
        if (rms_decay > 1e-4 && chanmap_dbfs(rms_decay) - chanmap_dbfs(rms_noise) > 20.0) {
            set_detail(res, "step %u: capture %u stays at %.1f dBFS RMS after the tone",
                       play, c, chanmap_dbfs(rms_decay));
            return CHANMAP_ABORT_FEEDBACK;
        }
        if (!(level > CHANMAP_DETECT_MIN_DBFS && snr > CHANMAP_DETECT_MIN_SNR))
            continue;

        /* The tone must have decayed by 30 dB or to the noise floor. */
        decay_limit = fmax(tone * pow(10.0, -CHANMAP_DECAY_MIN_DB / 20.0),
                           noise * pow(10.0, 10.0 / 20.0));
        if (decay > decay_limit) {
            set_detail(res, "step %u: 1 kHz on capture %u did not decay (%.1f dBFS after the tone)",
                       play, c, chanmap_dbfs(decay));
            return CHANMAP_ABORT_FEEDBACK;
        }
        if (res->n_hits >= CHANMAP_MAX_HITS)
            continue;
        h = &res->hits[res->n_hits++];
        h->play = play;
        h->capture = c;
        h->level_dbfs = level;
        h->snr_db = snr;
        h->latency_ms = 1000.0 *
            (double)chanmap_find_chirp(x + (size_t)CHIRP_START * CH,
                                       CHANMAP_CHIRP_FRAMES + CHANMAP_MAX_LATENCY, CH,
                                       CHANMAP_MAX_LATENCY, &pol) / CHANMAP_RATE;
        h->polarity = pol;
        logf_(cfg, "  play %2u -> capture %2u  %6.1f dBFS  SNR %5.1f dB  %5.1f ms  %s\n",
              play, c, level, snr, h->latency_ms, pol > 0 ? "+" : "-");
    }
    return CHANMAP_OK;
}

static void write_silence(const struct chanmap_backend *be, float *buf, float *scratch)
{
    memset(buf, 0, sizeof(float) * CHANMAP_PERIOD * CH);
    for (unsigned i = 0; i < END_SILENCE_PERIODS; i++) {
        if (be->write(be->ctx, buf, CHANMAP_PERIOD) < 0)
            break;
        /* keep the capture side drained so the stream does not overrun */
        if (be->read(be->ctx, scratch, CHANMAP_PERIOD) < 0)
            break;
    }
}

enum chanmap_status chanmap_measure(const struct chanmap_backend *be,
                                    const struct chanmap_config *cfg,
                                    struct chanmap_result *res)
{
    enum chanmap_status st = CHANMAP_OK;
    float *play = NULL, *capbuf = NULL, *step = NULL;
    char *mix_before = NULL, *mix_after = NULL;
    size_t total = (size_t)CHANMAP_TEST_PLAY * STEP;
    size_t t = 0;
    int io_failed = 0;

    memset(res, 0, sizeof *res);

    play = malloc(sizeof(float) * CHANMAP_PERIOD * CH);
    capbuf = malloc(sizeof(float) * CHANMAP_PERIOD * CH);
    step = calloc((size_t)STEP * CH, sizeof(float));
    if (!play || !capbuf || !step) {
        st = CHANMAP_ERR_NOMEM;
        goto out;
    }

    if (be->mixer_snapshot) {
        mix_before = calloc(1, MIXER_SNAPSHOT_LEN);
        mix_after = calloc(1, MIXER_SNAPSHOT_LEN);
        if (!mix_before || !mix_after) {
            st = CHANMAP_ERR_NOMEM;
            goto out;
        }
        if (be->mixer_snapshot(be->ctx, mix_before, MIXER_SNAPSHOT_LEN) < 0)
            mix_before[0] = '\0';
        logf_(cfg, "mixer state before:\n%s", mix_before);
    }

    while (t < total) {
        size_t s = t / STEP;

        if (cfg && cfg->abort_flag && *cfg->abort_flag) {
            st = CHANMAP_ABORT_SIGNAL;
            set_detail(res, "abort requested at step %zu", s);
            break;
        }
        if (t % STEP == 0)
            logf_(cfg, "step %zu: signal on playback %zu\n", s, s);

        memset(play, 0, sizeof(float) * CHANMAP_PERIOD * CH);
        for (size_t f = 0; f < CHANMAP_PERIOD; f++) {
            size_t g = t + f;
            if (g < total)
                play[f * CH + g / STEP] = (float)chanmap_step_sample(g % STEP);
        }
        /* Never hand anything above -40 dBFS to the device. */
        if (!chanmap_output_guard(play, (size_t)CHANMAP_PERIOD * CH)) {
            st = CHANMAP_ABORT_OUTPUT_GUARD;
            set_detail(res, "output peak %.6f above %.6f", chanmap_peak(play,
                       (size_t)CHANMAP_PERIOD * CH), CHANMAP_MAX_PEAK);
            break;
        }
        {
            double p = chanmap_peak(play, (size_t)CHANMAP_PERIOD * CH);
            if (p > res->max_output_peak)
                res->max_output_peak = p;
        }
        {
            int err = be->write(be->ctx, play, CHANMAP_PERIOD);
            if (err >= 0)
                err = be->read(be->ctx, capbuf, CHANMAP_PERIOD);
            if (err < 0) {
                st = CHANMAP_ERR_IO;
                io_failed = 1;
                set_detail(res, "audio I/O error %d (%s) at frame %zu", err, strerror(-err), t);
                break;
            }
        }
        {
            double p = chanmap_peak(capbuf, (size_t)CHANMAP_PERIOD * CH);
            if (p > res->max_input_peak)
                res->max_input_peak = p;
            if (p > CHANMAP_ABORT_INPUT) {
                st = CHANMAP_ABORT_INPUT_LEVEL;
                set_detail(res, "capture peak %.1f dBFS at step %zu", chanmap_dbfs(p), s);
                break;
            }
        }
        for (size_t f = 0; f < CHANMAP_PERIOD && t + f < total; f++) {
            size_t g = t + f;
            memcpy(step + (g % STEP) * CH, capbuf + f * CH, sizeof(float) * CH);
            if ((g + 1) % STEP == 0) {
                st = analyse_step((unsigned)(g / STEP), step, res, cfg);
                if (st != CHANMAP_OK)
                    break;
                res->steps_done++;
            }
        }
        if (st != CHANMAP_OK)
            break;
        t += CHANMAP_PERIOD;
    }

out:
    if (play && capbuf && !io_failed)
        write_silence(be, play, capbuf);

    if (st == CHANMAP_OK && mix_before && mix_after) {
        if (be->mixer_snapshot(be->ctx, mix_after, MIXER_SNAPSHOT_LEN) < 0)
            mix_after[0] = '\0';
        res->mixer_checked = 1;
        if (strcmp(mix_before, mix_after) != 0) {
            st = CHANMAP_ABORT_MIXER_CHANGED;
            set_detail(res, "mixer controls differ before and after the measurement");
            logf_(cfg, "mixer state after:\n%s", mix_after);
        }
    }
    be->close(be->ctx);

    free(play);
    free(capbuf);
    free(step);
    free(mix_before);
    free(mix_after);
    res->status = st;
    if (st != CHANMAP_OK && res->detail[0] == '\0')
        set_detail(res, "%s", chanmap_status_text(st));
    return st;
}
