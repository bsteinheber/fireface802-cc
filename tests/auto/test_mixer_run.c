/*
 * Mixer initialisation flow (ff802_mixer_run) against the fake sysfs tree
 * and the in-memory MIDI backend of fake_midi.h.
 */
#include "auto_fixture.h"

#include "fake_midi.h"
#include "ff802/mixer_run.h"
#include "test_common.h"

#define PORT "3-2.4.1.1.3"

static void mkdirs(fixture *fx, const char *rel)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "mkdir -p -- '%s/%s'", fx->paths.sysfs_root, rel);
    if (system(cmd) != 0)
        exit(2);
}

static void card1(fixture *fx)
{
    mkdirs(fx, "bus/usb/devices/" PORT "/" PORT ":1.0/sound/card1/midiC1D0");
}

static ff802_sysfs_dev snapshot(fixture *fx)
{
    ff802_sysfs_dev d;
    if (ff802_sysfs_read(fx->paths.sysfs_root, PORT, &d) != FF802_SYSFS_OK)
        exit(2);
    return d;
}

static ff802_mixer_options fast(void)
{
    ff802_mixer_options o = { .timeout_ms = 1000, .poll_ms = 100, .gap_ms = 1 };
    return o;
}

static ff802_mixer_outcome run(fixture *fx, fake_midi *m, ff802_mixer_result *r)
{
    ff802_sysfs_dev d = snapshot(fx);
    ff802_midi_factory f = fake_midi_factory(m);
    ff802_mixer_options o = fast();
    return ff802_mixer_run(fx->paths.sysfs_root, &d, &f, &o, NULL, r);
}

static void test_done(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);
    fake_midi_init(&m, 1, 20);

    CHECK(run(&fx, &m, &r) == FF802_MIXER_DONE);
    CHECK(r.outcome == FF802_MIXER_DONE && r.written == 26 && r.card == 1);
    CHECK(r.client == 20 && r.port == 1);
    CHECK(strcmp(r.port_name, "Fireface 802 (24047128) Port 2") == 0);
    CHECK(m.n_open == 1 && m.n_close == 1 && m.n_connect == 1);
    CHECK(m.conn_client == 20 && m.conn_port == 1);
    CHECK(m.n_sent == 26);
    /* exactly the table, in order */
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < 26u; i++) {
        uint8_t want[12];
        CHECK(ff802_rme_sysex_reg(t[i].addr, t[i].value, want) == FF802_OK);
        CHECK_BYTES(m.sent[i], want, 12);
    }
    /* 1 ms between messages, no waiting for card or port */
    CHECK(m.n_sleep == 25 && m.slept_ms == 25);
    CHECK(strstr(r.detail, "26 registers sent to MIDI port 20:1") != NULL);
    CHECK(strcmp(ff802_mixer_outcome_str(r.outcome), "done") == 0);
    CHECK(!ff802_mixer_outcome_failed(r.outcome));

    /* a second run writes the same absolute values again (idempotent) */
    fake_midi m2;
    fake_midi_init(&m2, 1, 20);
    CHECK(run(&fx, &m2, &r) == FF802_MIXER_DONE);
    CHECK(m2.n_sent == 26 && memcmp(m.sent, m2.sent, sizeof m.sent) == 0);
    fx_done(&fx);
}

/* the sound card appears after 3 polls, the sequencer port after 2 lists */
static fixture *g_fx;
static void appear_on_third_sleep(fake_midi *m)
{
    if (m->n_sleep == 3)
        card1(g_fx);
}

static void test_waits(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19);
    fake_midi_init(&m, 1, 20);
    g_fx = &fx;
    m.on_sleep = appear_on_third_sleep;
    m.ports_after_lists = 2;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_DONE);
    CHECK(m.n_list == 3 && m.n_sent == 26);
    CHECK(m.slept_ms == 300 + 200 + 25);
    fx_done(&fx);
}

static void test_no_port(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;

    /* no sound card: timeout, backend never opened */
    fx_init(&fx); fx_ff802_cc(&fx, 19);
    fake_midi_init(&m, 1, 20);
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NO_PORT);
    CHECK(m.n_open == 0 && m.n_sent == 0 && m.slept_ms == 1000);
    CHECK(strstr(r.detail, "no sound card with MIDI device") != NULL);
    CHECK(strstr(r.detail, "nothing sent") != NULL);
    CHECK(ff802_mixer_outcome_failed(r.outcome));

    /* card, but no sequencer client of it */
    card1(&fx);
    fake_midi_init(&m, 2, 20);
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NO_PORT);
    CHECK(m.n_open == 1 && m.n_close == 1 && m.n_connect == 0 && m.n_sent == 0);
    CHECK(strstr(r.detail, "MIDI port of card 1 not available") != NULL);

    /* client without port 1 */
    fake_midi_init(&m, 1, 20);
    m.nports = 3;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NO_PORT && m.n_sent == 0);
    fx_done(&fx);
}

static void test_refused(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);

    /* name does not match the index */
    fake_midi_init(&m, 1, 20);
    snprintf(m.ports[3].name, sizeof m.ports[3].name, "Something else");
    CHECK(run(&fx, &m, &r) == FF802_MIXER_REFUSED);
    CHECK(m.n_connect == 0 && m.n_sent == 0 && m.n_close == 1);

    /* two kernel clients of the card */
    fake_midi_init(&m, 1, 20);
    m.ports[5] = (ff802_seq_port){ 21, 1, 1, 1, 1, "x Port 2" };
    m.nports = 6;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_REFUSED && m.n_sent == 0);

    /* two cards on the connection */
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":1.1/sound/card2/midiC2D0");
    fake_midi_init(&m, 1, 20);
    CHECK(run(&fx, &m, &r) == FF802_MIXER_REFUSED && m.n_open == 0);
    fx_done(&fx);
}

/* the connection changes between finding the port and connecting */
static int g_change;
static int list_then_change(void *self, ff802_seq_port *out, size_t max, size_t *n)
{
    int rc = fm_list(self, out, max, n);
    if (g_change == 1)
        fx_ff802_cc(g_fx, 20);                          /* reconnected */
    else if (g_change == 2)
        fx_ff802_prop(g_fx, 19);                        /* back in proprietary mode */
    else if (g_change == 3)
        fx_unplug(g_fx, PORT);
    return rc;
}
static int open_hooked(void *ctx, ff802_midi_backend *be)
{
    int rc = fm_open(ctx, be);
    be->list_ports = list_then_change;
    return rc;
}

static void test_connection_changed(void)
{
    for (g_change = 1; g_change <= 3; g_change++) {
        fixture fx; fake_midi m; ff802_mixer_result r;
        fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);
        g_fx = &fx;
        fake_midi_init(&m, 1, 20);
        ff802_sysfs_dev d = snapshot(&fx);
        ff802_midi_factory f = fake_midi_factory(&m);
        f.open = open_hooked;
        ff802_mixer_options o = fast();
        CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, &f, &o, NULL, &r) == FF802_MIXER_REFUSED);
        CHECK(m.n_connect == 0 && m.n_sent == 0 && m.n_close == 1);
        CHECK(strstr(r.detail, "nothing sent") != NULL);
        fx_done(&fx);
    }
}

static void test_errors(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);

    fake_midi_init(&m, 1, 20);
    m.open_err = -ENOENT;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NOT_SENT && m.n_close == 0);
    CHECK(strstr(r.detail, "cannot open the ALSA sequencer") != NULL);

    fake_midi_init(&m, 1, 20);
    m.list_err = -EIO;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NOT_SENT && m.n_close == 1);

    fake_midi_init(&m, 1, 20);
    m.connect_err = -EBUSY;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_NOT_SENT);
    CHECK(m.n_sent == 0 && m.n_close == 1);
    CHECK(strstr(r.detail, "cannot connect to MIDI port 20:1") != NULL);

    /* sending fails at message 7: stop, 6 written, no retry */
    fake_midi_init(&m, 1, 20);
    m.send_err_at = 7;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_INCOMPLETE);
    CHECK(r.written == 6 && m.n_sent == 6 && m.n_send_calls == 7 && m.n_close == 1);
    CHECK(strstr(r.detail, "register 7 of 26 (0x4124)") != NULL);
    CHECK(strstr(r.detail, "6 written, nothing retried") != NULL);

    /* the first message fails */
    fake_midi_init(&m, 1, 20);
    m.send_err_at = 1;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_INCOMPLETE && r.written == 0 && m.n_send_calls == 1);
    fx_done(&fx);
}

static void test_disabled_and_unavailable(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);

    /* writes not compiled in: port is found and reported, nothing else */
    fake_midi_init(&m, 1, 20);
    m.write_enabled = 0;
    CHECK(run(&fx, &m, &r) == FF802_MIXER_DISABLED_BUILD);
    CHECK(m.n_list == 1 && m.n_connect == 0 && m.n_send_calls == 0 && m.n_close == 1);
    CHECK(r.client == 20 && r.port == 1);
    CHECK(strstr(r.detail, "not compiled into this build") != NULL);

    /* no backend */
    ff802_sysfs_dev d = snapshot(&fx);
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, NULL, NULL, NULL, &r) == FF802_MIXER_UNAVAILABLE);
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, NULL, NULL, NULL, NULL, &r) == FF802_MIXER_REFUSED);
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, NULL, NULL, NULL, NULL) == FF802_MIXER_REFUSED);
    fx_done(&fx);
}

static void test_strings(void)
{
    static const char *const W[] = { "off", "done", "disabled-build", "unavailable",
                                     "no-port", "refused", "not-sent", "incomplete",
                                     "dry-run", "aborted" };
    for (int i = 0; i <= (int)FF802_MIXER_ABORTED; i++) {
        CHECK(strcmp(ff802_mixer_outcome_str((ff802_mixer_outcome)i), W[i]) == 0);
        CHECK(ff802_mixer_outcome_failed((ff802_mixer_outcome)i) == (i >= 2 && i <= 7));
    }
    ff802_mixer_options o;
    ff802_mixer_default_options(&o);
    CHECK(o.timeout_ms == 15000 && o.poll_ms == 100 && o.gap_ms == 10 && o.settle_ms == 200);
    CHECK(o.dry_run == 0 && o.confirm == NULL && o.confirm_user == NULL);
}

/* Dry run, confirmation and the pause before closing. */
static int g_confirm_calls;
static int confirm_no(void *u, const ff802_mixer_result *r)
{
    (void)u;
    g_confirm_calls++;
    return r->client == 20 && r->port == 1 ? 0 : 1;
}
static int confirm_yes(void *u, const ff802_mixer_result *r)
{
    fake_midi *m = u;
    g_confirm_calls++;
    /* nothing has been sent or connected when asked */
    return m->n_connect == 0 && m->n_send_calls == 0 && r->written == 0;
}

static void test_dry_run_confirm_settle(void)
{
    fixture fx; fake_midi m; ff802_mixer_result r;
    fx_init(&fx); fx_ff802_cc(&fx, 19); card1(&fx);
    ff802_sysfs_dev d = snapshot(&fx);

    /* dry run: port found and reported, no connect, works without writes */
    fake_midi_init(&m, 1, 20);
    m.write_enabled = 0;
    ff802_midi_factory f = fake_midi_factory(&m);
    ff802_mixer_options o = fast();
    o.dry_run = 1;
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, &f, &o, NULL, &r) == FF802_MIXER_DRY_RUN);
    CHECK(r.client == 20 && r.port == 1 && m.n_connect == 0 && m.n_send_calls == 0 && m.n_close == 1);
    CHECK(!ff802_mixer_outcome_failed(r.outcome));

    /* confirmation declined: nothing sent */
    fake_midi_init(&m, 1, 20);
    f = fake_midi_factory(&m);
    o = fast();
    o.confirm = confirm_no;
    g_confirm_calls = 0;
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, &f, &o, NULL, &r) == FF802_MIXER_ABORTED);
    CHECK(g_confirm_calls == 1 && m.n_connect == 0 && m.n_send_calls == 0 && m.n_close == 1);

    /* confirmation given before anything is sent; then 26 + settle */
    fake_midi_init(&m, 1, 20);
    f = fake_midi_factory(&m);
    o = fast();
    o.confirm = confirm_yes;
    o.confirm_user = &m;
    o.gap_ms = 10;
    o.settle_ms = 200;
    g_confirm_calls = 0;
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, &f, &o, NULL, &r) == FF802_MIXER_DONE);
    CHECK(g_confirm_calls == 1 && m.n_sent == 26 && m.n_close == 1);
    CHECK(m.n_sleep == 26 && m.slept_ms == 25u * 10u + 200u);

    /* a build without writes is refused before asking */
    fake_midi_init(&m, 1, 20);
    m.write_enabled = 0;
    f = fake_midi_factory(&m);
    o = fast();
    o.confirm = confirm_yes;
    o.confirm_user = &m;
    g_confirm_calls = 0;
    CHECK(ff802_mixer_run(fx.paths.sysfs_root, &d, &f, &o, NULL, &r) == FF802_MIXER_DISABLED_BUILD);
    CHECK(g_confirm_calls == 0);
    fx_done(&fx);
}

static void test_messages(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < 26u; i++) {
        uint8_t a[12], b[12];
        CHECK(ff802_mixer_init_message(i, a) == FF802_OK);
        CHECK(ff802_rme_sysex_reg(t[i].addr, t[i].value, b) == FF802_OK);
        CHECK_BYTES(a, b, 12);
    }
    uint8_t c[12];
    CHECK(ff802_mixer_init_message(26, c) == FF802_ERR_ADDR_RANGE);
}

int main(void)
{
    test_dry_run_confirm_settle();
    test_messages();
    test_done();
    test_waits();
    test_no_port();
    test_refused();
    test_connection_changed();
    test_errors();
    test_disabled_and_unavailable();
    test_strings();
    TEST_EXIT();
}
