/*
 * ff802-chanmap - loopback measurement of the channel assignment of the
 * original RME Fireface 802 in class compliant mode (hardware test T2).
 *
 * See docs/audio_t2_plan.md. Only run with nothing but a patch cable
 * connected: no loudspeakers, amplifiers or headphones.
 */
#define _GNU_SOURCE
#include "ff802/chanmap.h"
#include "ff802/chanmap_alsa.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FF802_VERSION
#define FF802_VERSION "unknown"
#endif

#define PROC_ASOUND "/proc/asound"
#define RESERVE_PROG "pw-reserve"
#define OPEN_TIMEOUT_MS 10000u
#define RESERVE_STOP_MS 3000u

enum {
    EXIT_OK = 0,
    EXIT_MISMATCH = 1,
    EXIT_USAGE = 2,
    EXIT_NO_CARD = 3,
    EXIT_SAFETY = 4,
    EXIT_BUSY = 5,
    EXIT_AUDIO = 6,
    EXIT_INCOMPLETE = 7,
    EXIT_SIGNAL = 130
};

static volatile sig_atomic_t abort_flag;

static void on_signal(int sig)
{
    (void)sig;
    abort_flag = 1;
}

static void usage(FILE *f)
{
    fprintf(f,
        "Usage: ff802-chanmap check\n"
        "       ff802-chanmap measure --position <pos> [--input 1|2] --confirm-loopback-only\n"
        "                             [--out <dir>] [--no-reserve]\n"
        "       ff802-chanmap report [--out <dir>]\n"
        "       ff802-chanmap --help | --version\n"
        "\n"
        "Loopback measurement of the Fireface 802 channel assignment (test T2).\n"
        "  check     read-only: identify the card, show PCM state and mixer controls\n"
        "  measure   play a -40 dBFS test signal on playback 0..11, one after the\n"
        "            other, record all 22 inputs and store the result\n"
        "  report    compare all stored results with the UCM profile\n"
        "\n"
        "  <pos>     cable position on the output side: out1..out8, hp1, hp2\n"
        "  --input   Line input the cable is plugged into (default 1)\n"
        "  --out     result directory (default ./build-t2)\n"
        "  --no-reserve  do not run pw-reserve (only if PipeWire is not running)\n"
        "\n"
        "Before 'measure': unplug all loudspeakers, amplifiers and headphones,\n"
        "connect only one cable from the output <pos> to a rear Line input\n"
        "(never a microphone input), and make sure no 48 V indicator is lit.\n"
        "The signal level is fixed at -40 dBFS and cannot be changed.\n"
        "\n"
        "Exit status: 0 ok/verified, 1 mismatch, 2 usage, 3 no Fireface 802,\n"
        "4 safety abort, 5 device busy or reservation failed, 6 audio error,\n"
        "7 report incomplete, 130 interrupted.\n");
}

/* stdout plus a log file */
struct tee {
    FILE *a, *b;
};

static ssize_t tee_write(void *c, const char *buf, size_t n)
{
    struct tee *t = c;
    fwrite(buf, 1, n, t->a);
    fflush(t->a);
    if (t->b) {
        fwrite(buf, 1, n, t->b);
        fflush(t->b);
    }
    return (ssize_t)n;
}

static int tee_close(void *c)
{
    struct tee *t = c;
    if (t->b)
        fclose(t->b);
    return 0;
}

static int status_exit(enum chanmap_status s)
{
    switch (s) {
    case CHANMAP_OK: return EXIT_OK;
    case CHANMAP_ABORT_SIGNAL: return EXIT_SIGNAL;
    case CHANMAP_ABORT_INPUT_LEVEL:
    case CHANMAP_ABORT_FEEDBACK:
    case CHANMAP_ABORT_OUTPUT_GUARD:
    case CHANMAP_ABORT_MIXER_CHANGED: return EXIT_SAFETY;
    case CHANMAP_ERR_BUSY:
    case CHANMAP_ERR_RESERVE: return EXIT_BUSY;
    default: return EXIT_AUDIO;
    }
}

static int find_card(struct chanmap_card *card)
{
    char why[256];
    int r = chanmap_find_card(PROC_ASOUND, card, why, sizeof why);
    if (r == 1)
        return 0;
    fprintf(stderr, "ff802-chanmap: %s\n", why);
    return r == 0 ? EXIT_NO_CARD : EXIT_USAGE;
}

static void cat_file(const char *path)
{
    char line[256];
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("  (%s not readable)\n", path);
        return;
    }
    while (fgets(line, sizeof line, f))
        printf("  %s", line);
    fclose(f);
}

static int in_path(const char *prog)
{
    const char *path = getenv("PATH");
    char buf[1024];
    if (!path)
        return 0;
    while (*path) {
        size_t n = strcspn(path, ":");
        snprintf(buf, sizeof buf, "%.*s/%s", (int)n, path, prog);
        if (access(buf, X_OK) == 0)
            return 1;
        path += n;
        if (*path == ':')
            path++;
    }
    return 0;
}

static int cmd_check(void)
{
    struct chanmap_card card;
    struct chanmap_alsa a;
    struct chanmap_backend be;
    char path[256];
    static char mixer[65536];
    snd_ctl_card_info_t *info;
    int rc;

    if ((rc = find_card(&card)) != 0)
        return rc;
    printf("card %d: id %s\n  %s\n", card.index, card.id, card.longname);
    if (chanmap_alsa_setup(&a, card.id) < 0) {
        fprintf(stderr, "ff802-chanmap: unusable card id '%s'\n", card.id);
        return EXIT_USAGE;
    }
    snd_ctl_card_info_alloca(&info);
    if (snd_ctl_open(&a.ctl, a.ctl_name, 0) < 0 || snd_ctl_card_info(a.ctl, info) < 0) {
        fprintf(stderr, "ff802-chanmap: cannot open control device %s\n", a.ctl_name);
        if (a.ctl)
            snd_ctl_close(a.ctl);
        return EXIT_AUDIO;
    }
    printf("control device: driver %s, components %s\n",
           snd_ctl_card_info_get_driver(info), snd_ctl_card_info_get_components(info));
    if (!chanmap_card_info_ok(snd_ctl_card_info_get_driver(info),
                              snd_ctl_card_info_get_components(info),
                              snd_ctl_card_info_get_longname(info))) {
        fprintf(stderr, "ff802-chanmap: control device does not identify an original Fireface 802\n");
        snd_ctl_close(a.ctl);
        return EXIT_NO_CARD;
    }
    printf("PCM %s\n", a.pcm_name);
    snprintf(path, sizeof path, PROC_ASOUND "/card%d/pcm0p/sub0/status", card.index);
    printf("playback state:\n");
    cat_file(path);
    snprintf(path, sizeof path, PROC_ASOUND "/card%d/pcm0c/sub0/status", card.index);
    printf("capture state:\n");
    cat_file(path);
    snprintf(path, sizeof path, PROC_ASOUND "/card%d/stream0", card.index);
    printf("stream formats:\n");
    cat_file(path);
    chanmap_alsa_backend(&a, &be);
    be.mixer_snapshot(be.ctx, mixer, sizeof mixer);
    printf("mixer controls (read only):\n%s", mixer[0] ? mixer : "  (none)\n");
    printf("pw-reserve: %s\n", in_path(RESERVE_PROG) ? "found" : "NOT FOUND (package pipewire-bin)");
    snd_ctl_close(a.ctl);
    return EXIT_OK;
}

static int ensure_dir(const char *dir)
{
    if (mkdir(dir, 0755) == 0 || errno == EEXIST)
        return 0;
    fprintf(stderr, "ff802-chanmap: cannot create %s: %s\n", dir, strerror(errno));
    return -1;
}

static int cmd_measure(const char *pos, unsigned input, const char *dir, int reserve)
{
    struct chanmap_card card;
    struct chanmap_alsa a;
    struct chanmap_backend be;
    struct chanmap_reserve rsv = { 0, 0 };
    static struct chanmap_result res;
    struct chanmap_config cfg;
    struct sigaction sa;
    struct tee t;
    cookie_io_functions_t io = { NULL, tee_write, NULL, tee_close };
    char path[512];
    enum chanmap_status st;
    unsigned play[2];
    int pol[2], rc;
    unsigned n;
    FILE *log, *out;

    if ((rc = find_card(&card)) != 0)
        return rc;
    if (chanmap_alsa_setup(&a, card.id) < 0) {
        fprintf(stderr, "ff802-chanmap: unusable card id '%s'\n", card.id);
        return EXIT_USAGE;
    }
    if (ensure_dir(dir) < 0)
        return EXIT_USAGE;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal; /* no SA_RESTART: blocking I/O returns */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);

    snprintf(path, sizeof path, "%s/%s-in%u.log", dir, pos, input);
    t.a = stdout;
    t.b = fopen(path, "w");
    log = fopencookie(&t, "w", io);
    if (!log)
        log = stdout;
    setvbuf(log, NULL, _IONBF, 0);

    fprintf(log, "ff802-chanmap %s measure: position %s, input %u\n", FF802_VERSION, pos, input);
    fprintf(log, "card %d: id %s, %s\n", card.index, card.id, card.longname);
    fprintf(log, "signal: playback 0..%u one after the other, chirp + 1 kHz, peak %.2f dBFS (limit %.0f dBFS)\n",
            CHANMAP_TEST_PLAY - 1u, chanmap_dbfs(CHANMAP_AMPLITUDE), chanmap_dbfs(CHANMAP_MAX_PEAK));
    n = chanmap_position_expect(pos, play, pol);
    if (n == 2)
        fprintf(log, "expected: playback %u (+) and %u (-) arrive on capture %u\n",
                play[0], play[1], input - 1u);
    else
        fprintf(log, "expected: playback %u arrives on capture %u\n", play[0], input - 1u);

    chanmap_alsa_backend(&a, &be);
    if (reserve) {
        char rlog[512];
        snprintf(rlog, sizeof rlog, "%s/pw-reserve.log", dir);
        fprintf(log, "requesting the card from PipeWire: %s -n Audio%d -r\n", RESERVE_PROG, card.index);
        if (chanmap_reserve_start(&rsv, RESERVE_PROG, card.index, rlog) < 0) {
            fprintf(log, "error: cannot start %s\n", RESERVE_PROG);
            fclose(log);
            return EXIT_BUSY;
        }
    }
    st = chanmap_open_with_reserve(&be, reserve ? &rsv : NULL, OPEN_TIMEOUT_MS, &abort_flag);
    if (st != CHANMAP_OK) {
        memset(&res, 0, sizeof res);
        res.status = st;
        snprintf(res.detail, sizeof res.detail, "%s", chanmap_status_text(st));
        fprintf(log, "error: %s\n", chanmap_status_text(st));
        if (st == CHANMAP_ERR_BUSY)
            fprintf(log, "hint: the card is still in use. Is WirePlumber running with device\n"
                         "      reservation (D-Bus)? Close all audio programs and retry.\n");
        else if (st == CHANMAP_ERR_RESERVE)
            fprintf(log, "hint: pw-reserve ended early, see %s/pw-reserve.log\n", dir);
    } else {
        fprintf(log, "opened %s\n", a.info);
        cfg.abort_flag = &abort_flag;
        cfg.log = log;
        st = chanmap_measure(&be, &cfg, &res); /* closes the device */
    }
    if (reserve) {
        int ws = chanmap_reserve_stop(&rsv, RESERVE_STOP_MS);
        fprintf(log, "reservation released (pw-reserve wait status %d)\n", ws);
    }

    snprintf(path, sizeof path, "%s/%s-in%u.txt", dir, pos, input);
    out = fopen(path, "w");
    if (!out || chanmap_write_result(out, pos, input, card.id, &res) < 0) {
        fprintf(log, "error: cannot write %s\n", path);
        if (out)
            fclose(out);
        fclose(log);
        return EXIT_AUDIO;
    }
    fclose(out);

    fprintf(log, "result: %s%s%s\n", chanmap_status_text(res.status),
            res.detail[0] && res.status != CHANMAP_OK ? " - " : "",
            res.status != CHANMAP_OK ? res.detail : "");
    fprintf(log, "steps completed %u/%u, max output %.2f dBFS, max input %.2f dBFS\n",
            res.steps_done, CHANMAP_TEST_PLAY, chanmap_dbfs(res.max_output_peak),
            chanmap_dbfs(res.max_input_peak));
    for (unsigned i = 0; i < res.n_hits; i++)
        fprintf(log, "  playback %2u -> capture %2u  %6.1f dBFS  SNR %5.1f dB  latency %6.2f ms  polarity %s\n",
                res.hits[i].play, res.hits[i].capture, res.hits[i].level_dbfs, res.hits[i].snr_db,
                res.hits[i].latency_ms, res.hits[i].polarity > 0 ? "+" : "-");
    if (res.n_hits == 0)
        fprintf(log, "  no signal detected on any input\n");
    fprintf(log, "stored %s\n", path);
    fclose(log);
    return status_exit(st);
}

static int cmd_report(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    struct chanmap_pos_file *files = NULL;
    size_t n = 0;
    enum chanmap_verdict v;
    char **names = NULL;
    size_t nn = 0;

    if (!d) {
        fprintf(stderr, "ff802-chanmap: cannot read %s\n", dir);
        return EXIT_USAGE;
    }
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        char **nn2;
        if (l < 5 || strcmp(e->d_name + l - 4, ".txt") != 0)
            continue;
        nn2 = realloc(names, (nn + 1) * sizeof *names);
        if (!nn2)
            break;
        names = nn2;
        names[nn++] = strdup(e->d_name);
    }
    closedir(d);
    /* deterministic order */
    for (size_t i = 1; i < nn; i++)
        for (size_t j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char *tmp = names[j];
            names[j] = names[j - 1];
            names[j - 1] = tmp;
        }
    files = calloc(nn ? nn : 1, sizeof *files);
    if (!files)
        return EXIT_AUDIO;
    for (size_t i = 0; i < nn; i++) {
        char path[1024];
        FILE *f;
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        f = fopen(path, "r");
        if (f && chanmap_read_result(f, &files[n]) == 0)
            n++;
        else
            fprintf(stderr, "ff802-chanmap: ignoring %s (not a result file)\n", path);
        if (f)
            fclose(f);
        free(names[i]);
    }
    free(names);
    printf("%zu result file(s) in %s\n", n, dir);
    v = chanmap_report(files, n, stdout);
    free(files);
    return v == CHANMAP_VERIFIED ? EXIT_OK : v == CHANMAP_MISMATCH ? EXIT_MISMATCH : EXIT_INCOMPLETE;
}

int main(int argc, char **argv)
{
    const char *cmd = NULL, *pos = NULL, *dir = "build-t2";
    unsigned input = 1;
    int confirm = 0, reserve = 1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(stdout);
            return EXIT_OK;
        } else if (!strcmp(a, "--version")) {
            printf("ff802-chanmap %s\n", FF802_VERSION);
            return EXIT_OK;
        } else if (!strcmp(a, "--position") && i + 1 < argc) {
            pos = argv[++i];
        } else if (!strcmp(a, "--input") && i + 1 < argc) {
            const char *v = argv[++i];
            if (strcmp(v, "1") && strcmp(v, "2")) {
                fprintf(stderr, "ff802-chanmap: --input expects 1 or 2\n");
                return EXIT_USAGE;
            }
            input = (unsigned)(v[0] - '0');
        } else if (!strcmp(a, "--out") && i + 1 < argc) {
            dir = argv[++i];
        } else if (!strcmp(a, "--confirm-loopback-only")) {
            confirm = 1;
        } else if (!strcmp(a, "--no-reserve")) {
            reserve = 0;
        } else if (a[0] != '-' && cmd == NULL) {
            cmd = a;
        } else {
            fprintf(stderr, "ff802-chanmap: unexpected argument '%s'\n", a);
            usage(stderr);
            return EXIT_USAGE;
        }
    }
    if (cmd == NULL) {
        usage(stderr);
        return EXIT_USAGE;
    }
    if (geteuid() == 0) {
        fprintf(stderr, "ff802-chanmap: do not run as root (PipeWire runs in the user session)\n");
        return EXIT_USAGE;
    }
    if (!strcmp(cmd, "check"))
        return cmd_check();
    if (!strcmp(cmd, "report"))
        return cmd_report(dir);
    if (strcmp(cmd, "measure") != 0) {
        fprintf(stderr, "ff802-chanmap: unknown command '%s'\n", cmd);
        return EXIT_USAGE;
    }
    if (pos == NULL || !chanmap_position_valid(pos)) {
        fprintf(stderr, "ff802-chanmap: --position expects out1..out8, hp1 or hp2\n");
        return EXIT_USAGE;
    }
    if (!confirm) {
        fprintf(stderr,
                "ff802-chanmap: refusing to play without --confirm-loopback-only.\n"
                "Confirm first that\n"
                "  - no loudspeaker, amplifier or headphone is connected to the Fireface,\n"
                "  - only one cable connects output %s to rear Line input %u,\n"
                "  - no microphone input is used and no 48 V indicator is lit,\n"
                "  - all audio applications are closed.\n", pos, input);
        return EXIT_USAGE;
    }
    return cmd_measure(pos, input, dir, reserve);
}
