/*
 * Hub port of a device: resolve, cycle (disable=1, wait, disable=0) and
 * wait for the new USB ID, in a private fake sysfs tree (never /sys).
 */
#include "auto_fixture.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "ff802/usb_port.h"
#include "test_common.h"

#define PORT_DEV "3-2.4.1.1.3"
#define HUB      "3-2.4.1.1"

/* hub port directory with "disable" and a "device" link to target */
static void fx_port(fixture *fx, const char *hub, const char *iface,
                    const char *port, const char *target, int with_disable)
{
    char d[512], cmd[1024];
    snprintf(d, sizeof d, "%s/bus/usb/devices/%s/%s/%s", fx->paths.sysfs_root, hub, iface, port);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s'", d);
    if (system(cmd) != 0)
        exit(2);
    if (with_disable) {
        char f[600];
        snprintf(f, sizeof f, "%s/disable", d);
        fx_write(f, "0\n");
    }
    if (target != NULL) {
        char l[600], t[128];
        snprintf(l, sizeof l, "%s/device", d);
        snprintf(t, sizeof t, "../../../%s", target);
        unlink(l);
        if (symlink(t, l) != 0)
            exit(2);
    }
}

static void fx_hub_dev(fixture *fx, const char *name)
{
    char d[400];
    snprintf(d, sizeof d, "%s/bus/usb/devices/%s", fx->paths.sysfs_root, name);
    mkdir(d, 0755);
}

static const char *disable_value(fixture *fx, char *buf, size_t size)
{
    char rel[256];
    snprintf(rel, sizeof rel, "sys/bus/usb/devices/" HUB "/" HUB ":1.0/" HUB "-port3/disable");
    return fx_read(fx, rel, buf, size);
}

static void setup_hub(fixture *fx)
{
    fx_hub_dev(fx, HUB);
    fx_ff802_prop(fx, 18);
    fx_port(fx, HUB, HUB ":1.0", HUB "-port3", PORT_DEV, 1);
    fx_port(fx, HUB, HUB ":1.0", HUB "-port1", NULL, 1);   /* empty neighbour */
}

static void test_resolve(void)
{
    fixture fx;
    ff802_usb_port p;
    fx_init(&fx);
    setup_hub(&fx);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);
    CHECK(strcmp(p.hub, HUB) == 0 && p.number == 3);
    CHECK(strstr(p.dir, "/" HUB "/" HUB ":1.0/" HUB "-port3") != NULL);
    CHECK(ff802_port_belongs_to(&p, fx.paths.sysfs_root, PORT_DEV));

    /* root hub port, two-digit port number */
    fx_hub_dev(&fx, "usb3");
    fx_dev(&fx, "3-10", "2a39", "3fcd", 3, 4, "480", "RME", "Fireface 802 (1)");
    fx_port(&fx, "usb3", "3-0:1.0", "usb3-port10", "3-10", 1);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, "3-10", &p) == FF802_PORT_OK);
    CHECK(strcmp(p.hub, "usb3") == 0 && p.number == 10);

    /* the port of another device is never accepted */
    fx_dev(&fx, "3-2.4.1.1.1", "046d", "c31c", 3, 5, "12", "Logitech", "Keyboard");
    fx_port(&fx, HUB, HUB ":1.0", HUB "-port3", "3-2.4.1.1.1", 1);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_MISMATCH);
    fx_port(&fx, HUB, HUB ":1.0", HUB "-port3", PORT_DEV, 1);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);

    /* no link, no disable attribute, no hub, unplugged, bad names */
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -f '%s/bus/usb/devices/" HUB "/" HUB ":1.0/" HUB "-port3/device'",
             fx.paths.sysfs_root);
    CHECK(system(cmd) == 0);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_MISMATCH);
    fx_port(&fx, HUB, HUB ":1.0", HUB "-port3", PORT_DEV, 1);
    snprintf(cmd, sizeof cmd, "rm -f '%s/bus/usb/devices/" HUB "/" HUB ":1.0/" HUB "-port3/disable'",
             fx.paths.sysfs_root);
    CHECK(system(cmd) == 0);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_NO_DISABLE);
    fx_dev(&fx, "3-9.1", "2a39", "3fcd", 3, 6, "480", "RME", "Fireface 802 (1)");
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, "3-9.1", &p) == FF802_PORT_NOT_FOUND);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, "3-7.7", &p) == FF802_PORT_NO_DEVICE);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, "../x", &p) == FF802_PORT_BAD_NAME);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, "3-2.4.1.1:1.0", &p) == FF802_PORT_BAD_NAME);
    fx_done(&fx);

    /* the same port below two hub interfaces: ambiguous, refused */
    fx_init(&fx);
    setup_hub(&fx);
    fx_port(&fx, HUB, HUB ":1.1", HUB "-port3", PORT_DEV, 1);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_NOT_FOUND);
    fx_done(&fx);
}

/* sleep hook: records the calls and what "disable" holds meanwhile */
typedef struct hook {
    fixture *fx;
    int calls;
    unsigned ms[64];
    char during[16];
    int make_readonly;      /* make "disable" read-only while sleeping */
    int raise_term;         /* raise SIGTERM while sleeping */
    int appear_after;       /* create the device in CC mode after N calls */
    int appear_prop_after;  /* or in proprietary mode */
    int unplug_first;       /* remove the device at the first call */
} hook;

static void hook_sleep(void *user, unsigned ms)
{
    hook *h = user;
    if (h->calls < 64)
        h->ms[h->calls] = ms;
    h->calls++;
    if (h->calls == 1)
        disable_value(h->fx, h->during, sizeof h->during);
    if (h->make_readonly) {
        char f[600];
        snprintf(f, sizeof f, "%s/bus/usb/devices/" HUB "/" HUB ":1.0/" HUB "-port3/disable",
                 h->fx->paths.sysfs_root);
        chmod(f, 0444);
    }
    if (h->raise_term)
        raise(SIGTERM);
    if (h->unplug_first && h->calls == 1)
        fx_unplug(h->fx, PORT_DEV);
    if (h->appear_after && h->calls == h->appear_after)
        fx_ff802_cc(h->fx, 19);
    if (h->appear_prop_after && h->calls == h->appear_prop_after)
        fx_ff802_prop(h->fx, 20);
}

static void test_cycle(void)
{
    fixture fx;
    ff802_usb_port p;
    ff802_port_cycle_result r;
    char buf[16];

    fx_init(&fx);
    setup_hub(&fx);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);
    hook h = { .fx = &fx };
    ff802_port_ops ops = { .sleep_ms = hook_sleep, .user = &h };
    CHECK(ff802_port_cycle(&p, 2000, &ops, &r) == 0);
    CHECK(r.disabled && r.enabled && r.err_disable == 0 && r.err_enable == 0);
    CHECK(h.calls == 1 && h.ms[0] == 2000);
    CHECK(strcmp(h.during, "1\n") == 0);                 /* off for 2 s */
    CHECK(strcmp(disable_value(&fx, buf, sizeof buf), "0\n") == 0);
    /* the neighbour port is untouched */
    CHECK(strcmp(fx_read(&fx, "sys/bus/usb/devices/" HUB "/" HUB ":1.0/" HUB "-port1/disable",
                         buf, sizeof buf), "0\n") == 0);
    fx_done(&fx);

    if (getuid() != 0) {
        /* "1" cannot be written: no waiting, "0" is still attempted */
        fx_init(&fx);
        setup_hub(&fx);
        CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);
        char f[600];
        snprintf(f, sizeof f, "%s/disable", p.dir);
        chmod(f, 0444);
        hook h2 = { .fx = &fx };
        ops.user = &h2;
        CHECK(ff802_port_cycle(&p, 2000, &ops, &r) != 0);
        CHECK(!r.disabled && r.err_disable == EACCES && h2.calls == 0);
        CHECK(!r.enabled && r.err_enable == EACCES);
        fx_done(&fx);

        /* "0" fails: reported */
        fx_init(&fx);
        setup_hub(&fx);
        CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);
        hook h3 = { .fx = &fx, .make_readonly = 1 };
        ops.user = &h3;
        CHECK(ff802_port_cycle(&p, 2000, &ops, &r) != 0);
        CHECK(r.disabled && !r.enabled && r.err_enable == EACCES);
        fx_done(&fx);
    }

    /* SIGTERM during the off time: the port is enabled before the process ends */
    fx_init(&fx);
    setup_hub(&fx);
    CHECK(ff802_port_resolve(fx.paths.sysfs_root, PORT_DEV, &p) == FF802_PORT_OK);
    pid_t pid = fork();
    if (pid == 0) {
        hook hc = { .fx = &fx, .raise_term = 1 };
        ff802_port_ops oc = { .sleep_ms = hook_sleep, .user = &hc };
        ff802_port_cycle_result rc;
        ff802_port_cycle(&p, 2000, &oc, &rc);
        _exit(0);   /* not reached: SIGTERM is delivered after the cycle */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    CHECK(strcmp(disable_value(&fx, buf, sizeof buf), "0\n") == 0);
    fx_done(&fx);
}

static void test_wait(void)
{
    fixture fx;
    ff802_sysfs_dev d;

    fx_init(&fx);
    setup_hub(&fx);
    hook h = { .fx = &fx, .unplug_first = 1, .appear_after = 4 };
    ff802_port_ops ops = { .sleep_ms = hook_sleep, .user = &h };
    CHECK(ff802_port_wait_mode(fx.paths.sysfs_root, PORT_DEV, FF802_MODE_CLASS_COMPLIANT,
                               15000, &ops, &d) == FF802_WAIT_OK);
    CHECK(d.vid == 0x0424 && d.pid == 0x3FDD && d.devnum == 19 && h.calls == 4);
    CHECK(h.ms[1] == 100);

    /* already there: no waiting */
    hook h0 = { .fx = &fx };
    ops.user = &h0;
    CHECK(ff802_port_wait_mode(fx.paths.sysfs_root, PORT_DEV, FF802_MODE_CLASS_COMPLIANT,
                               15000, &ops, &d) == FF802_WAIT_OK && h0.calls == 0);

    /* never comes back: timeout after 15 s (150 polls) */
    fx_unplug(&fx, PORT_DEV);
    hook h1 = { .fx = &fx };
    ops.user = &h1;
    CHECK(ff802_port_wait_mode(fx.paths.sysfs_root, PORT_DEV, FF802_MODE_CLASS_COMPLIANT,
                               15000, &ops, &d) == FF802_WAIT_TIMEOUT);
    CHECK(h1.calls == 150 && d.vid == 0);

    /* comes back in the old mode: other */
    hook h2 = { .fx = &fx, .appear_prop_after = 2 };
    ops.user = &h2;
    CHECK(ff802_port_wait_mode(fx.paths.sysfs_root, PORT_DEV, FF802_MODE_CLASS_COMPLIANT,
                               1000, &ops, &d) == FF802_WAIT_OTHER);
    CHECK(d.vid == 0x2A39);
    /* and the other direction (disable): proprietary wanted */
    CHECK(ff802_port_wait_mode(fx.paths.sysfs_root, PORT_DEV, FF802_MODE_PROPRIETARY,
                               1000, &ops, &d) == FF802_WAIT_OK);
    fx_done(&fx);
}

int main(void)
{
    test_resolve();
    test_cycle();
    test_wait();
    TEST_EXIT();
}
