/*
 * T4: identification of a connection from sysfs, and the state files
 * (mode, per-connection records, events, lock). Fake sysfs tree only.
 */
#include "auto_fixture.h"

#include <sys/wait.h>

#include "ff802/auto_sysfs.h"
#include "test_common.h"

static void test_names(void)
{
    CHECK(ff802_sysfs_valid_name("3-2"));
    CHECK(ff802_sysfs_valid_name("3-2.4.1.1.3"));
    CHECK(ff802_sysfs_valid_name("255-1.2.3.4.5.6.7"));
    CHECK(!ff802_sysfs_valid_name("3-1.2.3.4.5.6.7.8"));   /* depth > 7 */
    CHECK(!ff802_sysfs_valid_name("usb3"));                /* root hub */
    CHECK(!ff802_sysfs_valid_name("3-2:1.0"));             /* interface */
    CHECK(!ff802_sysfs_valid_name("3-02"));
    CHECK(!ff802_sysfs_valid_name("0-1"));
    CHECK(!ff802_sysfs_valid_name("256-1"));
    CHECK(!ff802_sysfs_valid_name("3-"));
    CHECK(!ff802_sysfs_valid_name("3-2."));
    CHECK(!ff802_sysfs_valid_name("../3-2"));
    CHECK(!ff802_sysfs_valid_name("3-2/../../x"));
    CHECK(!ff802_sysfs_valid_name(""));
    CHECK(!ff802_sysfs_valid_name(NULL));
}

static void test_product(void)
{
    CHECK(ff802_sysfs_product_ok("Fireface 802"));
    CHECK(ff802_sysfs_product_ok("Fireface 802 (24047128)"));
    CHECK(!ff802_sysfs_product_ok("Fireface 802 FS (24047128)"));
    CHECK(!ff802_sysfs_product_ok("Fireface 802 FS"));
    CHECK(!ff802_sysfs_product_ok("Fireface 802 ()"));
    CHECK(!ff802_sysfs_product_ok("Fireface 802 (2404712x)"));
    CHECK(!ff802_sysfs_product_ok("Fireface 802 (24047128) "));
    CHECK(!ff802_sysfs_product_ok("Fireface UFX+"));
    CHECK(!ff802_sysfs_product_ok(""));
    CHECK(!ff802_sysfs_product_ok(NULL));
}

static void test_read_identify(void)
{
    fixture fx;
    fx_init(&fx);
    ff802_sysfs_dev d, e;

    fx_ff802_prop(&fx, 18);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-2.4.1.1.3", &d) == FF802_SYSFS_OK);
    CHECK(d.vid == 0x2A39 && d.pid == 0x3FCD && d.bus == 3 && d.devnum == 18);
    CHECK(strcmp(d.product, "Fireface 802 (24047128)") == 0);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_PROPRIETARY);

    fx_ff802_cc(&fx, 19);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-2.4.1.1.3", &e) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&e) == FF802_ID_CLASS_COMPLIANT);
    CHECK(!ff802_sysfs_same_connection(&d, &e));

    /* 0424 is Microchip: another 0424 device is not a Fireface */
    fx_dev(&fx, "1-4", "0424", "2514", 1, 3, "480", "Microchip", "USB2514B");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-4", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_UNKNOWN_DEVICE);
    /* Fireface 802 FS or another RME product */
    fx_dev(&fx, "1-5", "2a39", "3ffc", 1, 4, "480", "RME", "Fireface 802 FS (1)");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-5", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_UNKNOWN_DEVICE);
    /* right ID, but the strings do not identify an original 802 */
    fx_dev(&fx, "1-6", "2a39", "3fcd", 1, 5, "480", "RME", "Fireface 802 FS (1)");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-6", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_BAD_PRODUCT);
    fx_dev(&fx, "1-6", "2a39", "3fcd", 1, 5, "480", NULL, "Fireface 802");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-6", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_BAD_MANUFACTURER);
    fx_dev(&fx, "1-6", "2a39", "3fcd", 1, 5, "480", "RME", NULL);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-6", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_BAD_PRODUCT);
    fx_dev(&fx, "1-6", "0424", "3fdd", 1, 5, "480", "Microchip", "Fireface 802");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-6", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_BAD_MANUFACTURER);
    fx_dev(&fx, "1-6", "2a39", "3fcd", 1, 5, "12", "RME", "Fireface 802");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-6", &d) == FF802_SYSFS_OK);
    CHECK(ff802_sysfs_identify(&d) == FF802_ID_NOT_HIGH_SPEED);

    /* unplugged, malformed, invalid names */
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-9", &d) == FF802_SYSFS_ABSENT);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "../x", &d) == FF802_SYSFS_BAD_NAME);
    fx_dev(&fx, "1-7", "2a39", "3fcd", 1, 0, "480", "RME", "Fireface 802");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-7", &d) == FF802_SYSFS_UNREADABLE);
    fx_dev(&fx, "1-7", "2a3", "3fcd", 1, 6, "480", "RME", "Fireface 802");
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "1-7", &d) == FF802_SYSFS_UNREADABLE);
    fx_done(&fx);
}

static void test_matches_usb_dev(void)
{
    ff802_sysfs_dev s;
    memset(&s, 0, sizeof s);
    strcpy(s.kernel_name, "3-2.4.1.1.3");
    s.bus = 3; s.devnum = 18; s.vid = 0x2A39; s.pid = 0x3FCD;
    ff802_usb_dev u = { .bus = 3, .address = 18, .vid = 0x2A39, .pid = 0x3FCD,
                        .port_count = 5, .ports = { 2, 4, 1, 1, 3 } };
    CHECK(ff802_sysfs_matches_usb_dev(&s, &u));
    u.address = 19;
    CHECK(!ff802_sysfs_matches_usb_dev(&s, &u));
    u.address = 18; u.ports[4] = 4;
    CHECK(!ff802_sysfs_matches_usb_dev(&s, &u));
    u.ports[4] = 3; u.pid = 0x3FDD;
    CHECK(!ff802_sysfs_matches_usb_dev(&s, &u));
}

static void test_mode(void)
{
    fixture fx;
    fx_init(&fx);
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_OFF);   /* default */
    fx_mode(&fx, "on\n");
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_ON);
    fx_mode(&fx, "off\n");
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_OFF);
    fx_mode(&fx, "yes\n");
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_INVALID);
    fx_mode(&fx, "on\non\n");
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_INVALID);
    fx_mode(&fx, "");
    CHECK(ff802_auto_read_mode(&fx.paths) == FF802_AUTO_MODE_INVALID);
    fx_done(&fx);
}

static void test_connection_records(void)
{
    fixture fx;
    fx_init(&fx);
    ff802_sysfs_dev d;
    fx_ff802_prop(&fx, 18);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-2.4.1.1.3", &d) == FF802_SYSFS_OK);
    CHECK(ff802_auto_claim_connection(&fx.paths, &d) == 1);
    CHECK(ff802_auto_claim_connection(&fx.paths, &d) == 0);   /* same connection */
    CHECK(fx_exists(&fx, "run/attempts/3-2.4.1.1.3_3_18"));
    d.devnum = 20;                                             /* reconnected */
    CHECK(ff802_auto_claim_connection(&fx.paths, &d) == 1);
    /* "reboot": /run is empty again */
    fx_rm_rf(fx.paths.run_dir);
    d.devnum = 18;
    CHECK(ff802_auto_claim_connection(&fx.paths, &d) == 1);
    fx_done(&fx);
}

static void test_events(void)
{
    fixture fx;
    fx_init(&fx);
    ff802_sysfs_dev d;
    char buf[512];
    fx_ff802_prop(&fx, 18);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-2.4.1.1.3", &d) == FF802_SYSFS_OK);
    CHECK(ff802_auto_write_event(&fx.paths, FF802_EVENT_PREPARED, &d, "sent") == 1);
    CHECK(ff802_auto_write_event(&fx.paths, FF802_EVENT_FAILED, &d,
                                 "bad\nline\x01") == 2);
    fx_read(&fx, "run/event", buf, sizeof buf);
    CHECK(strstr(buf, "seq=2\nkind=failed\n") == buf);
    CHECK(strstr(buf, "\nport=3-2.4.1.1.3\nbusnum=3\ndevnum=18\n") != NULL);
    CHECK(strstr(buf, "\ndetail=bad?line?\n") != NULL);
    CHECK(!fx_exists(&fx, "run/event.tmp"));

    CHECK(!ff802_auto_pending_for(&fx.paths, "3-2.4.1.1.3"));
    CHECK(ff802_auto_set_pending(&fx.paths, &d) == 0);
    CHECK(ff802_auto_pending_for(&fx.paths, "3-2.4.1.1.3"));
    CHECK(!ff802_auto_pending_for(&fx.paths, "3-2.4.1.1"));
    CHECK(!ff802_auto_pending_for(&fx.paths, "3-2.4.1.1.4"));
    CHECK(ff802_auto_clear_pending(&fx.paths) == 0);
    CHECK(!ff802_auto_pending_for(&fx.paths, "3-2.4.1.1.3"));
    CHECK(ff802_auto_clear_pending(&fx.paths) == 0);

    CHECK(ff802_auto_write_result(&fx.paths, "sent", "3-2.4.1.1.3", "ok") == 0);
    fx_read(&fx, "run/last-result", buf, sizeof buf);
    CHECK(strstr(buf, "\noutcome=sent\nport=3-2.4.1.1.3\ndetail=ok\n") != NULL);
    fx_done(&fx);
}

/* Several processes claim the same connection at once: exactly one wins. */
static void test_lock_concurrent(void)
{
    fixture fx;
    fx_init(&fx);
    ff802_sysfs_dev d;
    fx_ff802_prop(&fx, 18);
    CHECK(ff802_sysfs_read(fx.paths.sysfs_root, "3-2.4.1.1.3", &d) == FF802_SYSFS_OK);
    enum { N = 8 };
    pid_t kids[N];
    for (int i = 0; i < N; i++) {
        kids[i] = fork();
        if (kids[i] == 0) {
            int fd = ff802_auto_lock(&fx.paths);
            int r = fd >= 0 ? ff802_auto_claim_connection(&fx.paths, &d) : -1;
            ff802_auto_unlock(fd);
            _exit(r == 1 ? 10 : r == 0 ? 11 : 12);
        }
    }
    int won = 0, lost = 0;
    for (int i = 0; i < N; i++) {
        int st = 0;
        waitpid(kids[i], &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 10) won++;
        if (WIFEXITED(st) && WEXITSTATUS(st) == 11) lost++;
    }
    CHECK(won == 1);
    CHECK(lost == N - 1);
    fx_done(&fx);
}

int main(void)
{
    test_names();
    test_product();
    test_read_identify();
    test_matches_usb_dev();
    test_mode();
    test_connection_records();
    test_events();
    test_lock_concurrent();
    TEST_EXIT();
}
