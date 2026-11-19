/*
 * ff802-cc: prepare the switch of an original RME Fireface 802 from
 * proprietary USB mode to class-compliant mode.
 *
 *   ff802-cc check    diagnose only: never claims the interface or sends data
 *   ff802-cc enable   send the CC enable command once, after the user types
 *                     the confirmation word on an interactive terminal
 *   ff802-cc disable  switch a unit in class-compliant mode back to
 *                     proprietary mode (USB-MIDI interface 3, as tested on
 *                     2026-11-19), after the user types the confirmation word
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ff802/auto_sysfs.h"
#include "ff802/cc_disable.h"
#include "ff802/cc_mode.h"
#include "ff802/cc_switch.h"
#include "ff802/detect.h"
#include "ff802/disable_check.h"
#include "ff802/usb_comm.h"
#include "ff802/usb_port.h"

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-cc [--device BUS:ADDRESS] check\n"
        "       ff802-cc [--device BUS:ADDRESS] enable\n"
        "       ff802-cc [--device BUS:ADDRESS] disable\n"
        "       ff802-cc --help | --version\n"
        "\n"
        "Commands:\n"
        "  check   Verify that an original Fireface 802 in proprietary mode is\n"
        "          present, that its USB descriptors match, that it can be\n"
        "          opened and that no kernel driver uses interface 1.\n"
        "          Never claims the interface and never sends data.\n"
        "  enable  Run the same checks, then ask for confirmation and send the\n"
        "          'enable class-compliant mode' command (01 00 44 BD) once to\n"
        "          bulk OUT endpoint 0x0C. The mode is expected to change only\n"
        "          after the next USB disconnect or power cycle.\n"
        "  disable Switch a unit in class-compliant mode (0424:3fdd) back to\n"
        "          proprietary mode: checks the unit (exactly one original\n"
        "          Fireface 802, interface 3 = USB-MIDI with bulk OUT 0x07, bound\n"
        "          to snd-usb-audio), refuses while ff802-cc-auto is on or audio\n"
        "          is running, asks for confirmation and sends the 16-byte\n"
        "          command once (libusb detaches snd-usb-audio from interface 3;\n"
        "          the ALSA card disappears). Then waits for 2a39:3fcd. Needs\n"
        "          root (sudo).\n"
        "\n"
        "Options:\n"
        "  --device BUS:ADDRESS  select a device as listed by ff802-detect\n"
        "                        (required if several are connected)\n"
        "\n"
        "Exit status:\n"
        "  0 success, 1 no suitable device, 2 usage error,\n"
        "  3 precondition failed (nothing sent), 4 transfer failed,\n"
        "  5 not sent (aborted or writes disabled in this build),\n"
        "  6 transferred, but the new mode was not detected\n");
}


static void print_bytes(FILE *out, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        fprintf(out, "%s%02X", i ? " " : "", b[i]);
}

/* Interactive confirmation on the controlling terminal. */
static int confirm_on_tty(void *user, const ff802_usb_dev *target,
                          const uint8_t cmd[FF802_REG_WORD_SIZE])
{
    char line[64];
    char desc[160];
    (void)user;

    ff802_format_device(target, ff802_classify(target->vid, target->pid),
                        desc, sizeof desc);
    printf("\nAbout to send the class-compliant enable command to:\n"
           "  %s\n"
           "  interface %u, bulk OUT endpoint 0x%02X, %u bytes: ",
           desc, (unsigned)FF802_USB_INTERFACE, (unsigned)FF802_USB_EP_OUT,
           (unsigned)FF802_REG_WORD_SIZE);
    print_bytes(stdout, cmd, FF802_REG_WORD_SIZE);
    printf("\n\nThe device is expected to switch only after the next USB\n"
           "disconnect or power cycle. This has not been verified on Linux.\n"
           "The command is sent exactly once and is not retried.\n\n"
           "Type %s to send the command, anything else aborts: ",
           FF802_CONFIRM_WORD);
    fflush(stdout);

    if (fgets(line, sizeof line, stdin) == NULL)
        return 0;
    return ff802_confirm_answer_ok(line);
}

static void print_result(const ff802_sw_options *opt,
                         const ff802_sw_result *res)
{
    if (res->status == FF802_SW_OK) {
        if (opt->action == FF802_SW_CHECK) {
            uint8_t cmd[FF802_REG_WORD_SIZE];
            ff802_cc_command(true, cmd);
            printf("\nCheck passed. 'enable' would send: ");
            print_bytes(stdout, cmd, sizeof cmd);
            printf("\nUSB writes in this build: %s\n",
                   ff802_usb_comm_write_enabled() ? "enabled" : "disabled");
        } else {
            printf("\nCommand transferred (%zu bytes).\n"
                   "Next: disconnect the device (or power-cycle it), reconnect\n"
                   "and run ff802-detect. In class-compliant mode it is\n"
                   "expected to appear as 0424:3fdd.\n", res->transferred);
        }
        if (res->release_failed)
            fprintf(stderr, "Warning: releasing interface 1 failed: %s\n",
                    ff802_usb_err_str(res->release_err));
        return;
    }

    fflush(stdout);   /* keep progress log and error in order */
    fprintf(stderr, "\nff802-cc: %s", ff802_sw_status_str(res->status));
    if (res->status == FF802_SW_UNEXPECTED_LAYOUT)
        fprintf(stderr, ": %s", ff802_layout_status_str(res->layout));
    else if (res->usb_err != FF802_USB_OK)
        fprintf(stderr, " (%s)", ff802_usb_err_str(res->usb_err));
    fprintf(stderr, "\n");
    if (res->write_attempted)
        fprintf(stderr, "Bytes reported as transferred: %zu of %u\n",
                res->transferred, (unsigned)FF802_REG_WORD_SIZE);
    const char *hint = ff802_sw_status_hint(res->status);
    if (res->status == FF802_SW_DISCONNECTED &&
        res->failed_step == FF802_STEP_OPEN)
        hint = "The device node could not be opened: either the device was "
               "unplugged, or /dev/bus/usb is not available (e.g. inside a "
               "container without USB access).";
    if (hint != NULL)
        fprintf(stderr, "%s\n", hint);
    if (res->release_failed)
        fprintf(stderr, "Warning: releasing interface 1 failed: %s\n",
                ff802_usb_err_str(res->release_err));
}

/* --- disable: class-compliant -> proprietary mode --------------------------
 * Reproduces the transfer of the hardware test 2026-11-19 (interface 3,
 * bulk OUT 0x07, 16 bytes, libusb auto-detach of snd-usb-audio, timeout
 * 3000 ms, once). Needs root (sudo), like the tested script. */

#define DISABLE_WAIT_MS 10000u

typedef struct dis_ctx {
    const ff802_dchk_roots  *roots;
    const ff802_dchk_target *target;
} dis_ctx;

static int confirm_disable_tty(void *user, const ff802_usb_dev *target,
                               const uint8_t *data, size_t len)
{
    dis_ctx *c = user;
    char line[64], desc[160], why[256];
    uint8_t want[FF802_CC_DISABLE_MIDI_SIZE];

    if (len != sizeof want || ff802_cc_disable_midi(want) != FF802_OK ||
        memcmp(data, want, sizeof want) != 0) {
        fprintf(stderr, "ff802-cc: unexpected command bytes; nothing sent\n");
        return 0;
    }
    ff802_format_device(target, ff802_classify(target->vid, target->pid),
                        desc, sizeof desc);
    printf("\nAbout to send the 'leave class-compliant mode' command to:\n"
           "  %s\n"
           "  interface %u, bulk OUT endpoint 0x%02X, %zu bytes: ",
           desc, (unsigned)FF802_DIS_INTERFACE, (unsigned)FF802_DIS_EP_OUT, len);
    print_bytes(stdout, data, len);
    printf("\n\nlibusb first detaches snd-usb-audio from interface 3: the ALSA card\n"
           "of the Fireface (all outputs and the input) disappears at once. The\n"
           "unit is then expected to switch to proprietary mode (2a39:3fcd)\n"
           "without a USB reset. The command is sent exactly once and is not\n"
           "retried.\n\n"
           "Type %s to send the command, anything else aborts: ",
           FF802_DISABLE_CONFIRM_WORD);
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL ||
        !ff802_disable_confirm_answer_ok(line))
        return 0;

    /* Everything again after the confirmation, right before the claim. */
    ff802_dchk_target now;
    ff802_dchk_status st = ff802_disable_check(c->roots, 1, c->target->dev.bus,
                                               c->target->dev.devnum, &now,
                                               why, sizeof why);
    if (st != FF802_DCHK_OK) {
        fprintf(stderr, "ff802-cc: %s%s%s; nothing sent\n", ff802_dchk_status_str(st),
                why[0] ? ": " : "", why);
        return 0;
    }
    if (!ff802_sysfs_same_connection(&now.dev, &c->target->dev) ||
        !ff802_sysfs_matches_usb_dev(&now.dev, target)) {
        fprintf(stderr, "ff802-cc: the device changed after the check; nothing sent\n");
        return 0;
    }
    return 1;
}

static void print_disable_result(const ff802_sw_result *res)
{
    if (res->status == FF802_SW_OK) {
        printf("\nCommand transferred (%zu bytes).\n", res->transferred);
        if (res->release_failed)
            printf("Note: releasing interface 3 reported '%s' (expected once the "
                   "unit has left class-compliant mode).\n",
                   ff802_usb_err_str(res->release_err));
        return;
    }
    fflush(stdout);
    fprintf(stderr, "\nff802-cc: %s", ff802_disable_status_str(res->status));
    if (res->status == FF802_SW_UNEXPECTED_LAYOUT)
        fprintf(stderr, ": %s", ff802_disable_layout_str(res->layout));
    else if (res->usb_err != FF802_USB_OK)
        fprintf(stderr, " (%s)", ff802_usb_err_str(res->usb_err));
    fprintf(stderr, "\n");
    if (res->write_attempted)
        fprintf(stderr, "Bytes reported as transferred: %zu of %u\n",
                res->transferred, (unsigned)FF802_CC_DISABLE_MIDI_SIZE);
    const char *hint = ff802_sw_status_hint(res->status);
    if (res->status == FF802_SW_ACCESS_DENIED)
        hint = "Run it with sudo, as the tested procedure did.";
    else if (res->status == FF802_SW_NOT_FOUND)
        hint = "Check with ff802-detect.";
    if (hint != NULL)
        fprintf(stderr, "%s\n", hint);
    if (res->release_failed)
        fprintf(stderr, "Releasing interface 3 failed: %s\n",
                ff802_usb_err_str(res->release_err));
}

static int cmd_disable(const ff802_sw_options *opt)
{
    ff802_dchk_roots roots;
    ff802_dchk_target t;
    char why[256];

    /* Refuse before touching USB if no human can confirm. */
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "ff802-cc: 'disable' requires an interactive "
                        "terminal for confirmation.\n");
        return FF802_CC_EXIT_USAGE;
    }
    ff802_dchk_default_roots(&roots);
    ff802_dchk_status st = ff802_disable_check(&roots, opt->select_location, opt->bus,
                                               opt->address, &t, why, sizeof why);
    if (st != FF802_DCHK_OK) {
        fprintf(stderr, "ff802-cc: %s%s%s\nNothing was sent.\n", ff802_dchk_status_str(st),
                why[0] ? ": " : "", why);
        return (st == FF802_DCHK_NOT_FOUND || st == FF802_DCHK_AMBIGUOUS)
                   ? FF802_CC_EXIT_NOT_FOUND : FF802_CC_EXIT_CHECK_FAILED;
    }
    printf("Fireface 802 in class-compliant mode at %s (bus %03u, device %03u), "
           "ALSA card %d; interface 3 bound to snd-usb-audio; automation off.\n",
           t.dev.kernel_name, (unsigned)t.dev.bus, (unsigned)t.dev.devnum, t.card);

    ff802_usb_backend be;
    ff802_usb_err e = ff802_usb_comm_init(&be);
    if (e != FF802_USB_OK) {
        fprintf(stderr, "ff802-cc: libusb initialisation failed: %s\n",
                ff802_usb_err_str(e));
        return FF802_CC_EXIT_CHECK_FAILED;
    }
    ff802_usb_comm_allow_detach_claim(&be);
    dis_ctx ctx = { .roots = &roots, .target = &t };
    ff802_dis_options dopt = { .select_location = 1, .bus = t.dev.bus,
                               .address = t.dev.devnum,
                               .confirm = confirm_disable_tty, .confirm_user = &ctx };
    ff802_sw_result res;
    ff802_disable_run(&be, &dopt, stdout, &res);
    ff802_usb_comm_exit(&be);
    print_disable_result(&res);
    if (res.status != FF802_SW_OK)
        return ff802_sw_exit_code(res.status);

    printf("Waiting up to %u s for proprietary mode (2a39:3fcd) at %s ...\n",
           DISABLE_WAIT_MS / 1000u, t.dev.kernel_name);
    fflush(stdout);
    ff802_sysfs_dev found;
    ff802_wait_status ws = ff802_port_wait_mode(roots.paths.sysfs_root, t.dev.kernel_name,
                                                FF802_MODE_PROPRIETARY, DISABLE_WAIT_MS,
                                                NULL, &found);
    if (ws == FF802_WAIT_OK) {
        printf("Switched: %s is now %04x:%04x (bus %03u, device %03u), proprietary mode.\n",
               found.kernel_name, (unsigned)found.vid, (unsigned)found.pid,
               (unsigned)found.bus, (unsigned)found.devnum);
        return FF802_CC_EXIT_OK;
    }
    if (ws == FF802_WAIT_OTHER)
        fprintf(stderr, "ff802-cc: after %u s %s is %04x:%04x, not 2a39:3fcd.\n",
                DISABLE_WAIT_MS / 1000u, t.dev.kernel_name, (unsigned)found.vid,
                (unsigned)found.pid);
    else
        fprintf(stderr, "ff802-cc: no device at %s after %u s.\n", t.dev.kernel_name,
                DISABLE_WAIT_MS / 1000u);
    fprintf(stderr, "The command was transferred, but the switch was not confirmed. "
                    "Check with ff802-detect; nothing was retried.\n");
    return FF802_CC_EXIT_SWITCH_UNCONFIRMED;
}

int main(int argc, char **argv)
{
    ff802_sw_options opt;
    const char *command = NULL;

    memset(&opt, 0, sizeof opt);

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return FF802_CC_EXIT_OK;
        }
        if (strcmp(a, "-V") == 0 || strcmp(a, "--version") == 0) {
            printf("ff802-cc %s (USB writes %s)\n", FF802_VERSION,
                   ff802_usb_comm_write_enabled() ? "enabled" : "disabled");
            return FF802_CC_EXIT_OK;
        }
        if (strcmp(a, "--device") == 0) {
            if (i + 1 >= argc ||
                ff802_parse_location(argv[i + 1], &opt.bus, &opt.address) != 0) {
                fprintf(stderr, "ff802-cc: --device expects BUS:ADDRESS, "
                                "e.g. 3:18\n");
                return FF802_CC_EXIT_USAGE;
            }
            opt.select_location = 1;
            i++;
            continue;
        }
        if (command == NULL && (strcmp(a, "check") == 0 ||
                                strcmp(a, "enable") == 0 ||
                                strcmp(a, "disable") == 0)) {
            command = a;
            continue;
        }
        fprintf(stderr, "ff802-cc: unexpected argument '%s'\n\n", a);
        usage(stderr);
        return FF802_CC_EXIT_USAGE;
    }

    if (command == NULL) {
        usage(stderr);
        return FF802_CC_EXIT_USAGE;
    }

    if (strcmp(command, "disable") == 0)
        return cmd_disable(&opt);

    if (strcmp(command, "enable") == 0) {
        /* Refuse before touching USB if no human can confirm. */
        if (!isatty(STDIN_FILENO)) {
            fprintf(stderr, "ff802-cc: 'enable' requires an interactive "
                            "terminal for confirmation.\n");
            return FF802_CC_EXIT_USAGE;
        }
        opt.action = FF802_SW_ENABLE_CC;
        opt.confirm = confirm_on_tty;
    } else {
        opt.action = FF802_SW_CHECK;
    }

    ff802_usb_backend be;
    ff802_usb_err e = ff802_usb_comm_init(&be);
    if (e != FF802_USB_OK) {
        fprintf(stderr, "ff802-cc: libusb initialisation failed: %s\n",
                ff802_usb_err_str(e));
        return FF802_CC_EXIT_CHECK_FAILED;
    }

    ff802_sw_result res;
    ff802_switch_run(&be, &opt, stdout, &res);
    ff802_usb_comm_exit(&be);

    print_result(&opt, &res);
    return ff802_sw_exit_code(res.status);
}
