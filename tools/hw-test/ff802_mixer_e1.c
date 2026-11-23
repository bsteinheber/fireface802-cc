/*
 * ff802-mixer-e1: hardware test E1 - routing Input 9 (Mic/Inst 9) ->
 * Hardware Output 3 (Analog 3) of the original RME Fireface 802 in
 * class-compliant mode at 30 % linear amplitude, guided, in ONE call,
 * with restore to a defined state. Through the ALSA sequencer.
 *
 *   ff802-mixer-e1 [--dry-run] [--connection <kernel name>]   (default: dry run)
 *   ff802-mixer-e1 --apply   [--connection <k>] [--log <file>]
 *   ff802-mixer-e1 --restore [--connection <k>] [--log <file>]
 *   ff802-mixer-e1 --help | --version
 *
 * The program contains exactly TWO messages, both for register 0x4088:
 *
 *   OFF   0x4088 = 0x0000  F0 00 20 0D 10 00 00 00 20 04 04 F7  (word 0x40880000)
 *   TEST  0x4088 = 0x2666  F0 00 20 0D 10 00 66 4C 20 04 0C F7  (word 0xC0882666)
 *
 * 0x4088 = 0x4000 | (2 << 6) | 8: output index 2 (Analog 3), source
 * index 8 (Mic/Inst 9) - OSCMix device_ff802.c inputs[8] and ctltoreg
 * MIX_LEVEL, and the FireWire protocol (mixer step 0x40, line 8 + mic).
 * 0x2666 = lroundf(0.3 * 0x8000), OSCMix setmixlevel() for levels <= 0.5.
 * That the unit interprets 0x2666 as 30 % linear is NOT verified: this is
 * what E1 checks. 0x0000 = off was shown on the unit in D1-D3.
 *
 * --apply, after identity, mode, sound card and port are checked and the
 * plan is shown:
 *   1. the user lowers the monitoring volume and types SEND (approval of
 *      all writes of this run);
 *   2. question: is Input 9 audible on Analog 3 now (before any write)?
 *   3. OFF  (defined initial state; the previous value is not read and
 *      not kept: there is no reliable way to read it, no refresh request
 *      is sent);
 *   4. TEST, then the listening check (no time limit up to 10 min, then
 *      the test ends by itself): what is heard on Analog 3, and is
 *      Analog 4 still silent?
 *   5. OFF  (restore) - also after an abort, end of input, timeout,
 *      SIGINT/SIGTERM/SIGHUP or a failed TEST write;
 *   6. question: is Analog 3 silent again?
 * Each message is sent once; nothing is retried. A failed restore is
 * reported explicitly with the command to repeat it (--restore). The
 * result is only "passed" when the user confirmed the expected effect;
 * a successful MIDI transfer alone is reported as "transferred", never
 * as an acoustic result. Everything is written to the terminal and to
 * the log file (default ./ff802-mixer-e1-<date>-<time>.log).
 *
 * --restore sends only OFF (after SEND), e.g. when a restore failed.
 *
 * No other register, no input gain, no output volume, no refresh, no
 * mixer table, no automatic initialisation. Not part of any package and
 * never installed; built only with the CMake option FF802_BUILD_MIXER_E1
 * (default OFF). With FF802_E1_SIM the same source is built as a test
 * program with the simulated MIDI backend of tests/auto (no libasound),
 * which works only below $FF802_AUTO_ROOT and reads the answers from a
 * pipe.
 *
 * Exit status: 0 passed (expected effect confirmed, restored), 2 usage,
 * 3 refused or aborted before any write, 4 a write failed (restore sent),
 * 5 test done and restored, effect not confirmed, 6 RESTORE FAILED
 * (0x4088 may still be 0x2666).
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ff802/auto_sysfs.h"
#include "ff802/midi_backend.h"
#include "ff802/mixer_port.h"
#include "ff802/reg_codec.h"

#ifdef FF802_E1_SIM
#include "ff802/auto_state.h"
#include "sim_midi.h"
#else
#include "ff802/midi_seq_alsa.h"
#endif

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

enum {
    EXIT_PASSED = 0, EXIT_USAGE = 2, EXIT_REFUSED = 3, EXIT_WRITE_FAILED = 4,
    EXIT_NOT_CONFIRMED = 5, EXIT_RESTORE_FAILED = 6
};

#define E1_REG        0x4088u
#define E1_OFF        0x0000u
#define E1_TEST       0x2666u
#define MSG_LEN       12u
#define GAP_MS        10u     /* between two messages */
#define SETTLE_MS     200u    /* after the last message, before unsubscribing */
#define LISTEN_MAX_S  600u    /* listening check: then the test ends by itself */

static const uint8_t MSG_OFF[MSG_LEN] =
    { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x20, 0x04, 0x04, 0xF7 };
static const uint8_t MSG_TEST[MSG_LEN] =
    { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x66, 0x4C, 0x20, 0x04, 0x0C, 0xF7 };
static const char *const ROUTE = "Input 9 (Mic/Inst 9) -> Hardware Output 3 (Analog 3)";

static FILE *g_log;
static volatile sig_atomic_t g_signal;

static void on_signal(int sig) { g_signal = sig; }

/* Terminal and log file. */
__attribute__((format(printf, 1, 2)))
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_log != NULL) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
}

static void now_str(char *buf, size_t size)
{
    struct timespec ts;
    struct tm tm;
    char b[32];
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(buf, size, "%s.%03ld", b, ts.tv_nsec / 1000000L);
}

__attribute__((format(printf, 1, 2)))
static void stamp(const char *fmt, ...)
{
    char t[48], msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    now_str(t, sizeof t);
    say("[%s] %s\n", t, msg);
}

static void hex(const uint8_t *b, size_t n, char *out, size_t size)
{
    size_t o = 0;
    out[0] = '\0';
    for (size_t i = 0; i < n && o + 4u <= size; i++)
        o += (size_t)snprintf(out + o, size - o, i ? " %02X" : "%02X", b[i]);
}

/* The literal bytes are the codec's SysEx frame of (reg, value). */
static int msg_ok(uint16_t value, const uint8_t *msg, uint32_t *word)
{
    uint8_t want[MSG_LEN] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };
    if (ff802_reg_encode(E1_REG, value, word) != FF802_OK || !ff802_reg_parity_ok(*word))
        return 0;
    for (unsigned i = 0; i < 5u; i++)
        want[6u + i] = (uint8_t)((*word >> (7u * i)) & 0x7Fu);
    want[11] = 0xF7;
    return memcmp(want, msg, MSG_LEN) == 0;
}

static unsigned listen_max_s(void)
{
#ifdef FF802_E1_SIM
    const char *v = getenv("FF802_SIM_E1_LISTEN_S");   /* tests: short timeout */
    if (v != NULL && *v != '\0')
        return (unsigned)strtoul(v, NULL, 10);
#endif
    return LISTEN_MAX_S;
}

enum { ANS_LINE = 1, ANS_EOF = 0, ANS_TIMEOUT = -1, ANS_SIGNAL = -2 };

/* One answer line; waits up to timeout_s (0: no limit). */
static int ask(const char *prompt, unsigned timeout_s, char *buf, size_t size)
{
    say("%s", prompt);
    int left_ms = timeout_s ? (int)(timeout_s * 1000u) : -1;
    for (;;) {
        if (g_signal)
            return ANS_SIGNAL;
        struct pollfd pfd = { .fd = STDIN_FILENO, .events = POLLIN };
        int step = left_ms < 0 ? 1000 : (left_ms < 1000 ? left_ms : 1000);
        int r = poll(&pfd, 1, step);
        if (r < 0 && errno == EINTR)
            continue;                 /* g_signal is checked above */
        if (r > 0) {
            /* byte by byte, unbuffered: poll() must see what is not yet read */
            size_t n = 0;
            for (;;) {
                char c;
                ssize_t got = read(STDIN_FILENO, &c, 1);
                if (got < 0 && errno == EINTR) {
                    if (g_signal)
                        return ANS_SIGNAL;
                    continue;
                }
                if (got <= 0) {
                    if (n == 0)
                        return ANS_EOF;
                    break;            /* last line without newline */
                }
                if (c == '\n')
                    break;
                if (n + 1u < size)
                    buf[n++] = c;
            }
            buf[n] = '\0';
            buf[strcspn(buf, "\r")] = '\0';
            if (!isatty(STDIN_FILENO))
                say("%s\n", buf);     /* piped input: keep the log readable */
            else if (g_log != NULL)
                fprintf(g_log, "%s\n", buf);
            return ANS_LINE;
        }
        if (r < 0)
            return ANS_EOF;
        if (left_ms >= 0) {
            left_ms -= step;
            if (left_ms <= 0)
                return ANS_TIMEOUT;
        }
    }
}

/* A choice "1".."n"; anything else is asked again (at most 5 times). */
static int choose(const char *prompt, int n, unsigned timeout_s, int *answer)
{
    char line[64];
    for (int tries = 0; tries < 5; tries++) {
        int r = ask(prompt, timeout_s, line, sizeof line);
        if (r != ANS_LINE)
            return r;
        if (line[0] >= '1' && line[0] < '1' + n && line[1] == '\0') {
            *answer = line[0] - '0';
            return ANS_LINE;
        }
        say("please answer with a number 1..%d\n", n);
    }
    return ANS_EOF;
}

static const char *ans_str(int r)
{
    switch (r) {
    case ANS_EOF:     return "end of input";
    case ANS_TIMEOUT: return "no answer within the time limit";
    case ANS_SIGNAL:  return "interrupted by a signal";
    }
    return "answer";
}

typedef struct target {
    char root[FF802_SYSFS_STR_MAX * 4];
    ff802_sysfs_dev dev;
    int card, client, port;
} target;

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: ff802-mixer-e1 [--dry-run] [--connection <kernel name>]\n"
        "       ff802-mixer-e1 --apply   [--connection <kernel name>] [--log <file>]\n"
        "       ff802-mixer-e1 --restore [--connection <kernel name>] [--log <file>]\n"
        "       ff802-mixer-e1 --help | --version\n"
        "\n"
        "Hardware test E1: Input 9 (Mic/Inst 9) -> Analog Output 3 at 30 %% linear\n"
        "of the Fireface 802 in class-compliant mode, register 0x4088:\n"
        "  0x4088 = 0x0000  off (defined initial state, and restore)\n"
        "  0x4088 = 0x2666  30 %% linear (OSCMix encoding, not verified on the unit)\n"
        "Without an option (dry run) it finds the unit, its sound card and the\n"
        "ALSA sequencer port 'Port 2' and sends nothing. --apply guides through\n"
        "the whole test in one run: SEND, off, test value, listening check,\n"
        "restore to off (also after an abort or error). --restore sends only\n"
        "0x4088 = 0x0000. Only after an explicit approval of the hardware test.\n"
        "\n"
        "Exit status: 0 passed, 2 usage, 3 refused/aborted before any write,\n"
        "4 a write failed (restore sent), 5 done and restored, effect not\n"
        "confirmed, 6 RESTORE FAILED (0x4088 may still be 0x2666).\n");
}

/* Identity, mode, card, port; leaves the backend open on success. */
static int find_target(const ff802_midi_factory *f, ff802_midi_backend *be,
                       const char *conn, target *t)
{
    unsigned n = 0;
    ff802_unit_status us = ff802_mixer_find_unit(t->root, conn, &t->dev, &n);
    if (us != FF802_UNIT_FOUND) {
        say("refused: %s; nothing sent\n", ff802_unit_status_str(us));
        return -1;
    }
    const ff802_sysfs_dev *d = &t->dev;
    say("connection %s: bus %u devnum %u ID %04x:%04x '%s' '%s' speed %s\n"
        "identity: %s\n", d->kernel_name, (unsigned)d->bus, (unsigned)d->devnum,
        (unsigned)d->vid, (unsigned)d->pid, d->manufacturer, d->product, d->speed,
        ff802_identity_str(ff802_sysfs_identify(d)));
    ff802_mport_status cs = ff802_mixer_find_card(t->root, d->kernel_name, &t->card);
    say("sound card (sysfs): %s, card %d\n", ff802_mport_status_str(cs), t->card);
    if (cs != FF802_MPORT_FOUND)
        return -1;

    memset(be, 0, sizeof *be);
    int rc = f->open(f->ctx, be);
    if (rc < 0) {
        say("refused: cannot open the ALSA sequencer (%s)\n", strerror(-rc));
        return -1;
    }
    ff802_seq_port ports[FF802_SEQ_MAX_PORTS];
    size_t np = 0, idx = 0;
    rc = be->list_ports(be->self, ports, FF802_SEQ_MAX_PORTS, &np);
    if (rc < 0) {
        say("refused: cannot list the sequencer ports (%s)\n", strerror(-rc));
        f->close(f->ctx, be);
        return -1;
    }
    say("sequencer ports of card %d:\n", t->card);
    for (size_t i = 0; i < np; i++)
        if (ports[i].card == t->card)
            say("  %3d:%d %s %s '%s'\n", ports[i].client, ports[i].port,
                ports[i].kernel_client ? "kernel" : "user",
                ports[i].writable ? "w" : "-", ports[i].name);
    ff802_seqsel sel = ff802_mixer_select_port(ports, np, t->card, &idx);
    if (sel != FF802_SEQSEL_FOUND) {
        say("refused: %s\n", ff802_seqsel_str(sel));
        f->close(f->ctx, be);
        return -1;
    }
    t->client = ports[idx].client;
    t->port = ports[idx].port;
    say("selected port: %d:%d '%s'\n", t->client, t->port, ports[idx].name);
    return 0;
}

/* Same connection, card and port as found before? */
static int still_same(ff802_midi_backend *be, const target *t)
{
    ff802_sysfs_dev now;
    int card2 = -1;
    size_t idx2 = 0, n2 = 0;
    ff802_seq_port ports[FF802_SEQ_MAX_PORTS];
    if (ff802_sysfs_read(t->root, t->dev.kernel_name, &now) != FF802_SYSFS_OK ||
        now.bus != t->dev.bus || now.devnum != t->dev.devnum ||
        ff802_sysfs_identify(&now) != FF802_ID_CLASS_COMPLIANT ||
        ff802_mixer_find_card(t->root, t->dev.kernel_name, &card2) != FF802_MPORT_FOUND ||
        card2 != t->card)
        return 0;
    return be->list_ports(be->self, ports, FF802_SEQ_MAX_PORTS, &n2) == 0 &&
           ff802_mixer_select_port(ports, n2, t->card, &idx2) == FF802_SEQSEL_FOUND &&
           ports[idx2].client == t->client && ports[idx2].port == t->port;
}

/* One message, once. Signals are held back while it is handed to ALSA. */
static int send_one(ff802_midi_backend *be, const char *what, uint16_t value,
                    const uint8_t *msg)
{
    char h[64];
    sigset_t block, old;
    hex(msg, MSG_LEN, h, sizeof h);
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGHUP);
    int masked = sigprocmask(SIG_BLOCK, &block, &old) == 0;
    int rc = be->send_sysex(be->self, msg, MSG_LEN);
    if (masked)
        sigprocmask(SIG_SETMASK, &old, NULL);
    if (rc < 0) {
        stamp("%s: 0x%04X = 0x%04X  SysEx %s  FAILED (%s); not retried", what,
              E1_REG, (unsigned)value, h, strerror(-rc));
        return -1;
    }
    stamp("%s: 0x%04X = 0x%04X  SysEx %s  transferred to ALSA (12 bytes)", what,
          E1_REG, (unsigned)value, h);
    return 0;
}

static int confirm_send(const char *prompt)
{
#ifndef FF802_E1_SIM
    if (!isatty(STDIN_FILENO)) {
        say("refused: needs an interactive terminal\n");
        return 0;
    }
#endif
    char line[32];
    int r = ask(prompt, 0, line, sizeof line);
    return r == ANS_LINE && strcmp(line, "SEND") == 0;
}

static FILE *open_log(const char *path, char *used, size_t size)
{
    if (path != NULL) {
        snprintf(used, size, "%s", path);
    } else {
        time_t t = time(NULL);
        struct tm tm;
        char b[32];
        localtime_r(&t, &tm);
        strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
        snprintf(used, size, "ff802-mixer-e1-%s.log", b);
    }
    return fopen(used, "a");
}

int main(int argc, char **argv)
{
    const char *conn = NULL, *logpath = NULL;
    int apply = 0, dry = 0, restore = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return EXIT_PASSED;
        } else if (strcmp(argv[i], "--version") == 0) {
#ifdef FF802_E1_SIM
            printf("ff802-mixer-e1 %s (SIMULATED MIDI backend)\n", FF802_VERSION);
#else
            printf("ff802-mixer-e1 %s (ALSA sequencer, mixer writes %s)\n", FF802_VERSION,
                   ff802_midi_seq_write_enabled() ? "enabled" : "disabled");
#endif
            return EXIT_PASSED;
        } else if (strcmp(argv[i], "--dry-run") == 0 && !dry && !apply && !restore) {
            dry = 1;
        } else if (strcmp(argv[i], "--apply") == 0 && !dry && !apply && !restore) {
            apply = 1;
        } else if (strcmp(argv[i], "--restore") == 0 && !dry && !apply && !restore) {
            restore = 1;
        } else if (strcmp(argv[i], "--connection") == 0 && conn == NULL && i + 1 < argc) {
            conn = argv[++i];
        } else if (strcmp(argv[i], "--log") == 0 && logpath == NULL && i + 1 < argc) {
            logpath = argv[++i];
        } else {
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (logpath != NULL && !apply && !restore) {
        usage(stderr);
        return EXIT_USAGE;
    }

    ff802_midi_factory f;
    target t;
    memset(&t, 0, sizeof t);
    t.card = t.client = t.port = -1;
#ifdef FF802_E1_SIM
    ff802_auto_paths paths;
    ff802_mixer_options mopt;
    const char *rootenv = getenv("FF802_AUTO_ROOT");
    if (rootenv == NULL || ff802_auto_paths_below(&paths, rootenv) != 0) {
        fprintf(stderr, "ff802-mixer-e1 (sim): FF802_AUTO_ROOT must be an absolute path\n");
        return EXIT_USAGE;
    }
    snprintf(t.root, sizeof t.root, "%s", paths.sysfs_root);
    sim_midi_setup(&paths, &f, &mopt);
#else
    snprintf(t.root, sizeof t.root, "/sys");
    f = ff802_midi_seq_factory();
#endif

    char logused[512] = "";
    if (apply || restore) {
        g_log = open_log(logpath, logused, sizeof logused);
        if (g_log == NULL) {
            printf("refused: cannot open the log file %s (%s); nothing sent\n", logused,
                   strerror(errno));
            return EXIT_REFUSED;
        }
    }
    char t0[48];
    now_str(t0, sizeof t0);
    say("ff802-mixer-e1 %s: %s, %s\n", FF802_VERSION,
        apply ? "apply (guided test)" : restore ? "restore" : "dry run", t0);
    if (g_log != NULL)
        say("log file: %s\n", logused);

    uint32_t w_off = 0, w_test = 0;
    if (!msg_ok(E1_OFF, MSG_OFF, &w_off) || !msg_ok(E1_TEST, MSG_TEST, &w_test)) {
        say("refused: the compiled-in messages do not match the register codec\n");
        return EXIT_REFUSED;
    }
    char hoff[64], htest[64];
    hex(MSG_OFF, MSG_LEN, hoff, sizeof hoff);
    hex(MSG_TEST, MSG_LEN, htest, sizeof htest);
    say("route: %s\n", ROUTE);
    say("message OFF:  register 0x%04X = 0x%04X  off          word 0x%08X  SysEx %s\n",
        E1_REG, E1_OFF, (unsigned)w_off, hoff);
    say("message TEST: register 0x%04X = 0x%04X  30 %% linear word 0x%08X  SysEx %s\n",
        E1_REG, E1_TEST, (unsigned)w_test, htest);
    say("note: 0x2666 = 30 %% linear per OSCMix (setmixlevel); not verified on the unit\n");
    say("note: the previous value of 0x4088 is not read; the defined state is off (0x0000)\n");
    say("mixer writes in this program: %s\n", f.write_enabled ? "compiled in" : "NOT compiled in");

    ff802_midi_backend be;
    if (find_target(&f, &be, conn, &t) != 0)
        return EXIT_REFUSED;
    int out = EXIT_REFUSED;

    if (!apply && !restore) {
        say("dry run: nothing sent\n");
        out = EXIT_PASSED;
        goto close;
    }
    if (!f.write_enabled) {
        say("refused: mixer writes are not compiled into this program\n");
        goto close;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;        /* no SA_RESTART: poll() returns */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);

    if (restore) {
        say("\nAbout to send to %d:%d (%s, card %d) ONE message:\n"
            "  0x%04X = 0x%04X  %s off\n", t.client, t.port, t.dev.kernel_name, t.card,
            E1_REG, E1_OFF, ROUTE);
        if (!confirm_send("Type SEND to send it (anything else aborts): ") || g_signal) {
            say("aborted: nothing sent\n");
            goto close;
        }
        if (!still_same(&be, &t)) {
            say("refused: the connection or the port changed; nothing sent\n");
            goto close;
        }
        int rc = be.connect(be.self, t.client, t.port);
        if (rc < 0) {
            say("RESTORE FAILED: cannot connect to %d:%d (%s); 0x4088 unchanged\n",
                t.client, t.port, strerror(-rc));
            out = EXIT_RESTORE_FAILED;
            goto close;
        }
        if (send_one(&be, "RESTORE", E1_OFF, MSG_OFF) != 0) {
            say("RESTORE FAILED: run 'ff802-mixer-e1 --restore' again\n");
            out = EXIT_RESTORE_FAILED;
            goto close;
        }
        f.sleep_ms(f.ctx, SETTLE_MS);
        say("result: restore transferred (0x4088 = 0x0000)\n");
        out = EXIT_PASSED;
        goto close;
    }

    /* --- guided test ------------------------------------------------------- */
    say("\nPlan (port %d:%d, %s, card %d), each message once, no retry:\n"
        "  1. OFF   0x4088 = 0x0000  defined initial state\n"
        "  2. TEST  0x4088 = 0x2666  then listening check (up to %u s)\n"
        "  3. OFF   0x4088 = 0x0000  restore (also after abort or error)\n"
        "Nothing else is changed: no other matrix register, no input gain, no\n"
        "output volume, no refresh.\n\n"
        "Before you continue:\n"
        "  - turn the monitoring volume (amplifier/speakers at Analog Out 3/4)\n"
        "    DOWN; headphones off;\n"
        "  - feed a steady signal into Input 9 (Mic/Inst 9), e.g. a tone or\n"
        "    music from a line source at a moderate level; do not change the\n"
        "    gain knob of Input 9; no microphone in front of the speakers\n"
        "    (feedback);\n"
        "  - Analog Out 3 is the LEFT channel of the pair 3/4, Analog Out 4\n"
        "    the right one (check: Input 9 must stay silent on Out 4).\n",
        t.client, t.port, t.dev.kernel_name, t.card, listen_max_s());
    if (!confirm_send("Volume down and signal on Input 9? Type SEND to start the test: ") ||
        g_signal) {
        say("aborted: nothing sent\n");
        goto close;
    }
    stamp("approved by the user (SEND)");

    int a_before = 0, a_test = 0, a_out4 = 0, a_after = 0;
    int r = choose("\nBEFORE any write: is Input 9 audible on Analog Out 3 now?\n"
                   "  1 no, silent   2 yes, audible   3 cannot tell\nanswer: ",
                   3, 0, &a_before);
    if (r != ANS_LINE || g_signal) {
        say("aborted (%s): nothing sent\n", g_signal ? "interrupted by a signal" : ans_str(r));
        goto close;
    }
    stamp("before the test: Input 9 on Out 3 %s", a_before == 1 ? "silent" :
          a_before == 2 ? "AUDIBLE (previous value not off; it will be replaced by off)" :
          "unknown");

    if (!still_same(&be, &t)) {
        say("refused: the connection or the port changed; nothing sent\n");
        goto close;
    }
    int rc = be.connect(be.self, t.client, t.port);
    if (rc < 0) {
        say("refused: cannot connect to %d:%d (%s); nothing sent\n", t.client, t.port,
            strerror(-rc));
        goto close;
    }
    stamp("subscribed to %d:%d", t.client, t.port);

    /* 1. defined initial state */
    if (send_one(&be, "step 1 OFF", E1_OFF, MSG_OFF) != 0) {
        say("result: FAILED at step 1; nothing else sent. 0x4088 is unchanged or off.\n");
        out = EXIT_WRITE_FAILED;
        goto close;
    }
    f.sleep_ms(f.ctx, GAP_MS);

    /* 2. test value; from here on the restore is always attempted */
    int test_sent = send_one(&be, "step 2 TEST", E1_TEST, MSG_TEST) == 0;
    int listen_r = ANS_LINE;
    if (test_sent && !g_signal) {
        say("\nLISTEN NOW (up to %u s; Ctrl-C restores and ends): Input 9 should be on\n"
            "Analog Out 3 at about -10.5 dB (30 %% amplitude), Out 4 silent.\n",
            listen_max_s());
        listen_r = choose("What do you hear on Analog Out 3?\n"
                          "  1 Input 9 clearly audible (moderately quieter than direct)\n"
                          "  2 Input 9 only very faintly audible\n"
                          "  3 nothing\n"
                          "  4 unexpectedly loud or distorted\nanswer: ",
                          4, listen_max_s(), &a_test);
        if (listen_r == ANS_LINE && !g_signal)
            listen_r = choose("Is Analog Out 4 (right channel) silent?\n"
                              "  1 yes, silent   2 no, Input 9 audible   3 cannot tell\nanswer: ",
                              3, listen_max_s(), &a_out4);
        if (listen_r != ANS_LINE || g_signal)
            stamp("listening check ended early (%s); restoring",
                  g_signal ? "interrupted by a signal" : ans_str(listen_r));
        else
            stamp("listening check: Out 3 %s; Out 4 %s",
                  a_test == 1 ? "clearly audible" : a_test == 2 ? "very faint" :
                  a_test == 3 ? "nothing" : "LOUD/DISTORTED",
                  a_out4 == 1 ? "silent" : a_out4 == 2 ? "AUDIBLE" : "unknown");
    }

    /* 3. restore, exactly once */
    f.sleep_ms(f.ctx, GAP_MS);
    if (send_one(&be, "step 3 RESTORE OFF", E1_OFF, MSG_OFF) != 0) {
        say("\n*** RESTORE FAILED: 0x4088 may still be 0x2666 (Input 9 on Analog Out 3).\n"
            "*** Keep the volume down and run:  ff802-mixer-e1 --restore\n"
            "*** (or power-cycle the unit; whether the value survives is unknown)\n");
        out = EXIT_RESTORE_FAILED;
        goto close;
    }
    f.sleep_ms(f.ctx, SETTLE_MS);
    if (!test_sent) {
        say("result: FAILED at step 2 (test value not transferred); restored to off\n");
        out = EXIT_WRITE_FAILED;
        goto close;
    }

    g_signal = 0;                      /* the restore is done; ask once more */
    r = choose("\nAFTER the restore: is Analog Out 3 silent again?\n"
               "  1 yes, silent   2 no, still audible   3 cannot tell\nanswer: ",
               3, listen_max_s(), &a_after);
    if (r != ANS_LINE)
        a_after = 0;
    stamp("after the restore: Out 3 %s", a_after == 1 ? "silent" :
          a_after == 2 ? "STILL AUDIBLE" : a_after == 3 ? "unknown" : "no answer");

    int passed = a_test == 1 && a_out4 == 1 && a_after == 1;
    say("\nsummary:\n"
        "  MIDI: OFF, TEST and RESTORE transferred to ALSA (no reply from the unit;\n"
        "        no readback)\n"
        "  before:   %s\n  test:     Out 3 %s, Out 4 %s\n  restore:  Out 3 %s\n",
        a_before == 1 ? "silent" : a_before == 2 ? "audible" : "unknown",
        a_test == 1 ? "clearly audible" : a_test == 2 ? "very faint" :
        a_test == 3 ? "nothing" : a_test == 4 ? "loud/distorted" : "no answer",
        a_out4 == 1 ? "silent" : a_out4 == 2 ? "audible" : "unknown",
        a_after == 1 ? "silent" : a_after == 2 ? "STILL AUDIBLE" : "unknown");
    if (passed) {
        say("result: PASSED - 0x2666 routes Input 9 to Analog Out 3 only, audibly and\n"
            "        below full level (user check, not a measurement); restored to off\n");
        out = EXIT_PASSED;
    } else {
        say("result: NOT CONFIRMED - the expected effect was not confirmed; restored to\n"
            "        off. 'very faint' would point to a dB-linear value format (0x2666\n"
            "        not 30 %% linear); see docs/mixer_e1.md\n");
        out = a_after == 2 ? EXIT_RESTORE_FAILED : EXIT_NOT_CONFIRMED;
        if (a_after == 2)
            say("*** Analog Out 3 is still audible after the restore: run "
                "'ff802-mixer-e1 --restore', check the routing in OSCMix\n");
    }

close:
    f.close(f.ctx, &be);
    stamp("closed (exit %d)", out);
    if (g_log != NULL)
        fclose(g_log);
    return out;
}
