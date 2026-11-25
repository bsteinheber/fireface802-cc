/*
 * ff802-mixer-init: manual mixer initialisation of the original RME
 * Fireface 802 in class-compliant mode - the same 168 register writes
 * (ff802-mixer-init-v4) and
 * the same flow (ff802_mixer_run) as the automatic initialisation of
 * ff802-cc-auto@.service, started by hand.
 *
 *   ff802-mixer-init --list
 *   ff802-mixer-init --dry-run [--connection <kernel name>]
 *   ff802-mixer-init --apply   [--connection <kernel name>]
 *   ff802-mixer-init --help | --version
 *
 * --list prints the 168 messages (no device access). --dry-run finds the
 * unit, its sound card and the MIDI port and sends nothing. --apply does
 * the same, shows what will be sent, asks for SEND on a terminal and sends
 * the 168 messages over one subscription (10 ms apart, 200 ms before
 * unsubscribing). No retry; nothing is read from the unit.
 *
 * The unit is the named connection or the only Fireface 802 in CC mode;
 * several units are never chosen silently, a unit in proprietary mode is
 * never touched. The run takes the lock of ff802-cc-auto (if the service
 * ever ran), so it never overlaps with an automatic initialisation. It
 * does not depend on "ff802-cc-auto mixer-init on": that setting only
 * controls the automatic run.
 *
 * Writes are compiled in only with FF802_ENABLE_MIXER_WRITE (the build of
 * the Debian package); otherwise --apply is refused ("disabled-build").
 * With FF802_MIXER_INIT_SIM the same source is built as a test program
 * with the simulated MIDI backend (no libasound), below $FF802_AUTO_ROOT.
 *
 * Exit status: 0 done / dry run passed, 2 usage, 3 nothing sent (refused,
 * aborted, no port, writes not compiled in), 4 stopped while sending.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ff802/auto_state.h"
#include "ff802/mixer_init.h"
#include "ff802/mixer_port.h"
#include "ff802/mixer_run.h"

#ifdef FF802_MIXER_INIT_SIM
#include "sim_midi.h"
#else
#include "ff802/midi_seq_alsa.h"
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

enum { EXIT_OK = 0, EXIT_USAGE = 2, EXIT_NOT_SENT = 3, EXIT_INCOMPLETE = 4 };

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-mixer-init --list\n"
        "       ff802-mixer-init --dry-run [--connection <kernel name>]\n"
        "       ff802-mixer-init --apply   [--connection <kernel name>]\n"
        "       ff802-mixer-init --help | --version\n"
        "\n"
        "Sets the internal mixer of an original RME Fireface 802 in\n"
        "class-compliant mode to the defined basic state, through the ALSA\n"
        "sequencer port 'Port 2' of the unit (168 register writes,\n"
        "configuration " FF802_MIXER_INIT_ID "):\n"
        "  Playback p -> Hardware Output o, p != o (132)    off (0x0000)\n"
        "  Playback 1..12 -> Hardware Output 1..12          0 dB (0x9000)\n"
        "  Volume of Hardware Output 1..12                  0 dB (0x0000)\n"
        "  Input 9 (Mic/Inst 9) -> Hardware Output 1..12    30 %% (0x2666)\n"
        "(p, o = 1..12: the 12 x 12 playback matrix completely). Nothing\n"
        "else is changed: no Playback 13..30, no Outputs 13..30; other input\n"
        "routings and the input gain are NOT changed.\n"
        "\n"
        "  --list     print the 168 messages; no device access\n"
        "  --dry-run  find the unit, its sound card and MIDI port; send nothing\n"
        "  --apply    the same, then ask for SEND and send the 168 messages\n"
        "\n"
        "WARNING: 0 dB on the headphone outputs Phones 9-12 can be very loud.\n"
        "\n"
        "Exit status: 0 ok, 2 usage, 3 nothing sent, 4 stopped while sending.\n");
}

static void list_messages(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    printf("configuration %s, %u entries, checksum 0x%08X\n", FF802_MIXER_INIT_ID,
           FF802_MIXER_INIT_COUNT, (unsigned)ff802_mixer_init_checksum());
    printf("entries 1..%u unchanged from %s, checksum 0x%08X\n", FF802_MIXER_INIT_V3_COUNT,
           FF802_MIXER_INIT_V3_ID, (unsigned)ff802_mixer_init_v3_checksum());
    printf("entries 1..%u unchanged from %s, checksum 0x%08X\n", FF802_MIXER_INIT_V2_COUNT,
           FF802_MIXER_INIT_V2_ID, (unsigned)ff802_mixer_init_v2_checksum());
    printf("%u messages, in this order:\n", FF802_MIXER_INIT_COUNT);
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        uint8_t m[FF802_MIXER_SYSEX_SIZE];
        char label[64], hex[64];
        size_t o = 0;
        ff802_mixer_entry_label(&t[i], label, sizeof label);
        if (ff802_mixer_init_message(i, m) != FF802_OK)
            continue;
        for (unsigned j = 0; j < FF802_MIXER_SYSEX_SIZE; j++)
            o += (size_t)snprintf(hex + o, sizeof hex - o, j ? " %02X" : "%02X", m[j]);
        printf("  %2u  0x%04X = 0x%04X  %-34s %s\n", i + 1u, (unsigned)t[i].addr,
               (unsigned)t[i].value, label, hex);
    }
}

static int confirm(void *user, const ff802_mixer_result *found)
{
    (void)user;
#ifndef FF802_MIXER_INIT_SIM
    if (!isatty(STDIN_FILENO)) {
        printf("refused: --apply needs an interactive terminal\n");
        return 0;
    }
#endif
    char line[32];
    printf("\nAbout to send the %u messages above to MIDI port %d:%d '%s' (card %d).\n"
           "Headphones off / amplifiers down: the outputs are set to 0 dB.\n"
           "Type SEND to send them (anything else aborts): ", FF802_MIXER_INIT_COUNT,
           found->client, found->port, found->port_name, found->card);
    fflush(stdout);
    char *got = fgets(line, sizeof line, stdin);
    if (!isatty(STDIN_FILENO))
        printf("\n");             /* keep the log line based without a terminal */
    if (got == NULL)
        return 0;
    line[strcspn(line, "\n")] = '\0';
    return strcmp(line, "SEND") == 0;
}

int main(int argc, char **argv)
{
    const char *mode = NULL, *conn = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FF802_MIXER_INIT_SIM
            printf("ff802-mixer-init %s (SIMULATED MIDI backend)\n", FF802_VERSION);
#else
            printf("ff802-mixer-init %s (ALSA sequencer, mixer writes %s)\n", FF802_VERSION,
                   ff802_midi_seq_write_enabled() ? "enabled" : "disabled");
#endif
            return EXIT_OK;
        } else if ((strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "--dry-run") == 0 ||
                    strcmp(argv[i], "--apply") == 0) && mode == NULL) {
            mode = argv[i] + 2;
        } else if (strcmp(argv[i], "--connection") == 0 && conn == NULL && i + 1 < argc) {
            conn = argv[++i];
        } else {
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (mode == NULL || (strcmp(mode, "list") == 0 && conn != NULL)) {
        usage(stderr);
        return EXIT_USAGE;
    }
    if (ff802_mixer_init_verify() != 0) {
        printf("refused: register table check failed\n");
        return EXIT_NOT_SENT;
    }
    if (strcmp(mode, "list") == 0) {
        list_messages();
        return EXIT_OK;
    }
    int apply = strcmp(mode, "apply") == 0;

    ff802_auto_paths paths;
    ff802_midi_factory f;
    ff802_mixer_options o;
#ifdef FF802_MIXER_INIT_SIM
    const char *r = getenv("FF802_AUTO_ROOT");
    if (r == NULL || ff802_auto_paths_below(&paths, r) != 0) {
        fprintf(stderr, "ff802-mixer-init (sim): FF802_AUTO_ROOT must be an absolute path\n");
        return EXIT_USAGE;
    }
    sim_midi_setup(&paths, &f, &o);
#else
    ff802_auto_default_paths(&paths);
    f = ff802_midi_seq_factory();
    ff802_mixer_default_options(&o);
#endif
    o.dry_run = !apply;
    o.confirm = apply ? confirm : NULL;

    printf("ff802-mixer-init %s: %s\n", FF802_VERSION, apply ? "apply" : "dry run");
    printf("mixer writes in this program: %s\n", f.write_enabled ? "compiled in" : "NOT compiled in");
    list_messages();

    ff802_sysfs_dev d;
    unsigned n = 0;
    ff802_unit_status us = ff802_mixer_find_unit(paths.sysfs_root, conn, &d, &n);
    if (us != FF802_UNIT_FOUND) {
        printf("refused: %s; nothing sent\n", ff802_unit_status_str(us));
        return EXIT_NOT_SENT;
    }
    printf("connection %s: bus %u devnum %u ID %04x:%04x '%s' '%s' speed %s\n"
           "identity: %s\n", d.kernel_name, (unsigned)d.bus, (unsigned)d.devnum,
           (unsigned)d.vid, (unsigned)d.pid, d.manufacturer, d.product, d.speed,
           ff802_identity_str(ff802_sysfs_identify(&d)));

    int lock = ff802_auto_lock_existing(&paths);
    if (lock == -1) {
        printf("refused: cannot take the lock %s/lock; nothing sent\n", paths.run_dir);
        return EXIT_NOT_SENT;
    }
    if (lock == -2)
        printf("note: %s/lock not present (ff802-cc-auto never ran); no lock needed\n",
               paths.run_dir);

    ff802_mixer_result res;
    ff802_mixer_outcome out = ff802_mixer_run(paths.sysfs_root, &d, &f, &o, stdout, &res);
    if (lock >= 0)
        ff802_auto_unlock(lock);
    printf("result: %s (%u of %u registers): %s\n", ff802_mixer_outcome_str(out),
           res.written, FF802_MIXER_INIT_COUNT, res.detail);
    switch (out) {
    case FF802_MIXER_DONE:
    case FF802_MIXER_DRY_RUN:
        return EXIT_OK;
    case FF802_MIXER_INCOMPLETE:
        return EXIT_INCOMPLETE;
    default:
        return EXIT_NOT_SENT;
    }
}
