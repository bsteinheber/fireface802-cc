/*
 * Preconditions of ff802-cc disable (disable_check.c) in a private fake
 * sysfs, /proc/asound and T4 state (never the real /sys or /proc).
 */
#include "auto_fixture.h"

#include "ff802/disable_check.h"
#include "test_common.h"

#define DEV "3-2.4.1.1.3"

typedef struct env {
    fixture fx;
    ff802_dchk_roots roots;
} env;

static void put(env *e, const char *rel, const char *content)
{
    char p[600], cmd[1400];
    snprintf(p, sizeof p, "%s/%s", e->fx.root, rel);
    snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname '%s')\"", p);
    if (system(cmd) != 0)
        exit(2);
    fx_write(p, content);
}

static void link_to(env *e, const char *rel, const char *target)
{
    char p[600], cmd[1400];
    snprintf(p, sizeof p, "%s/%s", e->fx.root, rel);
    snprintf(cmd, sizeof cmd, "mkdir -p \"$(dirname '%s')\" && rm -f '%s'", p, p);
    if (system(cmd) != 0 || symlink(target, p) != 0)
        exit(2);
}

static void rm(env *e, const char *rel)
{
    char cmd[700];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/%s'", e->fx.root, rel);
    if (system(cmd) != 0)
        exit(2);
}

#define IF3 "sys/bus/usb/devices/" DEV ":1.3"

/* The unit as on the host: CC mode, interface 3 USB-MIDI bound to snd-usb-audio. */
static void env_init(env *e)
{
    fx_init(&e->fx);
    memset(&e->roots, 0, sizeof e->roots);
    e->roots.paths = e->fx.paths;
    snprintf(e->roots.proc_asound, sizeof e->roots.proc_asound, "%s/proc/asound", e->fx.root);
    fx_ff802_cc(&e->fx, 19);
    put(e, "sys/bus/usb/devices/" DEV "/bConfigurationValue", "1\n");
    put(e, IF3 "/bInterfaceClass", "01\n");
    put(e, IF3 "/bInterfaceSubClass", "03\n");
    put(e, IF3 "/ep_07/bmAttributes", "02\n");
    put(e, IF3 "/ep_07/wMaxPacketSize", "0200\n");
    put(e, IF3 "/ep_07/direction", "out\n");
    link_to(e, IF3 "/driver", "../../../../bus/usb/drivers/snd-usb-audio");
    put(e, "sys/bus/usb/devices/" DEV ":1.0/sound/card1/number", "1\n");
    put(e, "proc/asound/card1/pcm0p/sub0/status", "closed\n");
    put(e, "proc/asound/card1/pcm0c/sub0/status", "closed\n");
}

static ff802_dchk_status check(env *e, ff802_dchk_target *t, char *why, size_t ws)
{
    return ff802_disable_check(&e->roots, 0, 0, 0, t, why, ws);
}

static void test_ok(void)
{
    env e; ff802_dchk_target t; char why[256];
    env_init(&e);
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK);
    CHECK(strcmp(t.dev.kernel_name, DEV) == 0 && t.dev.bus == 3 && t.dev.devnum == 19);
    CHECK(t.card == 1);
    /* T4 off or anything but "on": allowed */
    fx_mode(&e.fx, "off\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK);
    fx_mode(&e.fx, "yes\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK);
    /* opened but idle PCMs, no card at all */
    put(&e, "proc/asound/card1/pcm0p/sub0/status", "state: SETUP\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK);
    rm(&e, "sys/bus/usb/devices/" DEV ":1.0/sound");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK && t.card == -1);
    fx_done(&e.fx);
}

static void test_selection(void)
{
    env e; ff802_dchk_target t; char why[256];
    env_init(&e);
    fx_unplug(&e.fx, DEV);
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_NOT_FOUND);
    fx_ff802_prop(&e.fx, 20);                       /* proprietary: nothing to do */
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_NOT_FOUND);
    fx_done(&e.fx);

    env_init(&e);
    fx_dev(&e.fx, "1-5", "0424", "3fdd", 1, 7, "480", "RME", "Fireface 802 (2)");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_AMBIGUOUS);
    CHECK(ff802_disable_check(&e.roots, 1, 3, 19, &t, why, sizeof why) == FF802_DCHK_OK);
    CHECK(strcmp(t.dev.kernel_name, DEV) == 0);
    CHECK(ff802_disable_check(&e.roots, 1, 3, 20, &t, why, sizeof why) == FF802_DCHK_NOT_FOUND);
    fx_done(&e.fx);

    /* 0424:3fdd that is not an original Fireface 802 */
    const char *products[] = { "Fireface 802 FS (12345678)", "USB Audio", NULL };
    for (int i = 0; i < 3; i++) {
        env_init(&e);
        fx_dev(&e.fx, DEV, "0424", "3fdd", 3, 19, "480", "RME", products[i]);
        CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_IDENTITY);
        fx_done(&e.fx);
    }
    env_init(&e);
    fx_dev(&e.fx, DEV, "0424", "3fdd", 3, 19, "480", "Microchip", "Fireface 802 (1)");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_IDENTITY);
    fx_dev(&e.fx, DEV, "0424", "3fdd", 3, 19, "12", "RME", "Fireface 802 (1)");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_IDENTITY);
    fx_done(&e.fx);
}

static void test_interface(void)
{
    struct { const char *rel, *val; } bad[] = {
        { "sys/bus/usb/devices/" DEV "/bConfigurationValue", "2\n" },
        { IF3 "/bInterfaceClass", "ff\n" },
        { IF3 "/bInterfaceSubClass", "01\n" },
        { IF3 "/ep_07/bmAttributes", "03\n" },
        { IF3 "/ep_07/wMaxPacketSize", "0040\n" },
        { IF3 "/ep_07/direction", "in\n" },
    };
    env e; ff802_dchk_target t; char why[256];
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        env_init(&e);
        put(&e, bad[i].rel, bad[i].val);
        CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_INTERFACE);
        fx_done(&e.fx);
    }
    env_init(&e);
    rm(&e, IF3 "/ep_07");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_INTERFACE);
    rm(&e, IF3);
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_INTERFACE);
    fx_done(&e.fx);

    /* kernel driver: exactly snd-usb-audio */
    env_init(&e);
    rm(&e, IF3 "/driver");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_DRIVER);
    CHECK(strstr(why, "no kernel driver") != NULL);
    link_to(&e, IF3 "/driver", "../../../../bus/usb/drivers/usbfs");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_DRIVER);
    CHECK(strstr(why, "'usbfs'") != NULL);
    fx_done(&e.fx);
}

static void test_automation_and_audio(void)
{
    env e; ff802_dchk_target t; char why[256];
    env_init(&e);
    fx_mode(&e.fx, "on\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_AUTOMATION_ON);
    CHECK(strstr(why, "ff802-cc-auto off") != NULL);
    fx_done(&e.fx);

    const char *busy[] = { "state: RUNNING\n", "state: PREPARED\n", "state: DRAINING\n",
                           "state: PAUSED\n", "state: XRUN\n" };
    for (size_t i = 0; i < sizeof busy / sizeof busy[0]; i++) {
        env_init(&e);
        put(&e, "proc/asound/card1/pcm0p/sub0/status", busy[i]);
        CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_AUDIO_ACTIVE);
        CHECK(strstr(why, "card1/pcm0p/sub0") != NULL);
        fx_done(&e.fx);
    }
    /* recording counts as well */
    env_init(&e);
    put(&e, "proc/asound/card1/pcm0c/sub0/status", "state: RUNNING\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_AUDIO_ACTIVE);
    /* another card playing does not matter */
    put(&e, "proc/asound/card1/pcm0c/sub0/status", "closed\n");
    put(&e, "proc/asound/card0/pcm0p/sub0/status", "state: RUNNING\n");
    CHECK(check(&e, &t, why, sizeof why) == FF802_DCHK_OK);
    fx_done(&e.fx);
}

int main(void)
{
    test_ok();
    test_selection();
    test_interface();
    test_automation_and_audio();
    TEST_EXIT();
}
