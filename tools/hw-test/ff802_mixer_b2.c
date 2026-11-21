/*
 * ff802-mixer-b2: hardware test B.2 of the mixer initialisation - ONE
 * MIDI write of ONE register of the original RME Fireface 802 in
 * class-compliant mode, through the ALSA sequencer.
 *
 *   ff802-mixer-b2 --dry-run          [--connection <kernel name>]
 *   ff802-mixer-b2 --send test        [--connection <kernel name>]
 *   ff802-mixer-b2 --send restore     [--connection <kernel name>]
 *   ff802-mixer-b2 --help | --version
 *
 * The program contains exactly two messages, both for register 0x2600
 * (volume of Hardware Output 9 = Phones 9, the left channel of the
 * headphone output 9/10):
 *
 *   test     0x2600 = 0xFF38  (-20.0 dB)  F0 00 20 0D 10 00 38 7E 03 30 0A F7
 *   restore  0x2600 = 0x0000  (  0.0 dB)  F0 00 20 0D 10 00 00 00 00 30 02 F7
 *
 * There is no input for an address or a value. Before anything is sent
 * the two literals are checked against the register codec.
 *
 * Flow: the Fireface 802 in CC mode (the only one, or --connection) ->
 * its sound card from sysfs -> the sequencer port "Port 2" of that card
 * (ff802_mixer_select_port: by card number, no fixed client number) ->
 * --dry-run: report and stop. --send: report, ask for "SEND" on the
 * terminal, check the connection and the port again, subscribe, send
 * the ONE message, close. No retry, no second message.
 *
 * Not part of any package and never installed. Built only with the CMake
 * option FF802_BUILD_MIXER_B2 (default OFF); only this program compiles
 * the ALSA backend with its write functions. The global option
 * FF802_ENABLE_MIXER_WRITE stays OFF, so ff802-cc-autoswitch, the service
 * and the Debian package cannot send mixer registers.
 *
 * With FF802_B2_SIM the same source is built as a test program with the
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

#ifdef FF802_B2_SIM
#include "ff802/auto_state.h"
#include "sim_midi.h"
#else
#include "ff802/midi_seq_alsa.h"
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

enum { EXIT_OK = 0, EXIT_USAGE = 2, EXIT_REFUSED = 3, EXIT_SEND_FAILED = 4 };

#define B2_REG 0x2600u
#define MSG_LEN 12u

typedef struct b2_msg {
    const char *name;
    uint16_t    value;
    const char *meaning;
    uint8_t     bytes[MSG_LEN];
} b2_msg;

/* The only two messages of this program. */
static const b2_msg MSGS[2] = {
    { "test", 0xFF38u, "Hardware Output 9 (Phones 9) volume -20.0 dB",
      { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x38, 0x7E, 0x03, 0x30, 0x0A, 0xF7 } },
    { "restore", 0x0000u, "Hardware Output 9 (Phones 9) volume 0.0 dB",
      { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x00, 0x30, 0x02, 0xF7 } },
};

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-mixer-b2 --dry-run [--connection <kernel name>]\n"
        "       ff802-mixer-b2 --send test|restore [--connection <kernel name>]\n"
        "       ff802-mixer-b2 --help | --version\n"
        "\n"
        "Hardware test B.2: writes ONE register of the Fireface 802 in\n"
        "class-compliant mode through the ALSA sequencer port 'Port 2':\n"
        "  test     0x2600 = 0xFF38  Hardware Output 9 (Phones 9) -20.0 dB\n"
        "  restore  0x2600 = 0x0000  Hardware Output 9 (Phones 9)   0.0 dB\n"
        "--dry-run finds the unit, its sound card and the port and sends\n"
        "nothing. --send asks for SEND on the terminal and sends one message.\n"
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

/* The literal bytes are the codec's SysEx frame of (0x2600, value). */
static int msgs_ok(void)
{
    for (unsigned m = 0; m < 2u; m++) {
        uint32_t w;
        uint8_t want[MSG_LEN] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };
        if (ff802_reg_encode(B2_REG, MSGS[m].value, &w) != FF802_OK)
            return 0;
        for (unsigned i = 0; i < 5u; i++)
            want[6u + i] = (uint8_t)((w >> (7u * i)) & 0x7Fu);
        want[11] = 0xF7;
        if (memcmp(want, MSGS[m].bytes, MSG_LEN) != 0)
            return 0;
    }
    return 1;
}

/* The one Fireface 802 in CC mode, or the named connection. */
static int find_unit(const char *root, const char *want, ff802_sysfs_dev *out)
{
    char dir[512];
    unsigned n = 0;

    if (want != NULL) {
        if (!ff802_sysfs_valid_name(want) ||
            ff802_sysfs_read(root, want, out) != FF802_SYSFS_OK) {
            printf("connection %s: not found\n", want);
            return 0;
        }
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/bus/usb/devices", root);
    DIR *dh = opendir(dir);
    if (dh == NULL) {
        printf("cannot read %s\n", dir);
        return 0;
    }
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        ff802_sysfs_dev d;
        if (!ff802_sysfs_valid_name(de->d_name) ||
            ff802_sysfs_read(root, de->d_name, &d) != FF802_SYSFS_OK ||
            ff802_sysfs_identify(&d) != FF802_ID_CLASS_COMPLIANT)
            continue;
        if (n++ == 0)
            *out = d;
    }
    closedir(dh);
    if (n != 1) {
        printf("%u Fireface 802 in class-compliant mode found; exactly one is "
               "required (or --connection)\n", n);
        return 0;
    }
    return 1;
}

static int confirm(void)
{
#ifndef FF802_B2_SIM
    if (!isatty(STDIN_FILENO)) {
        printf("refused: --send needs an interactive terminal\n");
        return 0;
    }
#endif
    char line[32];
    printf("Type SEND to send this ONE message (anything else aborts): ");
    fflush(stdout);
    if (fgets(line, sizeof line, stdin) == NULL)
        return 0;
    line[strcspn(line, "\n")] = '\0';
    return strcmp(line, "SEND") == 0;
}

int main(int argc, char **argv)
{
    const char *mode = NULL, *which = NULL, *conn = NULL;
    const char *root = "/sys";
    char rootbuf[FF802_SYSFS_STR_MAX * 4];

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return EXIT_OK;
        } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FF802_B2_SIM
            printf("ff802-mixer-b2 %s (SIMULATED MIDI backend)\n", FF802_VERSION);
#else
            printf("ff802-mixer-b2 %s (ALSA sequencer, mixer writes %s)\n", FF802_VERSION,
                   ff802_midi_seq_write_enabled() ? "enabled" : "disabled");
#endif
            return EXIT_OK;
        } else if (strcmp(argv[i], "--dry-run") == 0 && mode == NULL) {
            mode = "dry-run";
        } else if (strcmp(argv[i], "--send") == 0 && mode == NULL && i + 1 < argc) {
            mode = "send";
            which = argv[++i];
        } else if (strcmp(argv[i], "--connection") == 0 && conn == NULL && i + 1 < argc) {
            conn = argv[++i];
        } else {
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (mode == NULL) {
        usage(stderr);
        return EXIT_USAGE;
    }
    const b2_msg *msg = NULL;
    if (which != NULL) {
        for (unsigned m = 0; m < 2u; m++)
            if (strcmp(which, MSGS[m].name) == 0)
                msg = &MSGS[m];
        if (msg == NULL) {
            usage(stderr);
            return EXIT_USAGE;
        }
    }

    ff802_midi_factory f;
#ifdef FF802_B2_SIM
    ff802_auto_paths paths;
    ff802_mixer_options mopt;
    const char *r = getenv("FF802_AUTO_ROOT");
    if (r == NULL || ff802_auto_paths_below(&paths, r) != 0) {
        fprintf(stderr, "ff802-mixer-b2 (sim): FF802_AUTO_ROOT must be an absolute path\n");
        return EXIT_USAGE;
    }
    snprintf(rootbuf, sizeof rootbuf, "%s", paths.sysfs_root);
    root = rootbuf;
    sim_midi_setup(&paths, &f, &mopt);
#else
    (void)rootbuf;
    f = ff802_midi_seq_factory();
#endif

    printf("ff802-mixer-b2 %s: %s%s%s\n", FF802_VERSION, mode, msg ? " " : "",
           msg ? msg->name : "");
    if (!msgs_ok()) {
        printf("refused: the compiled-in messages do not match the register codec\n");
        return EXIT_REFUSED;
    }
    for (unsigned m = 0; m < 2u; m++) {
        char h[64];
        hex(MSGS[m].bytes, MSG_LEN, h, sizeof h);
        printf("message %-7s 0x%04X = 0x%04X  %s\n                %s\n", MSGS[m].name,
               B2_REG, (unsigned)MSGS[m].value, MSGS[m].meaning, h);
    }
    printf("mixer writes in this program: %s\n", f.write_enabled ? "compiled in" : "NOT compiled in");

    ff802_sysfs_dev d;
    if (!find_unit(root, conn, &d))
        return EXIT_REFUSED;
    ff802_identity id = ff802_sysfs_identify(&d);
    printf("connection %s: bus %u devnum %u ID %04x:%04x '%s' '%s' speed %s\n"
           "identity: %s\n", d.kernel_name, (unsigned)d.bus, (unsigned)d.devnum,
           (unsigned)d.vid, (unsigned)d.pid, d.manufacturer, d.product, d.speed,
           ff802_identity_str(id));
    if (id != FF802_ID_CLASS_COMPLIANT) {
        printf("refused: not a Fireface 802 in class-compliant mode\n");
        return EXIT_REFUSED;
    }
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
    size_t n = 0, idx = 0;
    rc = be.list_ports(be.self, ports, FF802_SEQ_MAX_PORTS, &n);
    if (rc < 0) {
        printf("refused: cannot list the sequencer ports (%s)\n", strerror(-rc));
        goto done;
    }
    printf("sequencer ports of card %d:\n", card);
    for (size_t i = 0; i < n; i++)
        if (ports[i].card == card)
            printf("  %3d:%d %s %s '%s'\n", ports[i].client, ports[i].port,
                   ports[i].kernel_client ? "kernel" : "user",
                   ports[i].writable ? "w" : "-", ports[i].name);
    ff802_seqsel sel = ff802_mixer_select_port(ports, n, card, &idx);
    if (sel != FF802_SEQSEL_FOUND) {
        printf("refused: %s\n", ff802_seqsel_str(sel));
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
    char h[64];
    hex(msg->bytes, MSG_LEN, h, sizeof h);
    printf("\nAbout to send to %d:%d: %s\n  %s  (register 0x%04X = 0x%04X)\n",
           client, port, h, msg->meaning, B2_REG, (unsigned)msg->value);
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
    stamp("sent 12 bytes; closing (unsubscribe)");
    out = EXIT_OK;
done:
    f.close(f.ctx, &be);
    if (out == EXIT_OK && msg != NULL)
        stamp("closed");
    return out;
}
