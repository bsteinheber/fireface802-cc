/*
 * ff802-mixer-d1: diagnosis D1 - does the value 0x0000 switch off ONE
 * routing of the original RME Fireface 802 in class-compliant mode?
 *
 *   ff802-mixer-d1 [--dry-run] [--connection <kernel name>]   (default: dry run)
 *   ff802-mixer-d1 --apply     [--connection <kernel name>]
 *   ff802-mixer-d1 --restore   [--connection <kernel name>]
 *   ff802-mixer-d1 --help | --version
 *
 * The program contains exactly TWO messages, both for register 0x4269
 * (Playback 10 -> Hardware Output 10 = Phones 10, set to 0 dB by the
 * mixer initialisation and confirmed audible on the unit):
 *
 *   --apply    0x4269 = 0x0000  (presumably off)  F0 00 20 0D 10 00 00 00 24 13 0C F7
 *   --restore  0x4269 = 0x9000  (0 dB)            F0 00 20 0D 10 00 00 20 26 13 0C F7
 *
 * Word 0xC2690000 / 0xC2699000: 0x4269 has six set bits, so the parity
 * bit 31 is set. Each call sends at most one message; nothing is restored
 * automatically. No input for an address or a value. Before anything is
 * sent the literals are checked against the register codec.
 *
 * Flow and safety as in ff802-mixer-b4: the Fireface 802 in CC mode (the
 * only one, or --connection) -> its sound card from sysfs -> the
 * sequencer port "Port 2" of that card (by card number, no fixed client
 * number) -> dry run: report and stop. --apply/--restore: report
 * identity, mode, port, register, value and SysEx, ask for "SEND" on the
 * terminal, check unit and port again, subscribe, send the ONE message,
 * wait 200 ms, close. No retry.
 *
 * Not part of any package and never installed; no systemd, udev or
 * automatic initialisation. Built only with the CMake option
 * FF802_BUILD_MIXER_D1 (default OFF).
 *
 * With FF802_D1_SIM the same source is built as a test program with the
 * simulated MIDI backend of tests/auto (no libasound), which works only
 * below $FF802_AUTO_ROOT and accepts the confirmation from a pipe.
 *
 * Exit status: 0 dry run passed / message sent, 2 usage, 3 refused or
 * nothing sent, 4 sending failed (state of the register unknown).
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ff802/auto_sysfs.h"
#include "ff802/midi_backend.h"
#include "ff802/mixer_port.h"
#include "ff802/reg_codec.h"

#ifdef FF802_D1_SIM
#include "ff802/auto_state.h"
#include "sim_midi.h"
#else
#include "ff802/midi_seq_alsa.h"
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

enum { EXIT_OK = 0, EXIT_USAGE = 2, EXIT_REFUSED = 3, EXIT_SEND_FAILED = 4 };

#define D1_REG     0x4269u
#define MSG_LEN    12u
#define SETTLE_MS  200u   /* after the message, before unsubscribing */

typedef struct d1_msg {
    const char *mode;
    uint16_t    value;
    const char *meaning;
    uint8_t     bytes[MSG_LEN];
} d1_msg;

/* The only two messages of this program. */
static const d1_msg MSGS[2] = {
    { "apply", 0x0000u, "Playback 10 -> Hardware Output 10 (Phones 10) off (test)",
      { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x24, 0x13, 0x0C, 0xF7 } },
    { "restore", 0x9000u, "Playback 10 -> Hardware Output 10 (Phones 10) 0 dB",
      { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x26, 0x13, 0x0C, 0xF7 } },
};

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-mixer-d1 [--dry-run] [--connection <kernel name>]\n"
        "       ff802-mixer-d1 --apply   [--connection <kernel name>]\n"
        "       ff802-mixer-d1 --restore [--connection <kernel name>]\n"
        "       ff802-mixer-d1 --help | --version\n"
        "\n"
        "Diagnosis D1: does 0x0000 switch off a routing of the Fireface 802 in\n"
        "class-compliant mode? ONE register, through the ALSA sequencer port 'Port 2':\n"
        "  --apply    0x4269 = 0x0000  Playback 10 -> Hardware Output 10 (Phones 10) off\n"
        "  --restore  0x4269 = 0x9000  Playback 10 -> Hardware Output 10 (Phones 10) 0 dB\n"
        "Without an option (default) it finds the unit, its sound card and the\n"
        "port and sends nothing. --apply/--restore ask for SEND on the terminal\n"
        "and send one message. Nothing is restored automatically.\n"
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

/* The literal bytes are the codec's SysEx frame of (0x4269, value). */
static int msg_ok(const d1_msg *m, uint32_t *word)
{
    uint8_t want[MSG_LEN] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };
    if (ff802_reg_encode(D1_REG, m->value, word) != FF802_OK || !ff802_reg_parity_ok(*word))
        return 0;
    for (unsigned i = 0; i < 5u; i++)
        want[6u + i] = (uint8_t)((*word >> (7u * i)) & 0x7Fu);
    want[11] = 0xF7;
    return memcmp(want, m->bytes, MSG_LEN) == 0;
}

static int confirm(void)
{
#ifndef FF802_D1_SIM
    if (!isatty(STDIN_FILENO)) {
        printf("refused: --apply/--restore needs an interactive terminal\n");
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
    const d1_msg *msg = NULL;
    int dry = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FF802_D1_SIM
            printf("ff802-mixer-d1 %s (SIMULATED MIDI backend)\n", FF802_VERSION);
#else
            printf("ff802-mixer-d1 %s (ALSA sequencer, mixer writes %s)\n", FF802_VERSION,
                   ff802_midi_seq_write_enabled() ? "enabled" : "disabled");
#endif
            return EXIT_OK;
        } else if (strcmp(argv[i], "--dry-run") == 0 && !dry && msg == NULL) {
            dry = 1;
        } else if (strcmp(argv[i], "--apply") == 0 && !dry && msg == NULL) {
            msg = &MSGS[0];
        } else if (strcmp(argv[i], "--restore") == 0 && !dry && msg == NULL) {
            msg = &MSGS[1];
        } else if (strcmp(argv[i], "--connection") == 0 && conn == NULL && i + 1 < argc) {
            conn = argv[++i];
        } else {
            usage(stderr);
            return EXIT_USAGE;
        }
    }

    ff802_midi_factory f;
    char root[FF802_SYSFS_STR_MAX * 4];
#ifdef FF802_D1_SIM
    ff802_auto_paths paths;
    ff802_mixer_options mopt;
    const char *r = getenv("FF802_AUTO_ROOT");
    if (r == NULL || ff802_auto_paths_below(&paths, r) != 0) {
        fprintf(stderr, "ff802-mixer-d1 (sim): FF802_AUTO_ROOT must be an absolute path\n");
        return EXIT_USAGE;
    }
    snprintf(root, sizeof root, "%s", paths.sysfs_root);
    sim_midi_setup(&paths, &f, &mopt);
#else
    snprintf(root, sizeof root, "/sys");
    f = ff802_midi_seq_factory();
#endif

    printf("ff802-mixer-d1 %s: %s\n", FF802_VERSION, msg != NULL ? msg->mode : "dry run");
    uint32_t words[2] = { 0, 0 };
    if (!msg_ok(&MSGS[0], &words[0]) || !msg_ok(&MSGS[1], &words[1])) {
        printf("refused: the compiled-in messages do not match the register codec\n");
        return EXIT_REFUSED;
    }
    char h[64];
    for (unsigned m = 0; m < 2u; m++) {
        hex(MSGS[m].bytes, MSG_LEN, h, sizeof h);
        printf("message --%-7s register 0x%04X = 0x%04X  %s\n"
               "                  word 0x%08X (odd parity), SysEx %s\n", MSGS[m].mode,
               D1_REG, (unsigned)MSGS[m].value, MSGS[m].meaning, (unsigned)words[m], h);
    }
    printf("note: nothing is restored automatically; --restore is a separate call\n");
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
        printf("refused: cannot open the ALSA sequencer (%s); nothing sent\n", strerror(-rc));
        return EXIT_REFUSED;
    }
    int out = EXIT_REFUSED;
    ff802_seq_port ports[FF802_SEQ_MAX_PORTS];
    size_t np = 0, idx = 0;
    rc = be.list_ports(be.self, ports, FF802_SEQ_MAX_PORTS, &np);
    if (rc < 0) {
        printf("refused: cannot list the sequencer ports (%s); nothing sent\n", strerror(-rc));
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
        printf("refused: %s; nothing sent\n", ff802_seqsel_str(sel));
        goto done;
    }
    int client = ports[idx].client, port = ports[idx].port;
    printf("selected port: %d:%d '%s'\n", client, port, ports[idx].name);

    if (msg == NULL) {
        printf("dry run: nothing sent\n");
        out = EXIT_OK;
        goto done;
    }
    if (!f.write_enabled) {
        printf("refused: mixer writes are not compiled into this program\n");
        goto done;
    }
    unsigned k = msg == &MSGS[0] ? 0u : 1u;
    hex(msg->bytes, MSG_LEN, h, sizeof h);
    printf("\nAbout to send to %d:%d (%s, %s, card %d):\n"
           "  register 0x%04X = 0x%04X  %s\n  word 0x%08X, SysEx %s\n",
           client, port, d.kernel_name, ff802_identity_str(ff802_sysfs_identify(&d)),
           card, D1_REG, (unsigned)msg->value, msg->meaning, (unsigned)words[k], h);
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
    rc = be.send_sysex(be.self, msg->bytes, MSG_LEN);
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
    if (out == EXIT_OK && msg != NULL)
        stamp("closed");
    return out;
}
