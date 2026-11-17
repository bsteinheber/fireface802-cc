/*
 * ff802-chanmap: loopback measurement of the physical channel assignment of
 * the original Fireface 802 in class compliant mode (hardware test T2).
 *
 * The measurement core works on an abstract full-duplex audio backend, so
 * the complete flow (signal generation, level guards, detection, abort
 * handling) runs unchanged against the real ALSA device and against the
 * simulated device used by the tests.
 *
 * Safety properties enforced here, not configurable:
 *  - the test signal peak never exceeds CHANMAP_MAX_PEAK (-40 dBFS); every
 *    buffer is checked before it is handed to the backend
 *  - no gain is ever applied or adjusted, no mixer control is written
 *  - any capture sample above CHANMAP_ABORT_INPUT (-20 dBFS) aborts
 *  - a tone that does not decay after it ended (feedback) aborts
 *  - an abort request (signal) stops the signal within one period
 *  - on every exit path silence is written and the backend is closed
 */
#ifndef FF802_CHANMAP_H
#define FF802_CHANMAP_H

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

#define CHANMAP_RATE        48000u
#define CHANMAP_CHANNELS    22u     /* playback and capture channels */
#define CHANMAP_TEST_PLAY   12u     /* playback indices 0..11 are tested */
#define CHANMAP_PERIOD      1024u   /* frames per period */

/* -40 dBFS: absolute upper bound for any output sample (linear). */
#define CHANMAP_MAX_PEAK    0.01
/* Amplitude actually used: slightly below the bound (-40.09 dBFS). */
#define CHANMAP_AMPLITUDE   0.0099
/* Any capture sample above -20 dBFS aborts the measurement. */
#define CHANMAP_ABORT_INPUT 0.1

#define CHANMAP_TONE_HZ     1000.0

/* Timing of one step (one playback index), in frames at 48 kHz. */
#define CHANMAP_PRE_FRAMES    14400u  /* 0.30 s silence (noise reference) */
#define CHANMAP_CHIRP_FRAMES  24000u  /* 0.50 s chirp 300..3000 Hz       */
#define CHANMAP_GAP_FRAMES    14400u  /* 0.30 s silence (> MAX_LATENCY)  */
#define CHANMAP_TONE_FRAMES   72000u  /* 1.50 s sine 1 kHz               */
#define CHANMAP_TAIL_FRAMES   24000u  /* 0.50 s silence (decay check)    */
#define CHANMAP_STEP_FRAMES (CHANMAP_PRE_FRAMES + CHANMAP_CHIRP_FRAMES + \
    CHANMAP_GAP_FRAMES + CHANMAP_TONE_FRAMES + CHANMAP_TAIL_FRAMES)
#define CHANMAP_MAX_LATENCY   12000u  /* 0.25 s searched for the chirp   */

/* Detection thresholds. */
#define CHANMAP_DETECT_MIN_DBFS  (-100.0)
#define CHANMAP_DETECT_MIN_SNR   20.0
/* After the tone the level must have fallen by at least this much. */
#define CHANMAP_DECAY_MIN_DB     30.0

/* ------------------------------------------------------------------------ */
/* Pure signal functions                                                     */

/* Sample of the test signal at frame n of a step (0 <= n < STEP_FRAMES),
 * for the one active channel. Always within [-AMPLITUDE, AMPLITUDE]. */
double chanmap_step_sample(size_t n);

/* Reference chirp sample k (0 <= k < CHIRP_FRAMES), unit amplitude. */
double chanmap_chirp_ref(size_t k);

/* 1 if every sample satisfies |x| <= CHANMAP_MAX_PEAK, else 0. */
int chanmap_output_guard(const float *buf, size_t samples);

/* Largest |x| in the buffer. */
double chanmap_peak(const float *buf, size_t samples);

/* Amplitude of the 1 kHz component of x[0..n) (Goertzel). */
double chanmap_tone_amplitude(const float *x, size_t n, size_t stride);

/* Amplitude in dBFS (-200 for zero). */
double chanmap_dbfs(double amplitude);

/* Find the chirp in x (stride = channels): returns the lag in frames
 * (0..max_lag) of the largest |correlation| and sets *polarity to +1/-1. */
size_t chanmap_find_chirp(const float *x, size_t frames, size_t stride,
                          size_t max_lag, int *polarity);

/* ------------------------------------------------------------------------ */
/* Backend                                                                   */

struct chanmap_backend {
    void *ctx;
    /* Open playback and capture with CHANMAP_CHANNELS channels at
     * CHANMAP_RATE. 0, -EBUSY if the device is in use, other -errno. */
    int (*open)(void *ctx);
    /* Write/read exactly `frames` interleaved float frames (blocking).
     * Return 0 or a negative errno. */
    int (*write)(void *ctx, const float *buf, size_t frames);
    int (*read)(void *ctx, float *buf, size_t frames);
    /* Stop immediately and release the device. Safe to call twice. */
    void (*close)(void *ctx);
    /* Read-only text snapshot of all mixer controls (may be NULL). */
    int (*mixer_snapshot)(void *ctx, char *out, size_t len);
};

/* ------------------------------------------------------------------------ */
/* Measurement                                                               */

enum chanmap_status {
    CHANMAP_OK = 0,
    CHANMAP_ERR_OPEN,           /* device could not be opened            */
    CHANMAP_ERR_BUSY,           /* device stayed busy (PipeWire)         */
    CHANMAP_ERR_RESERVE,        /* pw-reserve failed or exited           */
    CHANMAP_ERR_IO,             /* read/write error (xrun, unplug, ...)  */
    CHANMAP_ERR_NOMEM,
    CHANMAP_ABORT_SIGNAL,       /* interrupted by the user               */
    CHANMAP_ABORT_INPUT_LEVEL,  /* capture above -20 dBFS                */
    CHANMAP_ABORT_FEEDBACK,     /* tone did not decay                    */
    CHANMAP_ABORT_OUTPUT_GUARD, /* output sample above -40 dBFS          */
    CHANMAP_ABORT_MIXER_CHANGED /* mixer state changed during the run    */
};

const char *chanmap_status_text(enum chanmap_status s);

struct chanmap_hit {
    unsigned play;        /* playback index                     */
    unsigned capture;     /* capture channel                    */
    double level_dbfs;    /* 1 kHz level on the capture channel */
    double snr_db;        /* level above the noise reference    */
    double latency_ms;    /* from the chirp correlation         */
    int polarity;         /* +1 or -1                           */
};

#define CHANMAP_MAX_HITS (CHANMAP_TEST_PLAY * CHANMAP_CHANNELS)

struct chanmap_result {
    enum chanmap_status status;
    unsigned steps_done;      /* completed steps (0..TEST_PLAY)      */
    unsigned n_hits;
    struct chanmap_hit hits[CHANMAP_MAX_HITS];
    double max_input_peak;    /* largest capture sample seen (linear) */
    double max_output_peak;   /* largest output sample written        */
    int mixer_checked;        /* 1 if snapshots were compared         */
    char detail[256];         /* human readable reason for an abort   */
};

struct chanmap_config {
    /* Set asynchronously (signal handler) to request an abort. */
    volatile sig_atomic_t *abort_flag;
    /* Optional progress log (NULL: quiet). */
    FILE *log;
};

/* Run all steps against an already opened backend. Writes silence and
 * closes the backend on every path. */
enum chanmap_status chanmap_measure(const struct chanmap_backend *be,
                                    const struct chanmap_config *cfg,
                                    struct chanmap_result *res);

/* ------------------------------------------------------------------------ */
/* Card identification (procfs)                                               */

struct chanmap_card {
    int index;            /* ALSA card number            */
    char id[32];          /* ALSA card id (hw:CARD=<id>) */
    char longname[128];
};

/* Find the single original Fireface 802 (usbid 0424:3fdd, long name with
 * "Fireface 802" and without "802 FS") below proc_root (/proc/asound).
 * Returns 1 if found, 0 if none, -2 if several, -1 on a read error. */
int chanmap_find_card(const char *proc_root, struct chanmap_card *card,
                      char *why, size_t why_len);

/* Check card info as reported by the control device: driver USB-Audio,
 * components contain USB0424:3fdd, long name names the original 802. */
int chanmap_card_info_ok(const char *driver, const char *components,
                         const char *longname);

/* ------------------------------------------------------------------------ */
/* PipeWire device reservation (pw-reserve as child process)                 */

struct chanmap_reserve {
    pid_t pid;            /* 0 if not running                       */
    int exit_status;      /* wait status once the child has exited  */
};

/* Start `prog -n Audio<card> -r` in its own process group, output to
 * logfile. 0 or -errno. */
int chanmap_reserve_start(struct chanmap_reserve *r, const char *prog,
                          int card, const char *logfile);
/* 1 while the child is running. */
int chanmap_reserve_alive(struct chanmap_reserve *r);
/* SIGINT, wait up to timeout_ms, then SIGKILL. Returns the child's exit
 * status, or -1. Safe to call when not running. */
int chanmap_reserve_stop(struct chanmap_reserve *r, unsigned timeout_ms);

/* Open the backend while a reservation is being negotiated: retries while
 * the device is busy, up to timeout_ms; fails early if the reservation
 * process exits. r may be NULL (no reservation). */
enum chanmap_status chanmap_open_with_reserve(const struct chanmap_backend *be,
                                              struct chanmap_reserve *r,
                                              unsigned timeout_ms,
                                              volatile sig_atomic_t *abort_flag);

/* ------------------------------------------------------------------------ */
/* Positions, result files, report                                           */

/* Known cable positions: out1..out8, hp1, hp2. Returns 1 if valid. */
int chanmap_position_valid(const char *name);

/* Expected playback indices and polarities for a position; returns the
 * count (1 or 2). */
unsigned chanmap_position_expect(const char *name, unsigned play[2],
                                 int polarity[2]);

int chanmap_write_result(FILE *f, const char *position, unsigned input,
                         const char *card_id, const struct chanmap_result *res);

struct chanmap_pos_file {
    char position[16];
    unsigned input;
    enum chanmap_status status;
    unsigned n_hits;
    struct chanmap_hit hits[CHANMAP_MAX_HITS];
};

int chanmap_read_result(FILE *f, struct chanmap_pos_file *p);

enum chanmap_verdict {
    CHANMAP_VERIFIED = 0,
    CHANMAP_MISMATCH = 1,
    CHANMAP_INCOMPLETE = 2
};

/* Compare the position files against the profile and print a table. */
enum chanmap_verdict chanmap_report(const struct chanmap_pos_file *files,
                                    size_t n, FILE *out);

#endif /* FF802_CHANMAP_H */
