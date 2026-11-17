/*
 * Test signal generation, output guard and detection for ff802-chanmap.
 * Pure functions, no I/O.
 */
#include "ff802/chanmap.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CHIRP_F0 300.0
#define CHIRP_F1 3000.0
#define CHIRP_FADE 960u   /* 20 ms */
#define TONE_FADE 2400u   /* 50 ms */

/* Raised cosine fade in and out over `fade` frames. */
static double fade(size_t k, size_t len, size_t f)
{
    if (k < f)
        return 0.5 * (1.0 - cos(M_PI * (double)k / (double)f));
    if (k + f > len)
        return 0.5 * (1.0 - cos(M_PI * (double)(len - k) / (double)f));
    return 1.0;
}

double chanmap_chirp_ref(size_t k)
{
    double t = (double)k / CHANMAP_RATE;
    double dur = (double)CHANMAP_CHIRP_FRAMES / CHANMAP_RATE;
    double phase = 2.0 * M_PI * (CHIRP_F0 * t + (CHIRP_F1 - CHIRP_F0) * t * t / (2.0 * dur));

    if (k >= CHANMAP_CHIRP_FRAMES)
        return 0.0;
    return fade(k, CHANMAP_CHIRP_FRAMES, CHIRP_FADE) * sin(phase);
}

double chanmap_step_sample(size_t n)
{
    size_t chirp_start = CHANMAP_PRE_FRAMES;
    size_t tone_start = chirp_start + CHANMAP_CHIRP_FRAMES + CHANMAP_GAP_FRAMES;
    double v = 0.0;

    if (n >= chirp_start && n < chirp_start + CHANMAP_CHIRP_FRAMES) {
        v = chanmap_chirp_ref(n - chirp_start);
    } else if (n >= tone_start && n < tone_start + CHANMAP_TONE_FRAMES) {
        size_t k = n - tone_start;
        v = fade(k, CHANMAP_TONE_FRAMES, TONE_FADE) *
            sin(2.0 * M_PI * CHANMAP_TONE_HZ * (double)k / CHANMAP_RATE);
    }
    return CHANMAP_AMPLITUDE * v;
}

double chanmap_peak(const float *buf, size_t samples)
{
    double p = 0.0;
    for (size_t i = 0; i < samples; i++) {
        double a = fabs((double)buf[i]);
        if (a != a)
            return INFINITY; /* NaN never passes the guard */
        if (a > p)
            p = a;
    }
    return p;
}

int chanmap_output_guard(const float *buf, size_t samples)
{
    return chanmap_peak(buf, samples) <= CHANMAP_MAX_PEAK;
}

double chanmap_tone_amplitude(const float *x, size_t n, size_t stride)
{
    double w = 2.0 * M_PI * CHANMAP_TONE_HZ / CHANMAP_RATE;
    double coeff = 2.0 * cos(w);
    double s1 = 0.0, s2 = 0.0;

    if (n == 0)
        return 0.0;
    for (size_t i = 0; i < n; i++) {
        double s0 = (double)x[i * stride] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    {
        double re = s1 - s2 * cos(w);
        double im = s2 * sin(w);
        return 2.0 * sqrt(re * re + im * im) / (double)n;
    }
}

double chanmap_dbfs(double amplitude)
{
    if (!(amplitude > 1e-10))
        return -200.0;
    return 20.0 * log10(amplitude);
}

static float chirp_table[CHANMAP_CHIRP_FRAMES];
static int chirp_table_ready;

static double corr_at(const float *x, size_t stride, size_t lag)
{
    double c = 0.0;
    if (!chirp_table_ready) {
        for (size_t k = 0; k < CHANMAP_CHIRP_FRAMES; k++)
            chirp_table[k] = (float)chanmap_chirp_ref(k);
        chirp_table_ready = 1;
    }
    for (size_t k = 0; k < CHANMAP_CHIRP_FRAMES; k++)
        c += (double)chirp_table[k] * (double)x[(k + lag) * stride];
    return c;
}

size_t chanmap_find_chirp(const float *x, size_t frames, size_t stride,
                          size_t max_lag, int *polarity)
{
    size_t best = 0, lo, hi;
    double best_abs = -1.0, best_val = 0.0;

    if (frames < CHANMAP_CHIRP_FRAMES) {
        *polarity = 1;
        return 0;
    }
    if (max_lag > frames - CHANMAP_CHIRP_FRAMES)
        max_lag = frames - CHANMAP_CHIRP_FRAMES;

    /* Coarse search: the main lobe of the chirp autocorrelation is about
     * 18 frames wide, so a step of 4 cannot miss it. */
    for (size_t lag = 0; lag <= max_lag; lag += 4) {
        double c = corr_at(x, stride, lag);
        if (fabs(c) > best_abs) {
            best_abs = fabs(c);
            best_val = c;
            best = lag;
        }
    }
    lo = best >= 4 ? best - 4 : 0;
    hi = best + 4 <= max_lag ? best + 4 : max_lag;
    for (size_t lag = lo; lag <= hi; lag++) {
        double c = corr_at(x, stride, lag);
        if (fabs(c) > best_abs) {
            best_abs = fabs(c);
            best_val = c;
            best = lag;
        }
    }
    *polarity = best_val < 0.0 ? -1 : 1;
    return best;
}
