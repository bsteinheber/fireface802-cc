/*
 * Mixer initialisation: sound card of a connection (fake sysfs tree) and
 * selection of the sequencer port (lists as reported by ALSA).
 */
#include "auto_fixture.h"

#include "ff802/mixer_port.h"
#include "test_common.h"

#define PORT "3-2.4.1.1.3"

static void mkdirs(fixture *fx, const char *rel)
{
    char cmd[512];
    snprintf(cmd, sizeof cmd, "mkdir -p -- '%s/%s'", fx->paths.sysfs_root, rel);
    if (system(cmd) != 0)
        exit(2);
}

/* A card below interface <ifnum> of the connection, with or without MIDI. */
static void card(fixture *fx, const char *dev, unsigned ifnum, int n, int midi)
{
    char rel[256];
    if (midi)
        snprintf(rel, sizeof rel, "bus/usb/devices/%s/%s:1.%u/sound/card%d/midiC%dD0",
                 dev, dev, ifnum, n, n);
    else
        snprintf(rel, sizeof rel, "bus/usb/devices/%s/%s:1.%u/sound/card%d",
                 dev, dev, ifnum, n);
    mkdirs(fx, rel);
}

static void test_find_card(void)
{
    fixture fx;
    int c = -7;

    fx_init(&fx);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_NOT_YET);
    fx_ff802_cc(&fx, 19);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_NOT_YET);
    card(&fx, PORT, 0, 1, 0);           /* card without MIDI device yet */
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_NOT_YET);
    CHECK(c == -7);
    card(&fx, PORT, 0, 1, 1);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_FOUND);
    CHECK(c == 1);
    fx_done(&fx);

    /* the card may hang below any interface; numbers above 9 */
    fx_init(&fx);
    fx_ff802_cc(&fx, 19);
    card(&fx, PORT, 3, 12, 1);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_FOUND);
    CHECK(c == 12);
    /* a second card with MIDI on the same connection: refused */
    card(&fx, PORT, 0, 2, 1);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_AMBIGUOUS);
    fx_done(&fx);

    /* two units: each connection finds its own card */
    fx_init(&fx);
    fx_ff802_cc(&fx, 19);
    fx_dev(&fx, "3-2.4.1.1.4", "0424", "3fdd", 3, 20, "480", "RME", "Fireface 802 (1)");
    card(&fx, PORT, 0, 1, 1);
    card(&fx, "3-2.4.1.1.4", 0, 2, 1);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_FOUND && c == 1);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, "3-2.4.1.1.4", &c) == FF802_MPORT_FOUND && c == 2);
    /* a hub port further down is a different device, not an interface */
    fx_dev(&fx, "3-2.4.1.1.3.1", "0424", "3fdd", 3, 21, "480", "RME", "x");
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_FOUND && c == 1);
    fx_done(&fx);

    /* malformed names, odd entries */
    fx_init(&fx);
    fx_ff802_cc(&fx, 19);
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":1.0/sound/card01/midiC01D0");
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":2.0/sound/card3/midiC3D0");
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":1.x/sound/card4/midiC4D0");
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":1.0/sound/cardX/midiCXD0");
    mkdirs(&fx, "bus/usb/devices/" PORT "/" PORT ":1.0/sound/card999/midiC999D0");
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, PORT, &c) == FF802_MPORT_NOT_YET);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, "../etc", &c) == FF802_MPORT_BAD);
    CHECK(ff802_mixer_find_card(fx.paths.sysfs_root, NULL, &c) == FF802_MPORT_BAD);
    CHECK(ff802_mixer_find_card(NULL, PORT, &c) == FF802_MPORT_BAD);
    fx_done(&fx);
}

/* Listing of the unit as seen in phase 2a (aconnect -l), card 1. */
static const ff802_seq_port HOST[] = {
    { 0, 0, 1, -1, 0, "Timer" },
    { 0, 1, 1, -1, 0, "Announce" },
    { 14, 0, 1, -1, 1, "Midi Through Port-0" },
    { 20, 0, 1, 1, 1, "Fireface 802 (24047128) Port 1" },
    { 20, 1, 1, 1, 1, "Fireface 802 (24047128) Port 2" },
    { 144, 0, 0, -1, 1, "input" },
};

static void test_select(void)
{
    size_t idx = 99;
    size_t n = sizeof HOST / sizeof HOST[0];

    CHECK(ff802_mixer_select_port(HOST, n, 1, &idx) == FF802_SEQSEL_FOUND);
    CHECK(idx == 4 && HOST[idx].client == 20 && HOST[idx].port == 1);
    /* another card number: not ours */
    CHECK(ff802_mixer_select_port(HOST, n, 2, &idx) == FF802_SEQSEL_NO_CLIENT);
    CHECK(ff802_mixer_select_port(HOST, 0, 1, &idx) == FF802_SEQSEL_NO_CLIENT);
    CHECK(ff802_mixer_select_port(HOST, n, -1, &idx) == FF802_SEQSEL_NO_CLIENT);

    /* client number changed after re-detection: found by card */
    ff802_seq_port l[8];
    memcpy(l, HOST, sizeof HOST);
    l[3].client = 24; l[4].client = 24;
    CHECK(ff802_mixer_select_port(l, n, 1, &idx) == FF802_SEQSEL_FOUND && l[idx].client == 24);

    /* only port 0 so far */
    CHECK(ff802_mixer_select_port(HOST, 4, 1, &idx) == FF802_SEQSEL_NO_PORT);

    /* a user client claiming the card does not count */
    memcpy(l, HOST, sizeof HOST);
    l[5].card = 1;
    CHECK(ff802_mixer_select_port(l, n, 1, &idx) == FF802_SEQSEL_FOUND && idx == 4);

    /* two kernel clients of the same card: refused */
    memcpy(l, HOST, sizeof HOST);
    l[6] = (ff802_seq_port){ 21, 1, 1, 1, 1, "Other Port 2" };
    CHECK(ff802_mixer_select_port(l, n + 1, 1, &idx) == FF802_SEQSEL_AMBIGUOUS);

    /* index and name must agree */
    memcpy(l, HOST, sizeof HOST);
    snprintf(l[4].name, sizeof l[4].name, "Fireface 802 (24047128) Port 1");
    CHECK(ff802_mixer_select_port(l, n, 1, &idx) == FF802_SEQSEL_MISMATCH);
    memcpy(l, HOST, sizeof HOST);
    snprintf(l[4].name, sizeof l[4].name, "Fireface 802 (24047128) Port 22");
    CHECK(ff802_mixer_select_port(l, n, 1, &idx) == FF802_SEQSEL_MISMATCH);
    memcpy(l, HOST, sizeof HOST);
    l[4].writable = 0;
    CHECK(ff802_mixer_select_port(l, n, 1, &idx) == FF802_SEQSEL_MISMATCH);

    CHECK(strcmp(ff802_seqsel_str(FF802_SEQSEL_MISMATCH), "port 1 is not a writable 'Port 2'") == 0);
    CHECK(strcmp(ff802_mport_status_str(FF802_MPORT_FOUND), "found") == 0);
}

static void test_find_unit(void)
{
    fixture fx;
    ff802_sysfs_dev d;
    unsigned n = 99;

    fx_init(&fx);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, NULL, &d, &n) == FF802_UNIT_NONE && n == 0);
    fx_ff802_prop(&fx, 18);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, NULL, &d, &n) == FF802_UNIT_NONE);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, PORT, &d, &n) == FF802_UNIT_NOT_CC);
    fx_ff802_cc(&fx, 19);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, NULL, &d, &n) == FF802_UNIT_FOUND && n == 1);
    CHECK(strcmp(d.kernel_name, PORT) == 0 && d.devnum == 19);
    /* other devices with the CC ID but no Fireface identity do not count */
    fx_dev(&fx, "1-4", "0424", "3fdd", 1, 3, "480", "Microchip", "Fireface 802 (1)");
    fx_dev(&fx, "1-5", "0424", "3fdd", 1, 4, "480", "RME", "Fireface 802 FS (1)");
    fx_dev(&fx, "1-6", "0424", "3fdd", 1, 5, "12", "RME", "Fireface 802 (2)");
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, NULL, &d, &n) == FF802_UNIT_FOUND && n == 1);
    /* two units: never chosen silently, but each can be named */
    fx_dev(&fx, "3-2.4.1.1.4", "0424", "3fdd", 3, 20, "480", "RME", "Fireface 802 (12345678)");
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, NULL, &d, &n) == FF802_UNIT_MULTIPLE && n == 2);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, "3-2.4.1.1.4", &d, &n) == FF802_UNIT_FOUND);
    CHECK(d.devnum == 20);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, "1-4", &d, &n) == FF802_UNIT_NOT_CC);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, "9-9", &d, &n) == FF802_UNIT_NONE);
    CHECK(ff802_mixer_find_unit(fx.paths.sysfs_root, "../x", &d, &n) == FF802_UNIT_BAD);
    CHECK(strstr(ff802_unit_status_str(FF802_UNIT_MULTIPLE), "--connection") != NULL);
    fx_done(&fx);
}

static void test_lock_existing(void)
{
    fixture fx;
    fx_init(&fx);
    char cmd[512];
    snprintf(cmd, sizeof cmd, "rm -rf -- '%s'", fx.paths.run_dir);
    if (system(cmd) != 0)
        exit(2);
    CHECK(ff802_auto_lock_existing(&fx.paths) == -2);     /* service never ran */
    int a = ff802_auto_lock(&fx.paths);                   /* the service's lock */
    CHECK(a >= 0);
    ff802_auto_unlock(a);
    int b = ff802_auto_lock_existing(&fx.paths);
    CHECK(b >= 0);
    ff802_auto_unlock(b);
    fx_done(&fx);
}

int main(void)
{
    test_find_unit();
    test_lock_existing();
    test_find_card();
    test_select();
    TEST_EXIT();
}
