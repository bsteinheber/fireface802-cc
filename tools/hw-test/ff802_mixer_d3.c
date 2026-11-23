/*
 * ff802-mixer-d3: diagnosis D3 - ONE MIDI write that switches OFF the
 * routing Playback 1 -> Hardware Output 9 (Phones 9) of the original RME
 * Fireface 802 in class-compliant mode, through the ALSA sequencer.
 *
 *   ff802-mixer-d3 [--dry-run] [--connection <kernel name>]   (default: dry run)
 *   ff802-mixer-d3 --apply     [--connection <kernel name>]
 *   ff802-mixer-d3 --help | --version
 *
 * The program contains exactly ONE message:
 *
 *   0x4220 = 0x0000   Playback 1 -> Hardware Output 9 (Phones 9), off
 *   F0 00 20 0D 10 00 00 00 00 11 04 F7   (word 0x42200000, odd parity)
 *
 * 0x4220 = 0x4000 | (8 << 6) | (0x20 + 0): output index 8 (Phones 9, the
 * block of the confirmed 0x4228), source Playback 1 (the source of
 * 0x4020). That 0x0000 switches a routing off through this send path was
 * shown in D1 (0x4269) and D2 (0x4261, Playback 2 -> Phones 10).
 *
 * There is no restore: the previous value of 0x4220 is unknown, and
 * writing a value afterwards would be a new connection, not the old
 * state; the target state of this routing is "off". No input for an
 * address or a value. Before anything is sent the literal is checked
 * against the register codec.
 *
 * Flow and safety as in ff802-mixer-b4/-d1: the Fireface 802 in CC mode
 * (the only one, or --connection) -> its sound card from sysfs -> the
 * sequencer port "Port 2" of that card (by card number, no fixed client
 * number) -> dry run: report and stop. --apply: report identity, mode,
 * port, register, value and the SysEx message, ask for "SEND" on the
 * terminal, check the connection and the port again, subscribe, send the
 * ONE message, wait 200 ms, close. No retry, no second message.
 *
 * Not part of any package and never installed; no systemd, udev or
 * automatic initialisation. Built only with the CMake option
 * FF802_BUILD_MIXER_D3 (default OFF); the global option
 * FF802_ENABLE_MIXER_WRITE is not involved.
 *
 * With FF802_D3_SIM the same source is built as a test program with the
 * simulated MIDI backend of tests/auto (no libasound), which works only
 * below $FF802_AUTO_ROOT and accepts the confirmation from a pipe.
 *
 * Exit status: 0 dry run passed / message sent, 2 usage, 3 refused or
 * nothing sent, 4 sending failed (state of the register unknown).
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ff802/auto_sysfs.h"
#include "ff802/midi_backend.h"
#include "ff802/mixer_port.h"
#include "ff802/reg_codec.h"

#ifdef FF802_D3_SIM
#include "ff802/auto_state.h"
#include "sim_midi.h"
#else
#include "ff802/midi_seq_alsa.h"
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

enum { EXIT_OK = 0, EXIT_USAGE = 2, EXIT_REFUSED = 3, EXIT_SEND_FAILED = 4 };

#define D3_REG     0x4220u
#define D3_VALUE   0x0000u
#define MSG_LEN    12u
#define SETTLE_MS  200u   /* after the message, before unsubscribing */

/* The only message of this program. */
static const uint8_t MSG[MSG_LEN] =
    { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x00, 0x11, 0x04, 0xF7 };
static const char *const MEANING = "Playback 1 -> Hardware Output 9 (Phones 9) off";

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-mixer-d3 [--dry-run] [--connection <kernel name>]\n"
        "       ff802-mixer-d3 --apply [--connection <kernel name>]\n"
        "       ff802-mixer-d3 --help | --version\n"
        "\n"
        "Diagnosis D3: switches OFF one routing of the Fireface 802 in\n"
        "class-compliant mode through the ALSA sequencer port 'Port 2':\n"
        "  0x4220 = 0x0000  Playback 1 -> Hardware Output 9 (Phones 9) off\n"
        "Without --apply (default) it finds the unit, its sound card and the\n"
        "port and sends nothing. --apply asks for SEND on the terminal and\n"
        "sends the one message. There is no restore of the previous value.\n"
        "Only after an explicit approval of the hardware test.\n"
        "\n"
        "Exit status: 0 ok, 2 usage, 3 refused/nothing sent, 4 sending failed.\n");
}

static void stamp(const char *what)
{
    struct timespec ts;
    struct tm tm;
    char buf[32];
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
    printf("[%s.%03ld] %s\n", buf, ts.tv_nsec / 1000000L, what);
    fflush(stdout);
}

static void hex(const uint8_t *b, size_t n, char *out, size_t size)
{
    size_t o = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && o + 4u <= size; i++)
        o += (size_t)snprintf(out + o, size - o, i ? " %02X" : "%02X", b[i]);
}

/* The literal bytes are the codec's SysEx frame of (0x4220, 0x0000). */
static int msg_ok(uint32_t *word)
{
    uint8_t want[MSG_LEN] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };
    if (ff802_reg_encode(D3_REG, D3_VALUE, word) != FF802_OK || !ff802_reg_parity_ok(*word))
        return 0;
    for (unsigned i = 0; i < 5u; i++)
        want[6u + i] = (uint8_t)((*word >> (7u * i)) & 0x7Fu);
    want[11] = 0xF7;
    return memcmp(want, MSG, MSG_LEN) == 0;
}

static int confirm(void)
{
#ifndef FF802_D3_SIM
    if (!isatty(STDIN_FILENO)) {
        printf("refused: --apply needs an interactive terminal\n");
        return 0;
    }
#endif
    char line[32];
    printf("Type SEND to send this ONE message (anything else aborts): ");
    fflush(stdout);
    char *got = fgets(line, sizeof line, stdin);
    if (!isatty(STDIN_FILENO))
        printf("\n");
    if (got == NULL)
        return 0;
    line[strcspn(line, "\n")] = '\0';
    return strcmp(line, "SEND") == 0;
}

int main(int argc, char **argv)
{
    const char *conn = NULL;
    int apply = 0, dry = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FF802_D3_SIM
            printf("ff802-mixer-d3 %s (SIMULATED MIDI backend)\n", FF802_VERSION);
#else
            printf("ff802-mixer-d3 %s (ALSA sequencer, mixer writes %s)\n", FF802_VERSION,
                   ff802_midi_seq_write_enabled() ? "enabled" : "disabled");
#endif
            return EXIT_OK;
        } else if (strcmp(argv[i], "--dry-run") == 0 && !dry && !apply) {
            dry = 1;
        } else if (strcmp(argv[i], "--apply") == 0 && !dry && !apply) {
            apply = 1;
        } else if (strcmp(argv[i], "--connection") == 0 && conn == NULL && i + 1 < argc) {
            conn = argv[++i];
        } else {
            usage(stderr);
            return EXIT_USAGE;
        }
    }

    ff802_midi_factory f;
    char root[FF802_SYSFS_STR_MAX * 4];
#ifdef FF802_D3_SIM
    ff802_auto_paths paths;
    ff802_mixer_options mopt;
    const char *r = getenv("FF802_AUTO_ROOT");
    if (r == NULL || ff802_auto_paths_below(&paths, r) != 0) {
        fprintf(stderr, "ff802-mixer-d3 (sim): FF802_AUTO_ROOT must be an absolute path\n");
        return EXIT_USAGE;
    }
    snprintf(root, sizeof root, "%s", paths.sysfs_root);
    sim_midi_setup(&paths, &f, &mopt);
#else
    snprintf(root, sizeof root, "/sys");
    f = ff802_midi_seq_factory();
#endif

    printf("ff802-mixer-d3 %s: %s\n", FF802_VERSION, apply ? "apply" : "dry run");
    uint32_t word = 0;
    if (!msg_ok(&word)) {
        printf("refused: the compiled-in message does not match the register codec\n");
        return EXIT_REFUSED;
    }
    char h[64];
    hex(MSG, MSG_LEN, h, sizeof h);
    printf("message: register 0x%04X = 0x%04X  %s\n", D3_REG, D3_VALUE, MEANING);
    printf("         word 0x%08X (odd parity), SysEx %s\n", (unsigned)word, h);
    printf("note: no restore - the previous value of 0x%04X is unknown\n", D3_REG);
    printf("mixer writes in this program: %s\n", f.write_enabled ? "compiled in" : "NOT compiled in");

    ff802_sysfs_dev d;
    unsigned n = 0;
    ff802_unit_status us = ff802_mixer_find_unit(root, conn, &d, &n);
    if (us != FF802_UNIT_FOUND) {
        printf("refused: %s; nothing sent\n", ff802_unit_status_str(us));
        return EXIT_REFUSED;
    }
    printf("connection %s: bus %u devnum %u ID %04x:%04x '%s' '%s' speed %s\n"
           "identity: %s\n", d.kernel_name, (unsigned)d.bus, (unsigned)d.devnum,
           (unsigned)d.vid, (unsigned)d.pid, d.manufacturer, d.product, d.speed,
           ff802_identity_str(ff802_sysfs_identify(&d)));
    int card = -1;
    ff802_mport_status cs = ff802_mixer_find_card(root, d.kernel_name, &card);
    printf("sound card (sysfs): %s, card %d\n", ff802_mport_status_str(cs), card);
    if (cs != FF802_MPORT_FOUND)
        return EXIT_REFUSED;

    ff802_midi_backend be;
    memset(&be, 0, sizeof be);
    int rc = f.open(f.ctx, &be);
    if (rc < 0) {
        printf("refused: cannot open the ALSA sequencer (%s)\n", strerror(-rc));
        return EXIT_REFUSED;
    }
    int out = EXIT_REFUSED;
    ff802_seq_port ports[FF802_SEQ_MAX_PORTS];
    size_t np = 0, idx = 0;
    rc = be.list_ports(be.self, ports, FF802_SEQ_MAX_PORTS, &np);
    if (rc < 0) {
        printf("refused: cannot list the sequencer ports (%s)\n", strerror(-rc));
        goto done;
    }
    printf("sequencer ports of card %d:\n", card);
    for (size_t i = 0; i < np; i++)
        if (ports[i].card == card)
            printf("  %3d:%d %s %s '%s'\n", ports[i].client, ports[i].port,
                   ports[i].kernel_client ? "kernel" : "user",
                   ports[i].writable ? "w" : "-", ports[i].name);
    ff802_seqsel sel = ff802_mixer_select_port(ports, np, card, &idx);
    if (sel != FF802_SEQSEL_FOUND) {
        printf("refused: %s\n", ff802_seqsel_str(sel));
        goto done;
    }
    int client = ports[idx].client, port = ports[idx].port;
    printf("selected port: %d:%d '%s'\n", client, port, ports[idx].name);

    if (!apply) {
        printf("dry run: nothing sent\n");
        out = EXIT_OK;
        goto done;
    }
    if (!f.write_enabled) {
        printf("refused: mixer writes are not compiled into this program\n");
        goto done;
    }
    printf("\nAbout to send to %d:%d (%s, %s, card %d):\n"
           "  register 0x%04X = 0x%04X  %s\n  SysEx %s\n"
           "  The previous value cannot be restored.\n",
           client, port, d.kernel_name, ff802_identity_str(ff802_sysfs_identify(&d)),
           card, D3_REG, D3_VALUE, MEANING, h);
    if (!confirm()) {
        printf("aborted: nothing sent\n");
        goto done;
    }

    /* everything again, right before the write */
    ff802_sysfs_dev now;
    int card2 = -1;
    size_t idx2 = 0, n2 = 0;
    if (ff802_sysfs_read(root, d.kernel_name, &now) != FF802_SYSFS_OK ||
        now.bus != d.bus || now.devnum != d.devnum ||
        ff802_sysfs_identify(&now) != FF802_ID_CLASS_COMPLIANT ||
        ff802_mixer_find_card(root, d.kernel_name, &card2) != FF802_MPORT_FOUND ||
        card2 != card) {
        printf("refused: the connection changed; nothing sent\n");
        goto done;
    }
    if (be.list_ports(be.self, ports, FF802_SEQ_MAX_PORTS, &n2) < 0 ||
        ff802_mixer_select_port(ports, n2, card, &idx2) != FF802_SEQSEL_FOUND ||
        ports[idx2].client != client || ports[idx2].port != port) {
        printf("refused: the sequencer port changed; nothing sent\n");
        goto done;
    }
    rc = be.connect(be.self, client, port);
    if (rc < 0) {
        printf("refused: cannot connect to %d:%d (%s); nothing sent\n", client, port,
               strerror(-rc));
        goto done;
    }
    stamp("sending");
    rc = be.send_sysex(be.self, MSG, MSG_LEN);
    if (rc < 0) {
        stamp("FAILED");
        printf("sending failed (%s); the register state is unknown; nothing retried\n",
               strerror(-rc));
        out = EXIT_SEND_FAILED;
        goto done;
    }
    stamp("sent 12 bytes; waiting 200 ms before closing (unsubscribe)");
    f.sleep_ms(f.ctx, SETTLE_MS);
    out = EXIT_OK;
done:
    f.close(f.ctx, &be);
    if (out == EXIT_OK && apply)
        stamp("closed");
    return out;
}
