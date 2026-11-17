/*
 * Card identification from a fake /proc/asound: exactly one original
 * Fireface 802 is accepted; the 802 FS, other cards and ambiguous setups
 * are refused. Also the control device identity check.
 *
 * Usage: test_chanmap_card <scratch dir>
 */
#define _GNU_SOURCE
#include "../test_common.h"
#include "ff802/chanmap.h"

#include <stdlib.h>
#include <sys/stat.h>

static char root[512];
static int seq;

static void put(const char *rel, const char *text)
{
    char path[1024];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", root, rel);
    f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(2);
    }
    fputs(text, f);
    fclose(f);
}

static void mk(const char *rel)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    mkdir(path, 0755);
}

static void new_root(const char *base)
{
    snprintf(root, sizeof root, "%s/proc%d", base, seq++);
    mkdir(root, 0755);
}

static void add_card(int n, const char *id, const char *usbid, const char *longname)
{
    char rel[64];
    snprintf(rel, sizeof rel, "card%d", n);
    mk(rel);
    if (usbid) {
        snprintf(rel, sizeof rel, "card%d/usbid", n);
        put(rel, usbid);
    }
    snprintf(rel, sizeof rel, "card%d/id", n);
    put(rel, id);
    (void)longname;
}

int main(int argc, char **argv)
{
    struct chanmap_card c;
    char why[256];
    const char *base = argc > 1 ? argv[1] : ".";

    mkdir(base, 0755);

    /* no cards at all */
    new_root(base);
    put("cards", "--- no soundcards ---\n");
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == 0);
    CHECK(strstr(why, "0424:3fdd") != NULL);

    /* the host setup of T1: laptop card 0, Fireface 802 card 1 */
    new_root(base);
    put("cards",
        " 0 [sofsoundwire   ]: sof-soundwire - sof-soundwire\n"
        "                      DellInc.-XPS179730-0C4XC6\n"
        " 1 [F80224047128   ]: USB-Audio - Fireface 802 (24047128)\n"
        "                      RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed\n");
    add_card(0, "sofsoundwire\n", NULL, NULL);
    add_card(1, "F80224047128\n", "0424:3fdd\n", NULL);
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == 1);
    CHECK(c.index == 1);
    CHECK(strcmp(c.id, "F80224047128") == 0);
    CHECK(strcmp(c.longname, "RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed") == 0);

    /* the short name alone does not count: the long name must match */
    new_root(base);
    put("cards",
        " 2 [F802          ]: USB-Audio - Fireface 802 (1)\n"
        "                      RME Fireface 802 FS (1) at usb-0000:00:14.0-2, high speed\n");
    add_card(2, "F802\n", "0424:3fdd\n", NULL);
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == 0);
    CHECK(strstr(why, "not an original Fireface 802") != NULL);

    /* another USB device with a different id */
    new_root(base);
    put("cards",
        " 0 [UCX            ]: USB-Audio - Fireface UCX\n"
        "                      RME Fireface UCX (23596862) at usb-0000:00:14.0-1, high speed\n");
    add_card(0, "UCX\n", "0424:3fb9\n", NULL);
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == 0);

    /* two original 802: refused */
    new_root(base);
    put("cards",
        " 0 [A              ]: USB-Audio - Fireface 802 (1)\n"
        "                      RME Fireface 802 (1) at usb-1, high speed\n"
        " 1 [B              ]: USB-Audio - Fireface 802 (2)\n"
        "                      RME Fireface 802 (2) at usb-2, high speed\n");
    add_card(0, "A\n", "0424:3fdd\n", NULL);
    add_card(1, "B\n", "0424:3fdd\n", NULL);
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == -2);
    CHECK(strstr(why, "refusing") != NULL);

    /* long name missing in cards: refused */
    new_root(base);
    put("cards", " 0 [X              ]: other - other\n                      other\n");
    add_card(3, "F802\n", "0424:3fdd\n", NULL);
    CHECK(chanmap_find_card(root, &c, why, sizeof why) == 0);

    /* unreadable proc root */
    CHECK(chanmap_find_card("/nonexistent/asound", &c, why, sizeof why) == -1);

    /* control device identity */
    CHECK(chanmap_card_info_ok("USB-Audio", "USB0424:3fdd",
                               "RME Fireface 802 (24047128) at usb-0000:00:14.0-2.4.1.1.3, high speed"));
    CHECK(!chanmap_card_info_ok("USB-Audio", "USB0424:3fdd", "RME Fireface 802 FS (1) at usb-1"));
    CHECK(!chanmap_card_info_ok("USB-Audio", "USB0424:3fb9", "RME Fireface 802 (1) at usb-1"));
    CHECK(!chanmap_card_info_ok("HDA-Intel", "USB0424:3fdd", "RME Fireface 802 (1) at usb-1"));
    CHECK(!chanmap_card_info_ok(NULL, "USB0424:3fdd", "RME Fireface 802 (1)"));

    TEST_EXIT();
}
