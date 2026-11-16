/*
 * Tests for the CC switch sequence against the simulated USB backend.
 *
 * Key properties verified for every scenario:
 *  - the order of USB operations (trace),
 *  - no write without confirmation and write gate,
 *  - at most one write attempt (no retries),
 *  - every opened handle is closed and every claim is released.
 */
#include "ff802/cc_switch.h"
#include "fake_usb.h"
#include "test_common.h"

/* Captured from Windows: enable class-compliant mode. */
static const uint8_t CAPTURE_CC_ENABLE[4] = { 0x01, 0x00, 0x44, 0xBD };

typedef struct confirm_state {
    int answer;
    int calls;
    ff802_usb_dev target;
    uint8_t cmd[4];
} confirm_state;

static int confirm_cb(void *user, const ff802_usb_dev *target,
                      const uint8_t cmd[4])
{
    confirm_state *c = user;
    c->calls++;
    c->target = *target;
    memcpy(c->cmd, cmd, 4);
    return c->answer;
}

static ff802_sw_options opts(ff802_sw_action action, confirm_state *c)
{
    ff802_sw_options o = { .action = action, .confirm = confirm_cb,
                           .confirm_user = c };
    return o;
}

/* Run a scenario and check resource balance invariants. */
static ff802_sw_status run(fake_usb *f, const ff802_sw_options *o,
                           ff802_sw_result *res)
{
    ff802_usb_backend be = fake_usb_backend(f);
    ff802_sw_status st = ff802_switch_run(&be, o, NULL, res);

    CHECK(st == res->status);
    CHECK(f->open_handles == 0);     /* every open is closed */
    CHECK(f->claimed == 0);          /* every claim is released */
    CHECK(f->n_write <= 1);          /* never retried */
    CHECK(f->n_list == f->n_free || f->err_list != FF802_USB_OK);
    return st;
}

#define CHECK_TRACE(f, expected)                                           \
    do {                                                                   \
        test_checks++;                                                     \
        if (strcmp((f).trace, (expected)) != 0) {                          \
            test_failures++;                                               \
            fprintf(stderr, "%s:%d: trace\n  actual:   %s\n  expected: %s\n", \
                    __FILE__, __LINE__, (f).trace, (expected));            \
        }                                                                  \
    } while (0)

static void test_check_only(void)
{
    fake_usb f; fake_usb_init(&f);
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_CHECK, &c);
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_OK);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,close");
    CHECK(c.calls == 0);
    CHECK(f.n_claim == 0 && f.n_write == 0);
    CHECK(r.have_target && r.target.bus == 3 && r.target.address == 18);
    CHECK(f.layout_iface == 1 && f.layout_alt == 0);
    CHECK(f.last_iface == 1);
    CHECK(!r.write_attempted);
}

static void test_check_only_ignores_write_gate(void)
{
    /* The diagnose mode works the same in a write-disabled build. */
    fake_usb f; fake_usb_init(&f);
    f.write_enabled = 0;
    ff802_sw_options o = opts(FF802_SW_CHECK, NULL);
    o.confirm = NULL;
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_OK);
    CHECK(f.n_claim == 0 && f.n_write == 0);
}

static void test_enable_success(void)
{
    fake_usb f; fake_usb_init(&f);
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_OK);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,claim,write,release,close");
    CHECK(c.calls == 1);
    CHECK_BYTES(c.cmd, CAPTURE_CC_ENABLE, 4);
    CHECK(c.target.bus == 3 && c.target.address == 18);

    CHECK(f.n_write == 1);
    CHECK(f.write_ep == 0x0C);
    CHECK(f.write_len == 4);
    CHECK_BYTES(f.write_data, CAPTURE_CC_ENABLE, 4);
    CHECK(f.write_timeout == 1000);
    CHECK(f.last_iface == 1);
    CHECK(r.write_attempted && r.transferred == 4);
    CHECK(r.failed_step == FF802_STEP_NONE);
    CHECK(!r.release_failed);
    CHECK(f.opened_dev.vid == 0x2A39 && f.opened_dev.pid == 0x3FCD);
}

static void test_enable_not_confirmed(void)
{
    fake_usb f; fake_usb_init(&f);
    confirm_state c = { .answer = 0 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_ABORTED);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,close");
    CHECK(c.calls == 1);
    CHECK(f.n_claim == 0 && f.n_write == 0);
    CHECK(r.failed_step == FF802_STEP_CONFIRM);
}

static void test_enable_without_callback(void)
{
    fake_usb f; fake_usb_init(&f);
    ff802_sw_options o = { .action = FF802_SW_ENABLE_CC };
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_ABORTED);
    CHECK(f.n_claim == 0 && f.n_write == 0);
}

static void test_write_disabled(void)
{
    fake_usb f; fake_usb_init(&f);
    f.write_enabled = 0;
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_WRITE_DISABLED);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,close");
    CHECK(c.calls == 0);                  /* user is not even asked */
    CHECK(f.n_claim == 0 && f.n_write == 0);
    CHECK(r.failed_step == FF802_STEP_WRITE_GATE);
}

static void test_not_found(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    /* No RME device at all. */
    fake_usb f; fake_usb_init(&f);
    f.devs[0].vid = 0x1234;
    CHECK(run(&f, &o, &r) == FF802_SW_NOT_FOUND);
    CHECK_TRACE(f, "list,free");
    CHECK(!r.have_target);

    /* Device already in class-compliant mode is not a target. */
    fake_usb_init(&f);
    f.devs[0].vid = 0x0424;
    f.devs[0].pid = 0x3FDD;
    CHECK(run(&f, &o, &r) == FF802_SW_NOT_FOUND);
    CHECK(f.n_open == 0);

    /* Other RME device is not a target. */
    fake_usb_init(&f);
    f.devs[0].pid = 0x3FC1;
    CHECK(run(&f, &o, &r) == FF802_SW_NOT_FOUND);
    CHECK(f.n_open == 0);

    /* Empty bus. */
    fake_usb_init(&f);
    f.ndevs = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_NOT_FOUND);
    CHECK(c.calls == 0);
}

static void test_selection(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    fake_usb f; fake_usb_init(&f);
    f.devs[2] = f.devs[0];
    f.devs[2].bus = 4;
    f.devs[2].address = 7;
    f.ndevs = 3;

    /* Two Fireface 802 without selection: refuse. */
    CHECK(run(&f, &o, &r) == FF802_SW_AMBIGUOUS);
    CHECK_TRACE(f, "list,free");

    /* With selection: exactly the chosen one is used. */
    fake_usb f2 = f;
    f2.trace[0] = '\0';
    f2.n_list = f2.n_free = 0;
    o.select_location = 1;
    o.bus = 4;
    o.address = 7;
    CHECK(run(&f2, &o, &r) == FF802_SW_OK);
    CHECK(f2.opened_dev.bus == 4 && f2.opened_dev.address == 7);
    CHECK(r.target.bus == 4 && r.target.address == 7);

    /* Selection pointing at a non-Fireface device: not found. */
    fake_usb f3; fake_usb_init(&f3);
    o.bus = 1;
    o.address = 2;
    CHECK(run(&f3, &o, &r) == FF802_SW_NOT_FOUND);
    CHECK(f3.n_open == 0);

    /* Selection pointing at nothing. */
    fake_usb_init(&f3);
    o.bus = 9;
    o.address = 9;
    CHECK(run(&f3, &o, &r) == FF802_SW_NOT_FOUND);
}

static void test_list_error(void)
{
    fake_usb f; fake_usb_init(&f);
    f.err_list = FF802_USB_ERR_NO_MEM;
    ff802_sw_options o = opts(FF802_SW_CHECK, NULL);
    ff802_sw_result r;

    CHECK(run(&f, &o, &r) == FF802_SW_LIST_FAILED);
    CHECK(r.usb_err == FF802_USB_ERR_NO_MEM);
    CHECK(r.failed_step == FF802_STEP_LIST);
    CHECK(f.n_open == 0);
}

static void test_layout_problems(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;
    fake_usb f;

    fake_usb_init(&f);
    f.err_layout = FF802_USB_ERR_IO;
    CHECK(run(&f, &o, &r) == FF802_SW_LAYOUT_FAILED);
    CHECK_TRACE(f, "list,free,layout");

    fake_usb_init(&f);
    f.layout.config_value = 2;
    CHECK(run(&f, &o, &r) == FF802_SW_UNEXPECTED_LAYOUT);
    CHECK(r.layout == FF802_LAYOUT_WRONG_CONFIG);
    CHECK(f.n_open == 0);

    fake_usb_init(&f);
    f.layout.eps[1].max_packet = 64;
    CHECK(run(&f, &o, &r) == FF802_SW_UNEXPECTED_LAYOUT);
    CHECK(r.layout == FF802_LAYOUT_MAX_PACKET);
    CHECK(f.n_open == 0 && c.calls == 0);
}

static void test_open_errors(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;
    fake_usb f;

    fake_usb_init(&f);
    f.err_open = FF802_USB_ERR_ACCESS;
    CHECK(run(&f, &o, &r) == FF802_SW_ACCESS_DENIED);
    CHECK_TRACE(f, "list,free,layout,open");   /* nothing to close */
    CHECK(r.failed_step == FF802_STEP_OPEN);
    CHECK(c.calls == 0);

    fake_usb_init(&f);
    f.err_open = FF802_USB_ERR_NO_DEVICE;
    CHECK(run(&f, &o, &r) == FF802_SW_DISCONNECTED);

    fake_usb_init(&f);
    f.err_open = FF802_USB_ERR_IO;
    CHECK(run(&f, &o, &r) == FF802_SW_OPEN_FAILED);
}

static void test_kernel_driver(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;
    fake_usb f;

    fake_usb_init(&f);
    f.kernel_driver_active = 1;
    CHECK(run(&f, &o, &r) == FF802_SW_KERNEL_DRIVER);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,close");
    CHECK(c.calls == 0 && f.n_claim == 0);

    /* Also reported in diagnose mode. */
    fake_usb_init(&f);
    f.kernel_driver_active = 1;
    o.action = FF802_SW_CHECK;
    CHECK(run(&f, &o, &r) == FF802_SW_KERNEL_DRIVER);

    fake_usb_init(&f);
    f.err_kdrv = FF802_USB_ERR_NOT_SUPPORTED;
    CHECK(run(&f, &o, &r) == FF802_SW_DRIVER_CHECK_FAILED);
    CHECK(f.n_close == 1);
}

static void test_claim_errors(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;
    fake_usb f;

    fake_usb_init(&f);
    f.err_claim = FF802_USB_ERR_BUSY;
    CHECK(run(&f, &o, &r) == FF802_SW_INTERFACE_BUSY);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,claim,close");
    CHECK(f.n_write == 0 && f.n_release == 0);
    CHECK(r.failed_step == FF802_STEP_CLAIM);

    fake_usb_init(&f);
    f.err_claim = FF802_USB_ERR_ACCESS;
    CHECK(run(&f, &o, &r) == FF802_SW_ACCESS_DENIED);

    fake_usb_init(&f);
    f.err_claim = FF802_USB_ERR_NO_DEVICE;
    CHECK(run(&f, &o, &r) == FF802_SW_DISCONNECTED);

    fake_usb_init(&f);
    f.err_claim = FF802_USB_ERR_IO;
    CHECK(run(&f, &o, &r) == FF802_SW_CLAIM_FAILED);
    CHECK(f.n_write == 0);
}

static void test_write_errors(void)
{
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;
    fake_usb f;

    /* Timeout, nothing transferred. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_TIMEOUT;
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_TIMEOUT);
    CHECK_TRACE(f, "list,free,layout,open,kdrv,claim,write,release,close");
    CHECK(f.n_write == 1);
    CHECK(r.write_attempted && r.transferred == 0);
    CHECK(r.failed_step == FF802_STEP_WRITE);
    CHECK(r.usb_err == FF802_USB_ERR_TIMEOUT);

    /* Timeout after a partial transfer counts as short write. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_TIMEOUT;
    f.write_transferred = 2;
    CHECK(run(&f, &o, &r) == FF802_SW_SHORT_WRITE);
    CHECK(r.transferred == 2 && f.n_write == 1);

    /* Success code but fewer bytes. */
    fake_usb_init(&f);
    f.write_transferred = 3;
    CHECK(run(&f, &o, &r) == FF802_SW_SHORT_WRITE);
    CHECK(f.n_write == 1 && f.n_release == 1);

    /* Zero bytes with success code. */
    fake_usb_init(&f);
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_SHORT_WRITE);

    /* Stall. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_PIPE;
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_STALL);
    CHECK(f.n_write == 1);

    /* Disconnect during transfer. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_NO_DEVICE;
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_DISCONNECTED);
    CHECK(f.n_write == 1);

    /* Generic I/O error. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_IO;
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_WRITE_FAILED);
    CHECK(f.n_write == 1 && f.n_release == 1 && f.n_close == 1);

    /* Backend refusing the write at the last moment. */
    fake_usb_init(&f);
    f.err_write = FF802_USB_ERR_WRITE_DISABLED;
    f.write_transferred = 0;
    CHECK(run(&f, &o, &r) == FF802_SW_WRITE_DISABLED);
}

static void test_release_failure(void)
{
    fake_usb f; fake_usb_init(&f);
    f.err_release = FF802_USB_ERR_NO_DEVICE;
    confirm_state c = { .answer = 1 };
    ff802_sw_options o = opts(FF802_SW_ENABLE_CC, &c);
    ff802_sw_result r;

    /* The command was transferred; a release error is only a warning. */
    CHECK(run(&f, &o, &r) == FF802_SW_OK);
    CHECK(r.release_failed && r.release_err == FF802_USB_ERR_NO_DEVICE);
    CHECK(f.n_close == 1);
}

static void test_invalid_args(void)
{
    ff802_sw_result r;
    ff802_sw_options o = { .action = FF802_SW_CHECK };
    fake_usb f; fake_usb_init(&f);
    ff802_usb_backend be = fake_usb_backend(&f);

    CHECK(ff802_switch_run(NULL, &o, NULL, &r) == FF802_SW_INTERNAL);
    CHECK(ff802_switch_run(&be, NULL, NULL, &r) == FF802_SW_INTERNAL);
    CHECK(ff802_switch_run(&be, &o, NULL, NULL) == FF802_SW_INTERNAL);
    CHECK(f.n_list == 0);
}

static void test_log_output(void)
{
    fake_usb f; fake_usb_init(&f);
    ff802_usb_backend be = fake_usb_backend(&f);
    ff802_sw_options o = { .action = FF802_SW_CHECK };
    ff802_sw_result r;
    FILE *log = tmpfile();
    char buf[2048] = { 0 };

    CHECK(log != NULL);
    if (log == NULL)
        return;
    CHECK(ff802_switch_run(&be, &o, log, &r) == FF802_SW_OK);
    rewind(log);
    size_t n = fread(buf, 1, sizeof buf - 1, log);
    buf[n] = '\0';
    fclose(log);

    CHECK(strstr(buf, "Target: Bus 003 Device 018 (ID 2a39:3fcd)") != NULL);
    CHECK(strstr(buf, "Check only: interface not claimed, nothing sent.") != NULL);
    CHECK(strstr(buf, "Device closed.") != NULL);
}

int main(void)
{
    test_check_only();
    test_check_only_ignores_write_gate();
    test_enable_success();
    test_enable_not_confirmed();
    test_enable_without_callback();
    test_write_disabled();
    test_not_found();
    test_selection();
    test_list_error();
    test_layout_problems();
    test_open_errors();
    test_kernel_driver();
    test_claim_errors();
    test_write_errors();
    test_release_failure();
    test_invalid_args();
    test_log_output();
    TEST_EXIT();
}
