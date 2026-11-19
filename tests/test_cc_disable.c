/*
 * ff802-cc disable sequence (cc_disable.c) against the simulated backend:
 * order of the calls, exactly one write of the tested 16 bytes to bulk OUT
 * 0x07 with 3000 ms, claim only with kernel driver auto-detach, every
 * failure, and that the enable sequence never uses the detach claim.
 */
#include "ff802/cc_disable.h"
#include "fake_usb.h"
#include "test_common.h"

static const uint8_t TESTED[16] = {
    0x13, 0xF0, 0x00, 0x20, 0x13, 0x0D, 0x10, 0x00,
    0x13, 0x7E, 0x7F, 0x13, 0x13, 0x6A, 0x0B, 0xF7
};

typedef struct confirm_state {
    int answer;
    int calls;
    uint8_t data[32];
    size_t len;
    ff802_usb_dev target;
} confirm_state;

static int confirm_cb(void *user, const ff802_usb_dev *target,
                      const uint8_t *data, size_t len)
{
    confirm_state *c = user;
    c->calls++;
    c->len = len;
    if (len <= sizeof c->data)
        memcpy(c->data, data, len);
    c->target = *target;
    return c->answer;
}

/* One healthy Fireface 802 in CC mode, interface 3 as on the host. */
static void fake_cc(fake_usb *f)
{
    fake_usb_init(f);
    f->devs[0] = (ff802_usb_dev){ .bus = 3, .address = 19, .vid = 0x0424, .pid = 0x3FDD,
                                  .bcd_device = 1, .port_count = 5, .ports = { 2, 4, 1, 1, 3 } };
    memset(&f->layout, 0, sizeof f->layout);
    f->layout.config_value = 1;
    f->layout.has_interface = 1;
    f->layout.num_endpoints = 2;
    f->layout.eps[0] = (ff802_ep_info){ .address = 0x07, .attributes = 0x02, .max_packet = 512 };
    f->layout.eps[1] = (ff802_ep_info){ .address = 0x86, .attributes = 0x02, .max_packet = 512 };
    f->kernel_driver_active = 1;           /* snd-usb-audio */
    f->write_transferred = 16;
}

static ff802_sw_status run(fake_usb *f, confirm_state *c, ff802_sw_result *r)
{
    ff802_usb_backend be = fake_usb_backend(f);
    ff802_dis_options o = { .confirm = confirm_cb, .confirm_user = c };
    return ff802_disable_run(&be, &o, NULL, r);
}

static void test_success(void)
{
    fake_usb f; confirm_state c = { .answer = 1 }; ff802_sw_result r;
    fake_cc(&f);
    CHECK(run(&f, &c, &r) == FF802_SW_OK);
    CHECK(strcmp(f.trace, "list,free,layout,open,kdrv,claim_detach,write,release,close") == 0);
    CHECK(f.layout_iface == 3 && f.layout_alt == 0 && f.last_iface == 3);
    CHECK(f.n_write == 1 && f.write_ep == 0x07 && f.write_len == 16 && f.write_timeout == 3000);
    CHECK_BYTES(f.write_data, TESTED, 16);
    CHECK(f.n_claim == 0 && f.n_claim_detach == 1);      /* never a plain claim */
    CHECK(f.claimed == 0 && f.open_handles == 0);
    CHECK(c.calls == 1 && c.len == 16 && c.target.address == 19);
    CHECK_BYTES(c.data, TESTED, 16);
    CHECK(r.transferred == 16 && !r.release_failed && r.failed_step == FF802_STEP_NONE);
    CHECK(ff802_sw_exit_code(r.status) == FF802_CC_EXIT_OK);

    /* no kernel driver bound: same sequence */
    fake_cc(&f);
    f.kernel_driver_active = 0;
    c.calls = 0;
    CHECK(run(&f, &c, &r) == FF802_SW_OK && f.n_write == 1);

    /* release fails because the unit has already switched: still success */
    fake_cc(&f);
    f.err_release = FF802_USB_ERR_NO_DEVICE;
    CHECK(run(&f, &c, &r) == FF802_SW_OK);
    CHECK(r.release_failed && r.release_err == FF802_USB_ERR_NO_DEVICE && f.open_handles == 0);
}

static void test_not_sent(void)
{
    fake_usb f; ff802_sw_result r;

    confirm_state no = { .answer = 0 };
    fake_cc(&f);
    CHECK(run(&f, &no, &r) == FF802_SW_ABORTED);
    CHECK(strcmp(f.trace, "list,free,layout,open,kdrv,close") == 0);
    CHECK(no.calls == 1 && f.n_claim_detach == 0 && f.n_write == 0);
    CHECK(ff802_sw_exit_code(r.status) == FF802_CC_EXIT_NOT_SENT);

    /* no confirmation callback */
    fake_cc(&f);
    ff802_usb_backend be = fake_usb_backend(&f);
    ff802_dis_options o = { 0 };
    CHECK(ff802_disable_run(&be, &o, NULL, &r) == FF802_SW_ABORTED && f.n_write == 0);

    /* write gate closed: no confirmation, no claim */
    confirm_state yes = { .answer = 1 };
    fake_cc(&f);
    f.write_enabled = 0;
    CHECK(run(&f, &yes, &r) == FF802_SW_WRITE_DISABLED);
    CHECK(yes.calls == 0 && f.n_claim_detach == 0 && f.n_write == 0 && f.open_handles == 0);

    /* backend without the detach claim (e.g. ff802-cc-autoswitch's) */
    fake_cc(&f);
    be = fake_usb_backend(&f);
    be.claim_detach = NULL;
    o = (ff802_dis_options){ .confirm = confirm_cb, .confirm_user = &yes };
    CHECK(ff802_disable_run(&be, &o, NULL, &r) == FF802_SW_INTERNAL);
    CHECK(r.failed_step == FF802_STEP_CLAIM && f.n_claim == 0 && f.n_write == 0);
    CHECK(f.open_handles == 0);

    /* claim fails */
    static const ff802_usb_err ce[] = { FF802_USB_ERR_BUSY, FF802_USB_ERR_ACCESS,
                                        FF802_USB_ERR_NO_DEVICE, FF802_USB_ERR_IO };
    static const ff802_sw_status cs[] = { FF802_SW_INTERFACE_BUSY, FF802_SW_ACCESS_DENIED,
                                          FF802_SW_DISCONNECTED, FF802_SW_CLAIM_FAILED };
    for (int i = 0; i < 4; i++) {
        fake_cc(&f);
        f.err_claim_detach = ce[i];
        CHECK(run(&f, &yes, &r) == cs[i]);
        CHECK(f.n_write == 0 && f.n_release == 0 && f.open_handles == 0);
    }
}

static void test_write_errors(void)
{
    static const ff802_usb_err we[] = { FF802_USB_ERR_TIMEOUT, FF802_USB_ERR_PIPE,
                                        FF802_USB_ERR_NO_DEVICE, FF802_USB_ERR_IO };
    static const ff802_sw_status ws[] = { FF802_SW_TIMEOUT, FF802_SW_STALL,
                                          FF802_SW_DISCONNECTED, FF802_SW_WRITE_FAILED };
    fake_usb f; confirm_state c = { .answer = 1 }; ff802_sw_result r;
    for (int i = 0; i < 4; i++) {
        fake_cc(&f);
        f.err_write = we[i];
        f.write_transferred = 0;
        CHECK(run(&f, &c, &r) == ws[i]);
        CHECK(f.n_write == 1 && f.n_release == 1 && f.claimed == 0 && f.open_handles == 0);
        CHECK(r.write_attempted && r.failed_step == FF802_STEP_WRITE);
    }
    /* short transfers, also with a timeout */
    fake_cc(&f);
    f.write_transferred = 8;
    CHECK(run(&f, &c, &r) == FF802_SW_SHORT_WRITE && f.n_write == 1);
    fake_cc(&f);
    f.err_write = FF802_USB_ERR_TIMEOUT;
    f.write_transferred = 4;
    CHECK(run(&f, &c, &r) == FF802_SW_SHORT_WRITE && f.n_write == 1);
    CHECK(ff802_sw_exit_code(r.status) == FF802_CC_EXIT_WRITE_FAILED);
}

static void test_selection_and_layout(void)
{
    fake_usb f; confirm_state c = { .answer = 1 }; ff802_sw_result r;

    /* only a unit in proprietary mode: not found, nothing opened */
    fake_usb_init(&f);
    CHECK(run(&f, &c, &r) == FF802_SW_NOT_FOUND && f.n_open == 0);
    /* two units in CC mode */
    fake_cc(&f);
    f.devs[1] = f.devs[0];
    f.devs[1].address = 20;
    CHECK(run(&f, &c, &r) == FF802_SW_AMBIGUOUS && f.n_open == 0);
    /* ... one selected by location */
    ff802_usb_backend be = fake_usb_backend(&f);
    ff802_dis_options o = { .select_location = 1, .bus = 3, .address = 20,
                            .confirm = confirm_cb, .confirm_user = &c };
    CHECK(ff802_disable_run(&be, &o, NULL, &r) == FF802_SW_OK && f.opened_dev.address == 20);
    /* another 0424 device is not a Fireface */
    fake_cc(&f);
    f.devs[0].pid = 0x2514;
    CHECK(run(&f, &c, &r) == FF802_SW_NOT_FOUND);

    /* descriptor of interface 3 */
    fake_cc(&f); f.layout.has_interface = 0;
    CHECK(run(&f, &c, &r) == FF802_SW_UNEXPECTED_LAYOUT && r.layout == FF802_LAYOUT_NO_INTERFACE && f.n_open == 0);
    fake_cc(&f); f.layout.eps[0].address = 0x0C;
    CHECK(run(&f, &c, &r) == FF802_SW_UNEXPECTED_LAYOUT && r.layout == FF802_LAYOUT_NO_ENDPOINT && f.n_open == 0);
    fake_cc(&f); f.layout.eps[0].attributes = 0x03;
    CHECK(run(&f, &c, &r) == FF802_SW_UNEXPECTED_LAYOUT && r.layout == FF802_LAYOUT_NOT_BULK);
    fake_cc(&f); f.layout.eps[0].max_packet = 64;
    CHECK(run(&f, &c, &r) == FF802_SW_UNEXPECTED_LAYOUT && r.layout == FF802_LAYOUT_MAX_PACKET);
    fake_cc(&f); f.layout.config_value = 0;
    CHECK(run(&f, &c, &r) == FF802_SW_UNEXPECTED_LAYOUT && r.layout == FF802_LAYOUT_WRONG_CONFIG);
    CHECK(f.n_write == 0);

    /* enumeration, descriptor, open and driver query errors */
    fake_cc(&f); f.err_list = FF802_USB_ERR_IO;
    CHECK(run(&f, &c, &r) == FF802_SW_LIST_FAILED);
    fake_cc(&f); f.err_layout = FF802_USB_ERR_IO;
    CHECK(run(&f, &c, &r) == FF802_SW_LAYOUT_FAILED && f.n_open == 0);
    fake_cc(&f); f.err_open = FF802_USB_ERR_ACCESS;
    CHECK(run(&f, &c, &r) == FF802_SW_ACCESS_DENIED && f.n_close == 0);
    fake_cc(&f); f.err_kdrv = FF802_USB_ERR_IO;
    CHECK(run(&f, &c, &r) == FF802_SW_DRIVER_CHECK_FAILED && f.n_write == 0 && f.open_handles == 0);
}

/* The enable sequence is unchanged and never uses the detach claim. */
static int enable_confirm(void *user, const ff802_usb_dev *t, const uint8_t cmd[4])
{
    (void)user; (void)t; (void)cmd;
    return 1;
}

static void test_enable_untouched(void)
{
    fake_usb f;
    ff802_sw_result r;
    fake_usb_init(&f);
    ff802_usb_backend be = fake_usb_backend(&f);
    ff802_sw_options o = { .action = FF802_SW_ENABLE_CC, .confirm = enable_confirm };
    CHECK(ff802_switch_run(&be, &o, NULL, &r) == FF802_SW_OK);
    CHECK(strcmp(f.trace, "list,free,layout,open,kdrv,claim,write,release,close") == 0);
    CHECK(f.n_claim_detach == 0 && f.n_claim == 1 && f.write_ep == 0x0C && f.write_len == 4);
    CHECK(f.write_timeout == 1000);
}

static void test_texts(void)
{
    CHECK(ff802_disable_confirm_answer_ok("DISABLE"));
    CHECK(ff802_disable_confirm_answer_ok("DISABLE\n"));
    CHECK(ff802_disable_confirm_answer_ok("DISABLE\r\n"));
    CHECK(!ff802_disable_confirm_answer_ok("disable\n"));
    CHECK(!ff802_disable_confirm_answer_ok("ENABLE\n"));
    CHECK(!ff802_disable_confirm_answer_ok("DISABLE now\n"));
    CHECK(!ff802_disable_confirm_answer_ok(""));
    CHECK(!ff802_disable_confirm_answer_ok(NULL));
    CHECK(strstr(ff802_disable_status_str(FF802_SW_NOT_FOUND), "class-compliant") != NULL);
    CHECK(strstr(ff802_disable_status_str(FF802_SW_INTERFACE_BUSY), "interface 3") != NULL);
    CHECK(strstr(ff802_disable_status_str(FF802_SW_STALL), "0x07") != NULL);
    CHECK(strstr(ff802_disable_layout_str(FF802_LAYOUT_NO_ENDPOINT), "0x07") != NULL);
    CHECK(strcmp(ff802_disable_status_str(FF802_SW_TIMEOUT), "bulk transfer timed out") == 0);
}

int main(void)
{
    test_success();
    test_not_sent();
    test_write_errors();
    test_selection_and_layout();
    test_enable_untouched();
    test_texts();
    TEST_EXIT();
}
