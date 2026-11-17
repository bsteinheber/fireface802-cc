/*
 * Pure signal functions of ff802-chanmap: the test signal never exceeds
 * -40 dBFS, the output guard rejects anything above (and NaN/inf), the
 * detectors measure level, latency and polarity correctly.
 */
#include "../test_common.h"
#include "ff802/chanmap.h"

#include <math.h>
#include <stdlib.h>

int main(void)
{
    double peak = 0.0;
    float buf[8];
    float *x;
    int pol;
    size_t lag;

    /* the bound itself */
    CHECK(fabs(chanmap_dbfs(CHANMAP_MAX_PEAK) + 40.0) < 1e-9);
    CHECK(CHANMAP_AMPLITUDE < CHANMAP_MAX_PEAK);

    /* every sample of a step stays at or below the amplitude */
    for (size_t n = 0; n < CHANMAP_STEP_FRAMES; n++) {
        double v = fabs(chanmap_step_sample(n));
        if (v > peak)
            peak = v;
    }
    CHECK(peak <= CHANMAP_AMPLITUDE + 1e-12);
    CHECK(peak > CHANMAP_AMPLITUDE * 0.99);
    /* silence before the chirp, between chirp and tone, after the tone */
    CHECK(chanmap_step_sample(0) == 0.0);
    CHECK(chanmap_step_sample(CHANMAP_PRE_FRAMES - 1) == 0.0);
    CHECK(chanmap_step_sample(CHANMAP_PRE_FRAMES + CHANMAP_CHIRP_FRAMES + 10) == 0.0);
    CHECK(chanmap_step_sample(CHANMAP_STEP_FRAMES - 1) == 0.0);
    /* fades: no step at the edges */
    CHECK(fabs(chanmap_step_sample(CHANMAP_PRE_FRAMES)) < 1e-9);
    CHECK(fabs(chanmap_step_sample(CHANMAP_PRE_FRAMES + CHANMAP_CHIRP_FRAMES +
                                   CHANMAP_GAP_FRAMES)) < 1e-9);

    /* output guard */
    for (int i = 0; i < 8; i++)
        buf[i] = 0.0f;
    buf[3] = (float)CHANMAP_AMPLITUDE;
    CHECK(chanmap_output_guard(buf, 8) == 1);
    buf[3] = (float)-CHANMAP_AMPLITUDE;
    CHECK(chanmap_output_guard(buf, 8) == 1);
    buf[3] = 0.0101f;
    CHECK(chanmap_output_guard(buf, 8) == 0);
    buf[3] = -0.0101f;
    CHECK(chanmap_output_guard(buf, 8) == 0);
    buf[3] = NAN;
    CHECK(chanmap_output_guard(buf, 8) == 0);
    buf[3] = INFINITY;
    CHECK(chanmap_output_guard(buf, 8) == 0);
    buf[3] = 1.0f;
    CHECK(chanmap_output_guard(buf, 8) == 0);

    /* Goertzel level on a synthetic tone, stride 22 */
    x = calloc((size_t)48000 * CHANMAP_CHANNELS, sizeof(float));
    CHECK(x != NULL);
    if (!x)
        TEST_EXIT();
    for (size_t n = 0; n < 48000; n++)
        x[n * CHANMAP_CHANNELS + 4] = (float)(0.001 * sin(2.0 * 3.14159265358979 * 1000.0 * (double)n / 48000.0 + 0.3));
    CHECK(fabs(chanmap_tone_amplitude(x + 4, 48000, CHANMAP_CHANNELS) - 0.001) < 1e-5);
    CHECK(chanmap_tone_amplitude(x + 5, 48000, CHANMAP_CHANNELS) == 0.0);
    CHECK(chanmap_dbfs(0.0) == -200.0);
    CHECK(fabs(chanmap_dbfs(0.001) + 60.0) < 1e-9);
    free(x);

    /* chirp correlation: latency and polarity */
    x = calloc((size_t)(CHANMAP_CHIRP_FRAMES + CHANMAP_MAX_LATENCY) * 2u, sizeof(float));
    CHECK(x != NULL);
    if (!x)
        TEST_EXIT();
    for (size_t k = 0; k < CHANMAP_CHIRP_FRAMES; k++)
        x[(k + 4411) * 2 + 1] = (float)(-0.003 * chanmap_chirp_ref(k));
    lag = chanmap_find_chirp(x + 1, CHANMAP_CHIRP_FRAMES + CHANMAP_MAX_LATENCY, 2,
                             CHANMAP_MAX_LATENCY, &pol);
    CHECK(lag == 4411);
    CHECK(pol == -1);
    for (size_t k = 0; k < CHANMAP_CHIRP_FRAMES + CHANMAP_MAX_LATENCY; k++)
        x[k * 2 + 1] = 0.0f;
    for (size_t k = 0; k < CHANMAP_CHIRP_FRAMES; k++)
        x[(k + 7) * 2 + 1] = (float)(0.003 * chanmap_chirp_ref(k));
    lag = chanmap_find_chirp(x + 1, CHANMAP_CHIRP_FRAMES + CHANMAP_MAX_LATENCY, 2,
                             CHANMAP_MAX_LATENCY, &pol);
    CHECK(lag == 7 && pol == 1);
    free(x);

    /* status texts exist for every status */
    for (int s = CHANMAP_OK; s <= CHANMAP_ABORT_MIXER_CHANGED; s++)
        CHECK(strcmp(chanmap_status_text((enum chanmap_status)s), "unknown") != 0);

    TEST_EXIT();
}
