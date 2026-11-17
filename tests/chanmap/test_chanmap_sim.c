/*
 * ff802-chanmap measurement flow against the simulated device: correct
 * and wrong assignments, crosstalk, polarity, noise, and every abort and
 * error path (input level, feedback, signal, I/O errors, busy device,
 * reservation, mixer change).
 */
#include "../test_common.h"
#include "sim_backend.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t abort_flag;
static void on_sigint(int sig)
{
    (void)sig;
    abort_flag = 1;
}

static struct chanmap_result res;

static enum chanmap_status run(const struct sim_config *c, struct sim_state *s)
{
    struct chanmap_backend be;
    struct chanmap_config cfg = { &abort_flag, NULL };
    enum chanmap_status st;

    abort_flag = 0;
    if (sim_init(s, c) != 0) {
        fprintf(stderr, "sim_init failed\n");
        exit(2);
    }
    sim_backend(s, &be);
    st = chanmap_open_with_reserve(&be, NULL, 1000, &abort_flag);
    if (st != CHANMAP_OK)
        return st;
    return chanmap_measure(&be, &cfg, &res);
}

static void route(struct sim_config *c, unsigned play, unsigned cap, double gain)
{
    c->routes[c->n_routes].play = play;
    c->routes[c->n_routes].capture = cap;
    c->routes[c->n_routes].gain = gain;
    c->n_routes++;
}

/* Common checks after every run that opened the device. */
static void check_safe_end(const struct sim_state *s)
{
    CHECK(s->closes == 1);
    CHECK(s->is_open == 0);
    CHECK(s->max_written <= CHANMAP_MAX_PEAK);
    CHECK(res.max_output_peak <= CHANMAP_MAX_PEAK);
}

/* The last written frames are silence. */
static void check_silence_written(const struct sim_state *s)
{
    CHECK(s->written > s->last_nonzero + 4u * CHANMAP_PERIOD - 1u);
}

static const struct chanmap_hit *hit_for(unsigned play)
{
    for (unsigned i = 0; i < res.n_hits; i++)
        if (res.hits[i].play == play)
            return &res.hits[i];
    return NULL;
}

int main(void)
{
    struct sim_config c;
    struct sim_state s;
    const struct chanmap_hit *h;
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);

    /* 1. correct assignment for out1: playback 0 -> capture 0 */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    c.noise = 1e-6;
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.steps_done == CHANMAP_TEST_PLAY);
    CHECK(res.n_hits == 1);
    h = hit_for(0);
    CHECK(h && h->capture == 0 && h->polarity == 1);
    CHECK(h && fabs(h->level_dbfs - chanmap_dbfs(CHANMAP_AMPLITUDE)) < 0.5);
    CHECK(h && fabs(h->latency_ms - 1000.0 * 4410 / 48000) < 0.1);
    CHECK(h && h->snr_db > 40.0);
    CHECK(res.mixer_checked == 1);
    CHECK(fabs(chanmap_dbfs(res.max_output_peak) + 40.0) < 0.2);
    check_safe_end(&s);
    check_silence_written(&s);
    CHECK(s.written == (unsigned long long)CHANMAP_TEST_PLAY * CHANMAP_STEP_FRAMES
                       + (CHANMAP_PERIOD - (CHANMAP_TEST_PLAY * CHANMAP_STEP_FRAMES) % CHANMAP_PERIOD)
                       + 4u * CHANMAP_PERIOD);
    sim_free(&s);

    /* 2. headphones 1 into a balanced input: L +, R - */
    sim_config_default(&c);
    route(&c, 8, 0, 1.0);
    route(&c, 9, 0, -1.0);
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.n_hits == 2);
    h = hit_for(8);
    CHECK(h && h->capture == 0 && h->polarity == 1);
    h = hit_for(9);
    CHECK(h && h->capture == 0 && h->polarity == -1);
    check_safe_end(&s);
    sim_free(&s);

    /* 3. wrong assignment: the jack carries playback 1 */
    sim_config_default(&c);
    route(&c, 1, 0, 1.0);
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.n_hits == 1 && hit_for(1) != NULL && hit_for(0) == NULL);
    sim_free(&s);

    /* 4. internal routing to several inputs (crosstalk) is reported */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    route(&c, 0, 5, 0.1);
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.n_hits == 2);
    sim_free(&s);

    /* 5. nothing connected: no hits, noise only */
    sim_config_default(&c);
    c.noise = 1e-5;
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.n_hits == 0);
    sim_free(&s);

    /* 6. attenuated chain (-30 dB) with -100 dBFS noise is still found */
    sim_config_default(&c);
    route(&c, 3, 0, 0.0316);
    c.noise = 1e-5;
    CHECK(run(&c, &s) == CHANMAP_OK);
    h = hit_for(3);
    CHECK(res.n_hits == 1 && h && fabs(h->level_dbfs + 70.0) < 1.0);
    sim_free(&s);

    /* 7. signal buried in noise: not reported as a hit */
    sim_config_default(&c);
    route(&c, 3, 0, 0.0001);   /* -120 dBFS */
    c.noise = 1e-5;
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(res.n_hits == 0);
    sim_free(&s);

    /* 8. unexpectedly high gain (+30 dB, -10 dBFS at the input): abort
     *    during the first step, silence written, device closed */
    sim_config_default(&c);
    route(&c, 0, 0, 31.6);
    CHECK(run(&c, &s) == CHANMAP_ABORT_INPUT_LEVEL);
    CHECK(res.steps_done == 0);
    CHECK(res.max_input_peak > CHANMAP_ABORT_INPUT);
    check_safe_end(&s);
    check_silence_written(&s);
    sim_free(&s);

    /* 9. feedback that decays too slowly: aborted as feedback */
    sim_config_default(&c);
    route(&c, 0, 0, 0.01); /* -80 dBFS, loop gain 0.97 -> about -50 dBFS */
    c.feedback = 0.97;
    c.fb_delay = 480;
    CHECK(run(&c, &s) == CHANMAP_ABORT_FEEDBACK);
    CHECK(res.steps_done == 0);
    check_safe_end(&s);
    check_silence_written(&s);
    sim_free(&s);

    /* 10. growing feedback: caught by the input level guard */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    c.feedback = 1.2;
    c.fb_delay = 480;
    CHECK(run(&c, &s) == CHANMAP_ABORT_INPUT_LEVEL);
    check_safe_end(&s);
    sim_free(&s);

    /* 11. SIGINT during step 3: signal stops within one period */
    sim_config_default(&c);
    route(&c, 3, 0, 1.0);
    c.raise_at = (long)(3u * CHANMAP_STEP_FRAMES + CHANMAP_PRE_FRAMES + CHANMAP_CHIRP_FRAMES / 2u);
    CHECK(run(&c, &s) == CHANMAP_ABORT_SIGNAL);
    CHECK(s.abort_frame == c.raise_at);
    CHECK(res.steps_done == 3);
    CHECK(s.nonzero_after < CHANMAP_PERIOD);
    check_safe_end(&s);
    check_silence_written(&s);
    CHECK(strstr(res.detail, "step 3") != NULL);
    sim_free(&s);

    /* 12. write error (underrun / unplug): no further writes, closed */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    c.fail_write_at = 200000;
    CHECK(run(&c, &s) == CHANMAP_ERR_IO);
    CHECK(s.written == 200000);
    check_safe_end(&s);
    sim_free(&s);

    /* 13. read error */
    sim_config_default(&c);
    c.fail_read_at = 5000;
    c.fail_errno = ENODEV;
    CHECK(run(&c, &s) == CHANMAP_ERR_IO);
    CHECK(strstr(res.detail, "No such device") != NULL);
    check_safe_end(&s);
    sim_free(&s);

    /* 14. mixer state changed by someone else during the run */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    c.mixer_change = 1;
    CHECK(run(&c, &s) == CHANMAP_ABORT_MIXER_CHANGED);
    CHECK(res.n_hits == 1);
    CHECK(s.closes == 1);
    sim_free(&s);

    /* 15. device busy (PipeWire still holds it) for 3 attempts, then free */
    sim_config_default(&c);
    route(&c, 0, 0, 1.0);
    c.busy_opens = 3;
    CHECK(run(&c, &s) == CHANMAP_OK);
    CHECK(s.opens == 4);
    sim_free(&s);

    /* 16. device stays busy: timeout, never opened */
    sim_config_default(&c);
    c.busy_opens = -1;
    CHECK(run(&c, &s) == CHANMAP_ERR_BUSY);
    CHECK(s.opens >= 10 && s.written == 0 && s.closes == 0);
    sim_free(&s);

    /* 17. other open error (e.g. wrong card info): no retry */
    sim_config_default(&c);
    c.open_error = -ENODEV;
    CHECK(run(&c, &s) == CHANMAP_ERR_OPEN);
    CHECK(s.opens == 1 && s.written == 0);
    sim_free(&s);

    /* 18. abort while waiting for the device */
    {
        struct chanmap_backend be;
        sim_config_default(&c);
        c.busy_opens = -1;
        sim_init(&s, &c);
        sim_backend(&s, &be);
        abort_flag = 1;
        CHECK(chanmap_open_with_reserve(&be, NULL, 5000, &abort_flag) == CHANMAP_ABORT_SIGNAL);
        abort_flag = 0;
        sim_free(&s);
    }

    /* 19. reservation process ends while the device is still busy */
    {
        struct chanmap_backend be;
        struct chanmap_reserve r;
        sim_config_default(&c);
        c.busy_opens = -1;
        sim_init(&s, &c);
        sim_backend(&s, &be);
        CHECK(chanmap_reserve_start(&r, "false", 1, NULL) == 0);
        CHECK(chanmap_open_with_reserve(&be, &r, 5000, &abort_flag) == CHANMAP_ERR_RESERVE);
        CHECK(r.pid == 0);
        sim_free(&s);
    }

    /* 20. polarity of a line output inverted by the cable */
    sim_config_default(&c);
    route(&c, 7, 0, -1.0);
    CHECK(run(&c, &s) == CHANMAP_OK);
    h = hit_for(7);
    CHECK(h && h->polarity == -1);
    sim_free(&s);

    TEST_EXIT();
}
