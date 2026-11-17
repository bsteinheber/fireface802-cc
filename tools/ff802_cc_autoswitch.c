/*
 * ff802-cc-autoswitch: non-interactive CC enable for ONE USB connection,
 * started only by ff802-cc-auto@.service (T4).
 *
 *   ff802-cc-autoswitch --service <kernel name>:<busnum>:<devnum>
 *   ff802-cc-autoswitch --status
 *   ff802-cc-autoswitch --help | --version
 *
 * --service refuses to run unless started by systemd (INVOCATION_ID) as
 * root; the automation must be on (ff802-cc-auto on). It is installed in
 * /usr/libexec, not in PATH. For manual use there is ff802-cc enable
 * with its interactive confirmation.
 *
 * --status is read only: sysfs, state and run files. No USB access.
 *
 * Built twice from this file: the installed program (libusb backend) and,
 * with FF802_AUTO_SIM, a test program with a simulated backend that does
 * not link libusb and works only below $FF802_AUTO_ROOT.
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ff802/auto_state.h"
#include "ff802/auto_switch.h"
#include "ff802/auto_sysfs.h"

#ifdef FF802_AUTO_SIM
#include "sim_usb.h"
#define WRITES_ENABLED() 1
#else
#include "ff802/usb_comm.h"
#define WRITES_ENABLED() ff802_usb_comm_write_enabled()
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-cc-autoswitch --service <kernel name>:<busnum>:<devnum>\n"
        "       ff802-cc-autoswitch --status\n"
        "       ff802-cc-autoswitch --help | --version\n"
        "\n"
        "Internal program of ff802-cc-auto (T4), started by\n"
        "ff802-cc-auto@.service for one USB connection of an original\n"
        "RME Fireface 802. Use 'ff802-cc-auto status' instead.\n"
        "\n"
        "  --service  handle one connection: if the automation is on and the\n"
        "             device is a Fireface 802 in proprietary mode, send the\n"
        "             class-compliant enable command once. Only when started\n"
        "             by systemd as root.\n"
        "  --status   read-only report (sysfs, state); no USB access.\n"
        "\n"
        "Exit status (--service): 0 off/handled/CC mode/sent, 2 usage or not\n"
        "started by systemd, 3 nothing sent (refused or failed before the\n"
        "write), 4 write failed (device state unknown).\n");
}

static int get_paths(ff802_auto_paths *p)
{
#ifdef FF802_AUTO_SIM
    const char *root = getenv("FF802_AUTO_ROOT");
    if (root == NULL || ff802_auto_paths_below(p, root) != 0) {
        fprintf(stderr, "ff802-cc-autoswitch (sim): FF802_AUTO_ROOT must be "
                        "an absolute path\n");
        return -1;
    }
#else
    ff802_auto_default_paths(p);
#endif
    return 0;
}

/* --- backend factory ------------------------------------------------------ */
#ifdef FF802_AUTO_SIM
static ff802_usb_err fac_open(void *ctx, ff802_usb_backend *be)
{
    return sim_usb_open((const ff802_auto_paths *)ctx, be);
}
static void fac_close(void *ctx, ff802_usb_backend *be)
{
    (void)ctx;
    sim_usb_close(be);
}
#else
static ff802_usb_err fac_open(void *ctx, ff802_usb_backend *be)
{
    (void)ctx;
    return ff802_usb_comm_init(be);
}
static void fac_close(void *ctx, ff802_usb_backend *be)
{
    (void)ctx;
    ff802_usb_comm_exit(be);
}
#endif

/* --- --status ------------------------------------------------------------- */
static void print_file(const char *dir, const char *name, const char *title)
{
    char path[FF802_AUTO_PATH_MAX + 32u];
    char line[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        printf("%s: none\n", title);
        return;
    }
    printf("%s:\n", title);
    while (fgets(line, sizeof line, f) != NULL)
        printf("  %s", line);
    fclose(f);
}

static int handled_before(const ff802_auto_paths *p, const ff802_sysfs_dev *d)
{
    char path[FF802_AUTO_PATH_MAX + 64u];
    snprintf(path, sizeof path, "%s/attempts/%s_%u_%u", p->run_dir,
             d->kernel_name, (unsigned)d->bus, (unsigned)d->devnum);
    return access(path, F_OK) == 0;
}

static int run_status(const ff802_auto_paths *p)
{
    char dir[FF802_AUTO_PATH_MAX + 32u];
    ff802_auto_mode mode = ff802_auto_read_mode(p);
    unsigned found = 0;

    printf("automation: %s (%s/mode)\n", ff802_auto_mode_str(mode), p->state_dir);
    printf("USB writes in this build: %s\n", WRITES_ENABLED() ? "enabled" : "disabled");
    snprintf(dir, sizeof dir, "%s/bus/usb/devices", p->sysfs_root);
    DIR *dh = opendir(dir);
    if (dh == NULL) {
        printf("cannot read %s\n", dir);
        return 1;
    }
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        ff802_sysfs_dev d;
        if (!ff802_sysfs_valid_name(de->d_name) ||
            ff802_sysfs_read(p->sysfs_root, de->d_name, &d) != FF802_SYSFS_OK)
            continue;
        if (!((d.vid == 0x2A39u && d.pid == 0x3FCDu) ||
              (d.vid == 0x0424u && d.pid == 0x3FDDu)))
            continue;
        found++;
        ff802_identity id = ff802_sysfs_identify(&d);
        printf("device %s: bus %u devnum %u ID %04x:%04x manufacturer '%s' "
               "product '%s' speed %s\n  identity: %s\n", d.kernel_name,
               (unsigned)d.bus, (unsigned)d.devnum, (unsigned)d.vid,
               (unsigned)d.pid, d.manufacturer, d.product, d.speed,
               ff802_identity_str(id));
        if (id == FF802_ID_PROPRIETARY)
            printf("  automation: %s\n",
                   mode != FF802_AUTO_MODE_ON ? "off, nothing will be sent" :
                   handled_before(p, &d) ? "this connection was handled already "
                                           "(reconnect for a new attempt)" :
                   "would send the CC enable command on the next connection event");
        else if (id == FF802_ID_CLASS_COMPLIANT)
            printf("  automation: nothing to do (class-compliant mode)\n");
        else
            printf("  automation: refused (not identified as an original "
                   "Fireface 802)\n");
    }
    closedir(dh);
    if (found == 0)
        printf("device: no Fireface 802 connected (2a39:3fcd or 0424:3fdd)\n");
    print_file(p->run_dir, "pending", "pending switch (reconnect needed)");
    print_file(p->run_dir, "last-result", "last result");
    print_file(p->run_dir, "event", "last notification event");
    return 0;
}

int main(int argc, char **argv)
{
    ff802_auto_paths paths;

    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout);
        return 0;
    }
    if (argc == 2 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-V") == 0)) {
        printf("ff802-cc-autoswitch %s (USB writes %s%s)\n", FF802_VERSION,
               WRITES_ENABLED() ? "enabled" : "disabled",
#ifdef FF802_AUTO_SIM
               ", SIMULATED backend"
#else
               ""
#endif
               );
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--status") == 0) {
        if (get_paths(&paths) != 0)
            return FF802_AUTO_EXIT_USAGE;
        return run_status(&paths);
    }
    if (argc != 3 || strcmp(argv[1], "--service") != 0) {
        usage(stderr);
        return FF802_AUTO_EXIT_USAGE;
    }

    /* Guard against unintended use: only as the systemd service. */
    const char *inv = getenv("INVOCATION_ID");
    if (inv == NULL || inv[0] == '\0') {
        fprintf(stderr, "ff802-cc-autoswitch: --service is only started by "
                        "ff802-cc-auto@.service (systemd); nothing done. For "
                        "manual use: ff802-cc enable.\n");
        return FF802_AUTO_EXIT_USAGE;
    }
#ifndef FF802_AUTO_SIM
    if (geteuid() != 0) {
        fprintf(stderr, "ff802-cc-autoswitch: --service runs as the system "
                        "service only; nothing done.\n");
        return FF802_AUTO_EXIT_USAGE;
    }
#endif

    ff802_auto_event ev;
    if (ff802_auto_parse_instance(argv[2], &ev) != 0) {
        fprintf(stderr, "ff802-cc-autoswitch: invalid connection '%s' "
                        "(expected <kernel name>:<busnum>:<devnum>); nothing done.\n",
                argv[2]);
        return FF802_AUTO_EXIT_USAGE;
    }
    if (get_paths(&paths) != 0)
        return FF802_AUTO_EXIT_USAGE;

    ff802_auto_backend_factory fac = { .ctx = &paths, .open = fac_open,
                                       .close = fac_close };
    ff802_auto_result res;
    ff802_auto_outcome o = ff802_auto_handle(&paths, &ev, &fac, stderr, &res);
    return ff802_auto_exit_code(o);
}
