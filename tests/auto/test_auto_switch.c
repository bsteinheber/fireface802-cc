/*
 * T4: the automatic switch flow (ff802_auto_handle) against the fake
 * sysfs tree and the simulated USB backend of tests/fake_usb.c.
 */
#include "auto_fixture.h"

#include "ff802/auto_switch.h"
#include "fake_midi.h"
#include "fake_usb.h"
#include "test_common.h"

static const uint8_t ENABLE[4] = { 0x01, 0x00, 0x44, 0xBD };
#define PORT "3-2.4.1.1.3"

/* Backend factory around fake_usb; hooks run inside open(). */
typedef struct factory_ctx {
    fake_usb fake;
    int      n_create, n_destroy;
    ff802_usb_err create_err;
    fixture *fx;
    void   (*on_open)(struct factory_ctx *c);
    ff802_usb_err (*orig_open)(void *, const ff802_usb_dev *, ff802_usb_handle *);
} factory_ctx;

static factory_ctx *g_ctx;

static ff802_usb_err hooked_open(void *self, const ff802_usb_dev *dev,
                                 ff802_usb_handle *h)
{
    ff802_usb_err e = g_ctx->orig_open(self, dev, h);
    if (e == FF802_USB_OK && g_ctx->on_open != NULL)
        g_ctx->on_open(g_ctx);
    return e;
}

static ff802_usb_err f_create(void *ctx, ff802_usb_backend *be)
{
    factory_ctx *c = ctx;
    c->n_create++;
    if (c->create_err != FF802_USB_OK)
        return c->create_err;
    *be = fake_usb_backend(&c->fake);
    c->orig_open = be->open;
    be->open = hooked_open;
    g_ctx = c;
    return FF802_USB_OK;
}

static void f_destroy(void *ctx, ff802_usb_backend *be)
{
    factory_ctx *c = ctx;
    (void)be;
    c->n_destroy++;
}

static void ctx_init(factory_ctx *c, fixture *fx, unsigned devnum)
{
    memset(c, 0, sizeof *c);
    fake_usb_init(&c->fake);
    c->fx = fx;
    /* the libusb view of the same device */
    c->fake.devs[0] = (ff802_usb_dev){ .bus = 3, .address = (uint8_t)devnum,
                                       .vid = 0x2A39, .pid = 0x3FCD,
                                       .bcd_device = 1, .port_count = 5,
                                       .ports = { 2, 4, 1, 1, 3 } };
}

static ff802_auto_outcome run(fixture *fx, factory_ctx *c, unsigned devnum,
                              ff802_auto_result *res)
{
    ff802_auto_event ev;
    char inst[64];
    snprintf(inst, sizeof inst, PORT ":003:%03u", devnum);
    if (ff802_auto_parse_instance(inst, &ev) != 0)
        return FF802_AUTO_ERROR;
    ff802_auto_backend_factory f = { .ctx = c, .open = f_create, .close = f_destroy };
    return ff802_auto_handle(&fx->paths, &ev, &f, NULL, res);
}

static int event_kind(fixture *fx, const char *kind)
{
    char buf[512], want[64];
    fx_read(fx, "run/ff802-cc-auto/event", buf, sizeof buf);
    snprintf(want, sizeof want, "\nkind=%s\n", kind);
    return strstr(buf, want) != NULL;
}

static void test_parse_instance(void)
{
    ff802_auto_event ev;
    CHECK(ff802_auto_parse_instance("3-2.4.1.1.3:003:019", &ev) == 0);
    CHECK(strcmp(ev.kernel_name, "3-2.4.1.1.3") == 0 && ev.bus == 3 && ev.devnum == 19);
    CHECK(ff802_auto_parse_instance("3-2:3:19", &ev) == 0);
    CHECK(ff802_auto_parse_instance("3-2:004:019", &ev) != 0);   /* bus mismatch */
    CHECK(ff802_auto_parse_instance("3-2:003", &ev) != 0);
    CHECK(ff802_auto_parse_instance("3-2:003:019:1", &ev) != 0);
    CHECK(ff802_auto_parse_instance("3-2:003:000", &ev) != 0);
    CHECK(ff802_auto_parse_instance("3-2:003:128", &ev) != 0);
    CHECK(ff802_auto_parse_instance("3-2:003:0019", &ev) != 0);
    CHECK(ff802_auto_parse_instance("../x:003:019", &ev) != 0);
    CHECK(ff802_auto_parse_instance(":003:019", &ev) != 0);
    CHECK(ff802_auto_parse_instance("3-2:3a:19", &ev) != 0);
    CHECK(ff802_auto_parse_instance(NULL, &ev) != 0);
}


static void test_parse_port_instance(void)
{
    ff802_auto_event ev;
    CHECK(ff802_auto_parse_port_instance("3-2.4.1.1.3", &ev) == 0);
    CHECK(strcmp(ev.kernel_name, "3-2.4.1.1.3") == 0 && ev.bus == 0 && ev.devnum == 0);
    CHECK(ff802_auto_parse_port_instance("3-10", &ev) == 0);
    CHECK(ff802_auto_parse_port_instance("3-2:003:019", &ev) != 0);
    CHECK(ff802_auto_parse_port_instance("usb3", &ev) != 0);
    CHECK(ff802_auto_parse_port_instance("3-2/../x", &ev) != 0);
    CHECK(ff802_auto_parse_port_instance("3-2.4:1.0", &ev) != 0);
    CHECK(ff802_auto_parse_port_instance("", &ev) != 0);
    CHECK(ff802_auto_parse_port_instance(NULL, &ev) != 0);
}

static void test_reset_outcomes(void)
{
    CHECK(strcmp(ff802_auto_outcome_str(FF802_AUTO_SWITCHED), "switched") == 0);
    CHECK(strcmp(ff802_auto_outcome_str(FF802_AUTO_RESET_FAILED), "reset-failed") == 0);
    CHECK(strcmp(ff802_auto_outcome_str(FF802_AUTO_NOT_REENUMERATED), "not-reenumerated") == 0);
    CHECK(strcmp(ff802_auto_outcome_str(FF802_AUTO_REENUMERATED_NOT_CC),
                 "reenumerated-not-cc") == 0);
    CHECK(ff802_auto_exit_code(FF802_AUTO_SWITCHED) == 0);
    CHECK(ff802_auto_exit_code(FF802_AUTO_SENT) == 0);
    CHECK(ff802_auto_exit_code(FF802_AUTO_RESET_FAILED) == 5);
    CHECK(ff802_auto_exit_code(FF802_AUTO_NOT_REENUMERATED) == 5);
    CHECK(ff802_auto_exit_code(FF802_AUTO_REENUMERATED_NOT_CC) == 5);
    CHECK(strcmp(ff802_auto_event_kind_str(FF802_EVENT_RESET_FAILED), "reset-failed") == 0);
}
/* 1: already in CC mode -> nothing, not even the USB backend */
static void test_cc_mode(void)
{
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    fx_ff802_cc(&fx, 19);
    ctx_init(&c, &fx, 19);
    CHECK(run(&fx, &c, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(c.n_create == 0 && c.fake.n_write == 0);
    CHECK(r.event_seq == 0 && !fx_exists(&fx, "run/ff802-cc-auto/event"));   /* nothing pending */
    fx_done(&fx);
}

/* 2, 3, 6, 10: proprietary -> one transfer; same connection again ->
 * nothing; reconnect in CC mode -> confirmation once */
static void test_enable_and_reconnect(void)
{
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_SENT);
    CHECK(c.n_create == 1 && c.n_destroy == 1);
    CHECK(c.fake.n_write == 1 && c.fake.write_ep == 0x0C && c.fake.write_len == 4);
    CHECK_BYTES(c.fake.write_data, ENABLE, 4);
    CHECK(c.fake.opened_dev.address == 18 && c.fake.claimed == 0 && c.fake.open_handles == 0);
    CHECK(strcmp(c.fake.trace, "list,free,layout,open,kdrv,claim,write,release,close") == 0);
    CHECK(r.event_seq == 1 && event_kind(&fx, "prepared"));
    CHECK(ff802_auto_pending_for(&fx.paths, PORT));
    CHECK(ff802_auto_exit_code(r.outcome) == 0);

    /* repeated event for the same connection (udevadm trigger, change) */
    for (int i = 0; i < 3; i++) {
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_ALREADY_HANDLED);
        CHECK(r.event_seq == 0);
    }
    CHECK(c.n_create == 1 && c.fake.n_write == 1);

    /* the user reconnects: CC mode, confirmation exactly once */
    fx_ff802_cc(&fx, 19);
    CHECK(run(&fx, &c, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(r.event_seq == 2 && event_kind(&fx, "cc-active"));
    CHECK(!ff802_auto_pending_for(&fx.paths, PORT));
    CHECK(run(&fx, &c, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(r.event_seq == 0);
    CHECK(c.n_create == 1 && c.fake.n_write == 1);
    fx_done(&fx);
}

/* 4: transfer fails -> state unknown, no retry, not on the next event */
static void test_write_failures(void)
{
    static const ff802_usb_err errs[] = { FF802_USB_ERR_TIMEOUT, FF802_USB_ERR_PIPE,
                                          FF802_USB_ERR_IO, FF802_USB_ERR_NO_DEVICE };
    for (size_t i = 0; i < sizeof errs / sizeof errs[0]; i++) {
        fixture fx; factory_ctx c; ff802_auto_result r;
        fx_init(&fx);
        fx_mode(&fx, "on\n");
        fx_ff802_prop(&fx, 18);
        ctx_init(&c, &fx, 18);
        c.fake.err_write = errs[i];
        c.fake.write_transferred = 0;
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_WRITE_FAILED);
        CHECK(c.fake.n_write == 1 && c.fake.claimed == 0 && c.fake.open_handles == 0);
        CHECK(event_kind(&fx, "failed") && strstr(r.detail, "state unknown") != NULL);
        CHECK(!ff802_auto_pending_for(&fx.paths, PORT));
        CHECK(ff802_auto_exit_code(r.outcome) == FF802_AUTO_EXIT_WRITE_FAILED);
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_ALREADY_HANDLED);
        CHECK(c.fake.n_write == 1);
        fx_done(&fx);
    }
    /* short write */
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18);
    c.fake.write_transferred = 2;
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_WRITE_FAILED);
    CHECK(r.sw.status == FF802_SW_SHORT_WRITE && c.fake.n_write == 1);
    fx_done(&fx);
}

/* failures before the write: nothing sent, one event, no second attempt */
static void test_not_sent(void)
{
    for (int k = 0; k < 6; k++) {
        fixture fx; factory_ctx c; ff802_auto_result r;
        fx_init(&fx);
        fx_mode(&fx, "on\n");
        fx_ff802_prop(&fx, 18);
        ctx_init(&c, &fx, 18);
        switch (k) {
        case 0: c.fake.kernel_driver_active = 1; break;
        case 1: c.fake.layout.eps[1].address = 0x0D; break;
        case 2: c.fake.err_open = FF802_USB_ERR_ACCESS; break;
        case 3: c.fake.err_claim = FF802_USB_ERR_BUSY; break;
        case 4: c.fake.write_enabled = 0; break;      /* build without writes */
        case 5: c.create_err = FF802_USB_ERR_OTHER; break;
        }
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_NOT_SENT);
        CHECK(c.fake.n_write == 0 && c.fake.claimed == 0 && c.fake.open_handles == 0);
        CHECK(event_kind(&fx, "failed") && strstr(r.detail, "nothing sent") != NULL);
        CHECK(ff802_auto_exit_code(r.outcome) == FF802_AUTO_EXIT_NOT_SENT);
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_ALREADY_HANDLED);
        CHECK(c.fake.n_write == 0);
        fx_done(&fx);
    }
}

static void hook_replug(factory_ctx *c) { fx_ff802_prop(c->fx, 19); }
static void hook_unplug(factory_ctx *c) { fx_unplug(c->fx, PORT); }
static void hook_cc(factory_ctx *c) { fx_ff802_cc(c->fx, 18); }
static void hook_off(factory_ctx *c) { fx_mode(c->fx, "off\n"); }

/* 5, 8: device removed or changed between detection and transfer */
static void test_device_changes(void)
{
    void (*hooks[])(factory_ctx *) = { hook_replug, hook_unplug, hook_cc, hook_off };
    const char *why[] = { "device changed before the write", "disappeared",
                          "device changed before the write", "switched off" };
    for (size_t i = 0; i < 4; i++) {
        fixture fx; factory_ctx c; ff802_auto_result r;
        fx_init(&fx);
        fx_mode(&fx, "on\n");
        fx_ff802_prop(&fx, 18);
        ctx_init(&c, &fx, 18);
        c.on_open = hooks[i];
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED);
        CHECK(c.fake.n_open == 1 && c.fake.n_claim == 0 && c.fake.n_write == 0);
        CHECK(c.fake.open_handles == 0);
        CHECK(strstr(r.detail, why[i]) != NULL);
        fx_done(&fx);
    }

    fixture fx; factory_ctx c; ff802_auto_result r;
    /* unplugged before the service ran */
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    ctx_init(&c, &fx, 18);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED && c.n_create == 0);
    /* reconnected before the service ran: event devnum 18, now 19 */
    fx_ff802_prop(&fx, 19);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED && c.n_create == 0);
    CHECK(strstr(r.detail, "reconnected since the event") != NULL);
    /* libusb sees a different port for this bus/address */
    c.fake.devs[0].address = 19;
    c.fake.devs[0].ports[4] = 4;
    CHECK(run(&fx, &c, 19, &r) == FF802_AUTO_REFUSED);
    CHECK(c.fake.n_write == 0 && strstr(r.detail, "not the connection") != NULL);
    fx_done(&fx);

    /* the device is removed during the transfer */
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18);
    c.fake.err_write = FF802_USB_ERR_NO_DEVICE;
    c.fake.write_transferred = 0;
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_WRITE_FAILED);
    CHECK(r.sw.status == FF802_SW_DISCONNECTED && c.fake.n_write == 1);
    fx_done(&fx);
}

/* 7, 8: unknown device, right ID but not identifiable as an original 802 */
static void test_identity(void)
{
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    ctx_init(&c, &fx, 18);
    fx_dev(&fx, PORT, "0424", "2514", 3, 18, "480", "Microchip", "USB2514B");
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED);
    CHECK(c.n_create == 0 && !fx_exists(&fx, "run/ff802-cc-auto/event"));
    fx_dev(&fx, PORT, "2a39", "3ffc", 3, 18, "480", "RME", "Fireface 802 FS (1)");
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED && c.n_create == 0);
    fx_dev(&fx, PORT, "0424", "3fdd", 3, 18, "480", "Microchip", "Fireface 802");
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_REFUSED && c.n_create == 0);

    const char *products[] = { "Fireface 802 FS (24047128)", NULL };
    for (int i = 0; i < 2; i++) {
        fx_dev(&fx, PORT, "2a39", "3fcd", 3, (unsigned)(20 + i), "480", "RME", products[i]);
        CHECK(run(&fx, &c, (unsigned)(20 + i), &r) == FF802_AUTO_REFUSED);
        CHECK(c.n_create == 0 && event_kind(&fx, "failed"));
        CHECK(run(&fx, &c, (unsigned)(20 + i), &r) == FF802_AUTO_ALREADY_HANDLED);
    }
    fx_dev(&fx, PORT, "2a39", "3fcd", 3, 22, "12", "RME", "Fireface 802");
    CHECK(run(&fx, &c, 22, &r) == FF802_AUTO_REFUSED && c.n_create == 0);
    fx_dev(&fx, PORT, "2a39", "3fcd", 3, 23, "480", "rme", "Fireface 802");
    CHECK(run(&fx, &c, 23, &r) == FF802_AUTO_REFUSED && c.n_create == 0);
    CHECK(c.fake.n_write == 0);
    fx_done(&fx);
}

/* 11, 12: automation off (default, explicit, invalid, after a reboot) */
static void test_off(void)
{
    const char *modes[] = { NULL, "off\n", "ON\n", "on \n", "1\n" };
    for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        fixture fx; factory_ctx c; ff802_auto_result r;
        fx_init(&fx);
        fx_mode(&fx, modes[i]);
        fx_ff802_prop(&fx, 18);
        ctx_init(&c, &fx, 18);
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_OFF);
        CHECK(c.n_create == 0 && c.fake.n_write == 0 && r.event_seq == 0);
        CHECK(!fx_exists(&fx, "run/ff802-cc-auto/attempts/" PORT "_3_18"));
        /* reboot: /run is empty, the off state stays */
        fx_rm_rf(fx.paths.run_dir);
        CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_OFF && c.n_create == 0);
        fx_done(&fx);
    }
    /* off -> on later: the same connection can still be handled once */
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_OFF);
    fx_mode(&fx, "on\n");
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_SENT && c.fake.n_write == 1);
    fx_done(&fx);
}

/* a reboot with the device still proprietary (not reconnected): one new attempt */
static void test_reboot_on(void)
{
    fixture fx; factory_ctx c; ff802_auto_result r;
    fx_init(&fx);
    fx_mode(&fx, "on\n");
    fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_SENT);
    fx_rm_rf(fx.paths.run_dir);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_SENT);
    CHECK(run(&fx, &c, 18, &r) == FF802_AUTO_ALREADY_HANDLED);
    CHECK(c.fake.n_write == 2);
    fx_done(&fx);
}

/* Mixer initialisation in the CC branch (fake MIDI backend). */
static void cc_card(fixture *fx)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "mkdir -p -- '%s/bus/usb/devices/" PORT "/" PORT
             ":1.0/sound/card1/midiC1D0'", fx->paths.sysfs_root);
    if (system(cmd) != 0)
        exit(2);
}

static ff802_auto_outcome run_mixer(fixture *fx, factory_ctx *c, fake_midi *m,
                                    unsigned devnum, ff802_auto_result *res)
{
    ff802_auto_event ev;
    char inst[64];
    snprintf(inst, sizeof inst, PORT ":003:%03u", devnum);
    if (ff802_auto_parse_instance(inst, &ev) != 0)
        return FF802_AUTO_ERROR;
    ff802_midi_factory mf;
    memset(&mf, 0, sizeof mf);
    if (m != NULL)
        mf = fake_midi_factory(m);
    ff802_mixer_options o = { .timeout_ms = 200, .poll_ms = 100, .gap_ms = 0 };
    ff802_auto_backend_factory f = { .ctx = c, .open = f_create, .close = f_destroy,
                                     .midi = m != NULL ? &mf : NULL, .mixer_opt = &o };
    return ff802_auto_handle(&fx->paths, &ev, &f, NULL, res);
}

static void mixer_file(fixture *fx, const char *content)
{
    char path[256];
    snprintf(path, sizeof path, "%s/mixer-init", fx->paths.state_dir);
    fx_write(path, content);
}

static void test_mixer_in_cc_branch(void)
{
    fixture fx; factory_ctx c; ff802_auto_result r; fake_midi m;
    char buf[640];

    /* off (default): nothing, not even the MIDI backend */
    fx_init(&fx); fx_ff802_cc(&fx, 19); cc_card(&fx);
    ctx_init(&c, &fx, 19); fake_midi_init(&m, 1, 20);
    CHECK(run_mixer(&fx, &c, &m, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(!r.mixer_ran && r.mixer.outcome == FF802_MIXER_OFF && m.n_open == 0);
    CHECK(ff802_auto_result_exit_code(&r) == 0);
    CHECK(!fx_exists(&fx, "run/ff802-cc-auto/mixer-result"));

    /* on, automation off: done, independent of the mode file */
    mixer_file(&fx, "on\n");
    CHECK(run_mixer(&fx, &c, &m, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(r.mixer_ran && r.mixer.outcome == FF802_MIXER_DONE && r.mixer.written == 38);
    CHECK(m.n_sent == 38 && c.n_create == 0 && ff802_auto_result_exit_code(&r) == 0);
    CHECK(r.mixer_event_seq == 0 && !fx_exists(&fx, "run/ff802-cc-auto/mixer-event"));
    fx_read(&fx, "run/ff802-cc-auto/mixer-result", buf, sizeof buf);
    CHECK(strstr(buf, "outcome=done\n") != NULL && strstr(buf, "written=38/38\n") != NULL);
    CHECK(strstr(buf, "written=38/38\nconfig=ff802-mixer-init-v3\nentries=38\n"
                      "checksum=0xEC858A44\nbase=ff802-mixer-init-v2\nbase_entries=26\n"
                      "base_checksum=0xC97A359C\n") != NULL);
    CHECK(strstr(buf, "port=" PORT "\nbusnum=3\ndevnum=19\n") != NULL);
    CHECK(strstr(buf, "midi_port=20:1 Fireface 802 (24047128) Port 2\n") != NULL);
    fx_read(&fx, "run/ff802-cc-auto/last-result", buf, sizeof buf);
    CHECK(strstr(buf, "outcome=cc-active\n") != NULL);

    /* failure while sending: exit 4, mixer-failed event, cc-active kept */
    fake_midi_init(&m, 1, 20);
    m.send_err_at = 3;
    CHECK(run_mixer(&fx, &c, &m, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(r.mixer.outcome == FF802_MIXER_INCOMPLETE && ff802_auto_result_exit_code(&r) == 4);
    CHECK(r.mixer_event_seq >= 1);
    fx_read(&fx, "run/ff802-cc-auto/mixer-event", buf, sizeof buf);
    CHECK(strstr(buf, "kind=mixer-failed\n") != NULL && strstr(buf, "(2 of 38 registers)") != NULL);

    /* no MIDI backend in the program: exit 3 */
    CHECK(run_mixer(&fx, &c, NULL, 19, &r) == FF802_AUTO_CC_ACTIVE);
    CHECK(r.mixer.outcome == FF802_MIXER_UNAVAILABLE && ff802_auto_result_exit_code(&r) == 3);
    fx_done(&fx);

    /* proprietary mode with mixer-init on: only the switch, MIDI untouched */
    fx_init(&fx); fx_mode(&fx, "on\n"); mixer_file(&fx, "on\n"); fx_ff802_prop(&fx, 18);
    ctx_init(&c, &fx, 18); fake_midi_init(&m, 1, 20);
    CHECK(run_mixer(&fx, &c, &m, 18, &r) == FF802_AUTO_SENT);
    CHECK(!r.mixer_ran && m.n_open == 0 && c.fake.n_write == 1);
    fx_done(&fx);
}

int main(void)
{
    test_parse_instance();
    test_parse_port_instance();
    test_reset_outcomes();
    test_cc_mode();
    test_mixer_in_cc_branch();
    test_enable_and_reconnect();
    test_write_failures();
    test_not_sent();
    test_device_changes();
    test_identity();
    test_off();
    test_reboot_on();
    TEST_EXIT();
}
