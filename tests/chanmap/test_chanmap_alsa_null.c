/*
 * ALSA backend of ff802-chanmap against the ALSA "null" plugin (no sound
 * card): hw parameters for 22 channels, format conversion, the second
 * output guard on integer samples, close twice, and the card id check
 * that keeps the device name from being redirected.
 */
#include "../test_common.h"
#include "ff802/chanmap_alsa.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>

#define CH CHANMAP_CHANNELS

int main(void)
{
    struct chanmap_alsa a;
    struct chanmap_backend be;
    static float buf[CHANMAP_PERIOD * CH];
    char mixer[64];

    /* card ids cannot inject other device names or arguments */
    CHECK(chanmap_alsa_setup(&a, "F80224047128") == 0);
    CHECK(strcmp(a.pcm_name, "hw:CARD=F80224047128,DEV=0") == 0);
    CHECK(strcmp(a.ctl_name, "hw:CARD=F80224047128") == 0);
    CHECK(a.verify_card == 1);
    CHECK(chanmap_alsa_setup(&a, "F802,DEV=1") == -EINVAL);
    CHECK(chanmap_alsa_setup(&a, "default") == 0); /* a name, but verified below */
    CHECK(chanmap_alsa_setup(&a, "x}") == -EINVAL);
    CHECK(chanmap_alsa_setup(&a, "") == -EINVAL);
    CHECK(chanmap_alsa_setup(&a, "a b") == -EINVAL);
    CHECK(chanmap_alsa_setup(&a, "../x") == -EINVAL);
    CHECK(chanmap_alsa_setup(&a, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == -EINVAL);

    /* verified open of a card that does not exist fails cleanly */
    CHECK(chanmap_alsa_setup(&a, "NoSuchCard") == 0);
    chanmap_alsa_backend(&a, &be);
    CHECK(be.open(be.ctx) < 0);
    CHECK(a.play == NULL && a.cap == NULL && a.ctl == NULL);
    be.close(be.ctx);

    /* full duplex on the null plugin */
    CHECK(chanmap_alsa_setup_unverified(&a, "null") == 0);
    chanmap_alsa_backend(&a, &be);
    CHECK(be.open(be.ctx) == 0);
    CHECK(a.play != NULL && a.cap != NULL);
    printf("opened: %s\n", a.info);
    memset(buf, 0, sizeof buf);
    buf[5] = (float)CHANMAP_AMPLITUDE;
    buf[6] = (float)-CHANMAP_AMPLITUDE;
    CHECK(be.write(be.ctx, buf, CHANMAP_PERIOD) == 0);
    CHECK(be.read(be.ctx, buf, CHANMAP_PERIOD) == 0);
    /* second guard on the converted integer samples */
    memset(buf, 0, sizeof buf);
    buf[100] = 0.0102f;
    CHECK(be.write(be.ctx, buf, CHANMAP_PERIOD) == -EDOM);
    buf[100] = -1.0f;
    CHECK(be.write(be.ctx, buf, CHANMAP_PERIOD) == -EDOM);
    /* more than one period at once is refused */
    CHECK(be.write(be.ctx, buf, CHANMAP_PERIOD + 1) == -EINVAL);
    /* no control device: empty snapshot */
    CHECK(be.mixer_snapshot(be.ctx, mixer, sizeof mixer) == 0 && mixer[0] == '\0');
    be.close(be.ctx);
    be.close(be.ctx);
    CHECK(a.play == NULL && a.cap == NULL);
    CHECK(be.write(be.ctx, buf, 1) == -EINVAL);

    /* sample conversion in every supported format */
    {
        static const snd_pcm_format_t fmts[] = { SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_3LE,
                                                 SND_PCM_FORMAT_S24_LE, SND_PCM_FORMAT_S16_LE };
        const float in[6] = { 0.0f, (float)CHANMAP_AMPLITUDE, (float)-CHANMAP_AMPLITUDE,
                              0.005f, -0.0001f, 0.0098f };
        float back[6];
        uint8_t raw[6 * 4];

        for (size_t i = 0; i < 4; i++) {
            int ok = chanmap_alsa_encode(fmts[i], in, 6, raw) == 0;
            CHECK(ok);
            CHECK(chanmap_alsa_decode(fmts[i], raw, 6, back) == 0);
            for (int k = 0; k < 6; k++)
                CHECK(fabs((double)back[k] - (double)in[k]) < 1e-4);
            /* just above the bound, NaN and full scale are refused */
            {
                float bad[1] = { 0.0101f };
                CHECK(chanmap_alsa_encode(fmts[i], bad, 1, raw) == -EDOM);
                bad[0] = NAN;
                CHECK(chanmap_alsa_encode(fmts[i], bad, 1, raw) == -EDOM);
                bad[0] = -1.0f;
                CHECK(chanmap_alsa_encode(fmts[i], bad, 1, raw) == -EDOM);
            }
        }
        /* S24_3LE byte layout (the format of the Fireface in CC mode):
         * 0.0099 * 8388607 = 83047 = 0x014467, -83047 = 0xFEBB99 */
        CHECK(chanmap_alsa_encode(SND_PCM_FORMAT_S24_3LE, in + 1, 2, raw) == 0);
        CHECK(raw[0] == 0x67 && raw[1] == 0x44 && raw[2] == 0x01);
        CHECK(raw[3] == 0x99 && raw[4] == 0xBB && raw[5] == 0xFE);
        CHECK(chanmap_alsa_encode(SND_PCM_FORMAT_FLOAT_LE, in, 1, raw) == -EINVAL);
    }

    /* complete measurement through the real ALSA backend (null plugin:
     * nothing comes back, so no hits) */
    {
        static struct chanmap_result res;
        volatile sig_atomic_t flag = 0;
        struct chanmap_config cfg = { &flag, NULL };

        CHECK(chanmap_alsa_setup_unverified(&a, "null") == 0);
        chanmap_alsa_backend(&a, &be);
        CHECK(be.open(be.ctx) == 0);
        CHECK(chanmap_measure(&be, &cfg, &res) == CHANMAP_OK);
        CHECK(res.steps_done == CHANMAP_TEST_PLAY);
        CHECK(res.n_hits == 0);
        CHECK(res.max_output_peak <= CHANMAP_MAX_PEAK);
        CHECK(a.play == NULL && a.cap == NULL); /* closed by chanmap_measure */
    }

    TEST_EXIT();
}
