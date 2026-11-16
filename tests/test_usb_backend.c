/*
 * Tests for the layout check and helper parsers (no USB involved).
 */
#include "ff802/cc_switch.h"
#include "ff802/usb_backend.h"
#include "fake_usb.h"
#include "test_common.h"

static void test_layout_ok(void)
{
    ff802_dev_layout l;
    fake_usb_good_layout(&l);
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_OK);

    /* Endpoint order must not matter. */
    ff802_ep_info tmp = l.eps[0];
    l.eps[0] = l.eps[1];
    l.eps[1] = tmp;
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_OK);
}

static void test_layout_errors(void)
{
    ff802_dev_layout l;

    fake_usb_good_layout(&l);
    l.config_value = 2;
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_WRONG_CONFIG);

    fake_usb_good_layout(&l);
    l.config_value = 0;   /* unconfigured */
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_WRONG_CONFIG);

    fake_usb_good_layout(&l);
    l.has_interface = 0;
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_NO_INTERFACE);

    fake_usb_good_layout(&l);
    l.num_endpoints = 1;  /* only the IN endpoint 0x8C remains */
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_NO_ENDPOINT);

    fake_usb_good_layout(&l);
    l.num_endpoints = 0;
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_NO_ENDPOINT);

    fake_usb_good_layout(&l);
    l.eps[1].attributes = 0x03;  /* interrupt */
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_NOT_BULK);

    fake_usb_good_layout(&l);
    l.eps[1].attributes = 0x01;  /* isochronous */
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_NOT_BULK);

    fake_usb_good_layout(&l);
    l.eps[1].max_packet = 64;    /* full speed */
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_MAX_PACKET);

    /* Invalid endpoint count is clamped, not read out of bounds. */
    fake_usb_good_layout(&l);
    l.num_endpoints = 255;
    CHECK(ff802_check_layout(&l) == FF802_LAYOUT_OK);
}

static void test_confirm_answer(void)
{
    CHECK(ff802_confirm_answer_ok("ENABLE"));
    CHECK(ff802_confirm_answer_ok("ENABLE\n"));
    CHECK(ff802_confirm_answer_ok("ENABLE\r\n"));

    CHECK(!ff802_confirm_answer_ok(NULL));
    CHECK(!ff802_confirm_answer_ok(""));
    CHECK(!ff802_confirm_answer_ok("\n"));
    CHECK(!ff802_confirm_answer_ok("y\n"));
    CHECK(!ff802_confirm_answer_ok("yes\n"));
    CHECK(!ff802_confirm_answer_ok("enable\n"));
    CHECK(!ff802_confirm_answer_ok(" ENABLE\n"));
    CHECK(!ff802_confirm_answer_ok("ENABLE \n"));
    CHECK(!ff802_confirm_answer_ok("ENABLED\n"));
    CHECK(!ff802_confirm_answer_ok("ENABL\n"));
    CHECK(!ff802_confirm_answer_ok("ENABLE\nENABLE\n"));
}

static void test_parse_location(void)
{
    uint8_t bus = 0, addr = 0;

    CHECK(ff802_parse_location("3:18", &bus, &addr) == 0);
    CHECK(bus == 3 && addr == 18);
    CHECK(ff802_parse_location("003:018", &bus, &addr) == 0);
    CHECK(bus == 3 && addr == 18);
    CHECK(ff802_parse_location("255:127", &bus, &addr) == 0);
    CHECK(bus == 255 && addr == 127);

    static const char *bad[] = {
        "", "3", "3:", ":18", "3:18x", "x3:18", "3::18", "3:1:8",
        "0:5", "3:0", "256:1", "3:128", "-3:18", "3:-18", " 3:18",
        "3: 18", "1000:1", "3:00000000000000000000018x", "a:b",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        bus = 77; addr = 77;
        int r = ff802_parse_location(bad[i], &bus, &addr);
        CHECK(r != 0);
        CHECK(bus == 77 && addr == 77);   /* untouched on error */
        if (r == 0)
            fprintf(stderr, "  accepted invalid location '%s'\n", bad[i]);
    }
    CHECK(ff802_parse_location(NULL, &bus, &addr) != 0);
}

static void test_exit_codes(void)
{
    CHECK(ff802_sw_exit_code(FF802_SW_OK) == 0);
    CHECK(ff802_sw_exit_code(FF802_SW_NOT_FOUND) == 1);
    CHECK(ff802_sw_exit_code(FF802_SW_AMBIGUOUS) == 1);
    CHECK(ff802_sw_exit_code(FF802_SW_ACCESS_DENIED) == 3);
    CHECK(ff802_sw_exit_code(FF802_SW_KERNEL_DRIVER) == 3);
    CHECK(ff802_sw_exit_code(FF802_SW_INTERFACE_BUSY) == 3);
    CHECK(ff802_sw_exit_code(FF802_SW_UNEXPECTED_LAYOUT) == 3);
    CHECK(ff802_sw_exit_code(FF802_SW_TIMEOUT) == 4);
    CHECK(ff802_sw_exit_code(FF802_SW_SHORT_WRITE) == 4);
    CHECK(ff802_sw_exit_code(FF802_SW_STALL) == 4);
    CHECK(ff802_sw_exit_code(FF802_SW_WRITE_FAILED) == 4);
    CHECK(ff802_sw_exit_code(FF802_SW_ABORTED) == 5);
    CHECK(ff802_sw_exit_code(FF802_SW_WRITE_DISABLED) == 5);
}

static void test_strings(void)
{
    /* Every status and error has a non-empty description. */
    for (int s = FF802_SW_OK; s <= FF802_SW_INTERNAL; s++)
        CHECK(ff802_sw_status_str((ff802_sw_status)s)[0] != '\0');
    for (int e = FF802_USB_OK; e <= FF802_USB_ERR_OTHER; e++)
        CHECK(ff802_usb_err_str((ff802_usb_err)e)[0] != '\0');
    for (int l = FF802_LAYOUT_OK; l <= FF802_LAYOUT_MAX_PACKET; l++)
        CHECK(ff802_layout_status_str((ff802_layout_status)l)[0] != '\0');
    CHECK(ff802_sw_status_hint(FF802_SW_ACCESS_DENIED) != NULL);
    CHECK(ff802_sw_status_hint(FF802_SW_TIMEOUT) != NULL);
}

int main(void)
{
    test_layout_ok();
    test_layout_errors();
    test_confirm_answer();
    test_parse_location();
    test_exit_codes();
    test_strings();
    TEST_EXIT();
}
