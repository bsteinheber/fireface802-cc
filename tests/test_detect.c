/*
 * Tests for device detection and report formatting using fake device
 * lists. No USB hardware or libusb is involved.
 */
#include "ff802/detect.h"
#include "test_common.h"

#include <stdlib.h>

/* Typical non-RME devices found on a Linux machine. */
#define ROOT_HUB(b)  { .bus = (b), .address = 1, .vid = 0x1D6B, .pid = 0x0002, \
                       .bcd_device = 0x0700, .port_count = 0 }
#define KEYBOARD     { .bus = 1, .address = 4, .vid = 0x046D, .pid = 0xC31C, \
                       .bcd_device = 0x6400, .port_count = 1, .ports = { 2 } }
#define MICROCHIP_HUB { .bus = 1, .address = 3, .vid = 0x0424, .pid = 0x2514, \
                        .bcd_device = 0x0b53, .port_count = 1, .ports = { 1 } }

#define FF802_PROP   { .bus = 2, .address = 5, .vid = 0x2A39, .pid = 0x3FCD, \
                       .bcd_device = 0x0106, .port_count = 2, .ports = { 3, 1 } }
#define FF802_CC     { .bus = 3, .address = 9, .vid = 0x0424, .pid = 0x3FDD, \
                       .bcd_device = 0x0100, .port_count = 1, .ports = { 4 } }
#define RME_UFX      { .bus = 2, .address = 7, .vid = 0x2A39, .pid = 0x3FC1, \
                       .bcd_device = 0x0100, .port_count = 1, .ports = { 2 } }

/* Run ff802_report() and capture its output in a heap buffer. */
static char *run_report(const ff802_usb_dev *devs, size_t n, int *exit_code)
{
    FILE *f = tmpfile();
    if (f == NULL) {
        perror("tmpfile");
        exit(2);
    }
    *exit_code = ff802_report(devs, n, f);

    long size = ftell(f);
    rewind(f);
    char *buf = calloc((size_t)size + 1, 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fprintf(stderr, "failed to read report output\n");
        exit(2);
    }
    fclose(f);
    return buf;
}

static void test_find_none(void)
{
    static const ff802_usb_dev devs[] = { ROOT_HUB(1), KEYBOARD, MICROCHIP_HUB };
    ff802_match m[4];

    CHECK(ff802_find_devices(devs, 3, m, 4) == 0);
    CHECK(ff802_find_devices(devs, 0, m, 4) == 0);
    CHECK(ff802_find_devices(NULL, 3, m, 4) == 0);
}

static void test_find_modes(void)
{
    static const ff802_usb_dev devs[] = {
        ROOT_HUB(1), FF802_PROP, KEYBOARD, FF802_CC, RME_UFX, MICROCHIP_HUB,
    };
    ff802_match m[8];

    size_t n = ff802_find_devices(devs, 6, m, 8);
    CHECK(n == 3);
    CHECK(m[0].dev == &devs[1] && m[0].mode == FF802_MODE_PROPRIETARY);
    CHECK(m[1].dev == &devs[3] && m[1].mode == FF802_MODE_CLASS_COMPLIANT);
    CHECK(m[2].dev == &devs[4] && m[2].mode == FF802_MODE_RME_OTHER);
}

static void test_find_truncated(void)
{
    static const ff802_usb_dev devs[] = { FF802_PROP, FF802_PROP, FF802_CC };
    ff802_match m[1] = { { NULL, FF802_MODE_NONE } };

    /* Total count is returned even if the output array is too small. */
    CHECK(ff802_find_devices(devs, 3, m, 1) == 3);
    CHECK(m[0].dev == &devs[0]);
    CHECK(ff802_find_devices(devs, 3, NULL, 0) == 3);
}

static void test_port_path(void)
{
    char buf[48];
    ff802_usb_dev d = FF802_PROP;

    ff802_format_port_path(&d, buf, sizeof buf);
    CHECK(strcmp(buf, "2-3.1") == 0);

    ff802_usb_dev root = ROOT_HUB(4);
    ff802_format_port_path(&root, buf, sizeof buf);
    CHECK(strcmp(buf, "4") == 0);

    ff802_usb_dev deep = { .bus = 1, .port_count = 7,
                           .ports = { 1, 2, 3, 4, 5, 6, 7 } };
    CHECK(ff802_format_port_path(&deep, buf, sizeof buf) == 15);
    CHECK(strcmp(buf, "1-1.2.3.4.5.6.7") == 0);

    /* Truncation: snprintf semantics, output stays NUL-terminated. */
    char small[4];
    CHECK(ff802_format_port_path(&deep, small, sizeof small) == 15);
    CHECK(strcmp(small, "1-1") == 0);

    /* Invalid depth is clamped instead of reading past ports[]. */
    ff802_usb_dev bad = { .bus = 1, .port_count = 200, .ports = { 9 } };
    CHECK(ff802_format_port_path(&bad, buf, sizeof buf) == 15);
}

static void test_format_device(void)
{
    char buf[160];
    ff802_usb_dev d = FF802_PROP;

    ff802_format_device(&d, FF802_MODE_PROPRIETARY, buf, sizeof buf);
    CHECK(strcmp(buf, "Bus 002 Device 005: ID 2a39:3fcd mode=proprietary "
                      "port=2-3.1 bcdDevice=1.06") == 0);

    ff802_usb_dev c = FF802_CC;
    ff802_format_device(&c, FF802_MODE_CLASS_COMPLIANT, buf, sizeof buf);
    CHECK(strcmp(buf, "Bus 003 Device 009: ID 0424:3fdd mode=class-compliant "
                      "port=3-4 bcdDevice=1.00") == 0);
}

static void test_report_found(void)
{
    static const ff802_usb_dev devs[] = { ROOT_HUB(2), KEYBOARD, FF802_PROP };
    int code = -1;
    char *out = run_report(devs, 3, &code);

    CHECK(code == FF802_EXIT_FOUND);
    CHECK(strstr(out, "Scanned 3 USB device(s).") != NULL);
    CHECK(strstr(out, "ID 2a39:3fcd mode=proprietary") != NULL);
    CHECK(strstr(out, "Fireface 802, proprietary USB mode") != NULL);
    CHECK(strstr(out, "Found 1 Fireface 802 device(s).") != NULL);
    CHECK(strstr(out, "046d") == NULL);   /* unrelated devices not listed */
    free(out);
}

static void test_report_cc_and_other(void)
{
    static const ff802_usb_dev devs[] = { RME_UFX, FF802_CC, MICROCHIP_HUB };
    int code = -1;
    char *out = run_report(devs, 3, &code);

    CHECK(code == FF802_EXIT_FOUND);
    CHECK(strstr(out, "ID 0424:3fdd mode=class-compliant") != NULL);
    CHECK(strstr(out, "not yet confirmed") != NULL);
    CHECK(strstr(out, "Note: other RME devices present:") != NULL);
    CHECK(strstr(out, "ID 2a39:3fc1 mode=rme-other") != NULL);
    CHECK(strstr(out, "0424:2514") == NULL);  /* Microchip hub ignored */
    /* Fireface entries come before the "other RME" note. */
    CHECK(strstr(out, "3fdd") < strstr(out, "Note:"));
    free(out);
}

static void test_report_only_other_rme(void)
{
    static const ff802_usb_dev devs[] = { RME_UFX };
    int code = -1;
    char *out = run_report(devs, 1, &code);

    CHECK(code == FF802_EXIT_NOT_FOUND);
    CHECK(strstr(out, "mode=rme-other") != NULL);
    CHECK(strstr(out, "No Fireface 802 found.") != NULL);
    free(out);
}

static void test_report_not_found(void)
{
    static const ff802_usb_dev devs[] = { ROOT_HUB(1), KEYBOARD };
    int code = -1;
    char *out = run_report(devs, 2, &code);

    CHECK(code == FF802_EXIT_NOT_FOUND);
    CHECK(strstr(out, "No Fireface 802 found.") != NULL);
    free(out);
}

static void test_report_empty(void)
{
    int code = -1;
    char *out = run_report(NULL, 0, &code);

    CHECK(code == FF802_EXIT_NOT_FOUND);
    CHECK(strstr(out, "Scanned 0 USB device(s).") != NULL);
    CHECK(strstr(out, "No USB devices visible.") != NULL);
    free(out);
}

static void test_report_multiple(void)
{
    static const ff802_usb_dev devs[] = { FF802_PROP, FF802_CC };
    int code = -1;
    char *out = run_report(devs, 2, &code);

    CHECK(code == FF802_EXIT_FOUND);
    CHECK(strstr(out, "Found 2 Fireface 802 device(s).") != NULL);
    free(out);
}

int main(void)
{
    test_find_none();
    test_find_modes();
    test_find_truncated();
    test_port_path();
    test_format_device();
    test_report_found();
    test_report_cc_and_other();
    test_report_only_other_rme();
    test_report_not_found();
    test_report_empty();
    test_report_multiple();
    TEST_EXIT();
}
