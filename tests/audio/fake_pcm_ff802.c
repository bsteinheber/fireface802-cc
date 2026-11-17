/*
 * Fake ALSA PCM device for container tests of the PipeWire integration.
 *
 * An external PCM plugin (snd_pcm_ioplug) that stands in for the hardware
 * PCM of the Fireface 802 in class compliant mode: 22 channels, S24_3LE,
 * 44.1 to 192 kHz. The hardware pointer advances with CLOCK_MONOTONIC, so
 * PipeWire can drive a real graph through it. Nothing is played or recorded:
 *
 * - playback: the plugin only measures the peak of every hardware channel
 *   and writes it to $FF802_FAKE_PCM_DIR/playback.peaks (deleting the file
 *   starts a new measurement)
 * - capture: hardware channel c carries the constant sample value
 *   (c + 1) * 65536, so a recording shows which channels arrived
 *
 * It is loaded only through a private ALSA configuration of the test
 * processes, as the implementation of the PCM type "hw" (UCM defines its
 * own pcm.hw with "type hw" in its library configuration):
 *
 *   pcm_type.hw { lib "<path>/libfake_pcm_ff802.so" open "_snd_pcm_ff802fake_open" }
 */
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#define HW_CHANNELS 22
#define BYTES_PER_SAMPLE 3

struct fake_pcm {
    snd_pcm_ioplug_t io;
    struct timespec start;
    int running;
    snd_pcm_uframes_t frames_at_start; /* hw position when started */
    int32_t peak[HW_CHANNELS];
    unsigned long long frames;
    struct timespec last_write;
    int written;
    char peaks_path[PATH_MAX];
};

/* The test starts a new measurement by deleting the peaks file. */
static void reset_if_requested(struct fake_pcm *f)
{
    if (f->written && f->peaks_path[0] != '\0' && access(f->peaks_path, F_OK) != 0) {
        memset(f->peak, 0, sizeof(f->peak));
        f->frames = 0;
        f->written = 0;
    }
}

static double elapsed(const struct timespec *a, const struct timespec *b)
{
    return (double)(b->tv_sec - a->tv_sec) + (double)(b->tv_nsec - a->tv_nsec) / 1e9;
}

static void write_peaks(struct fake_pcm *f)
{
    char tmp[PATH_MAX + 8];
    FILE *fp;
    int c;

    if (f->peaks_path[0] == '\0')
        return;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", f->peaks_path) >= (int)sizeof(tmp))
        return;
    fp = fopen(tmp, "w");
    if (fp == NULL)
        return;
    fprintf(fp, "rate %u\nframes %llu\n", f->io.rate, f->frames);
    for (c = 0; c < HW_CHANNELS; c++)
        fprintf(fp, "ch %d peak %ld\n", c, (long)f->peak[c]);
    if (fclose(fp) == 0 && rename(tmp, f->peaks_path) == 0)
        f->written = 1;
}

static int fake_start(snd_pcm_ioplug_t *io)
{
    struct fake_pcm *f = io->private_data;
    struct itimerspec its;
    long period_ns = (long)((double)io->period_size * 1e9 / io->rate);

    clock_gettime(CLOCK_MONOTONIC, &f->start);
    f->frames_at_start = io->hw_ptr;
    f->running = 1;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_nsec = period_ns;
    its.it_interval.tv_nsec = period_ns;
    timerfd_settime(io->poll_fd, 0, &its, NULL);
    return 0;
}

static int fake_stop(snd_pcm_ioplug_t *io)
{
    struct fake_pcm *f = io->private_data;
    struct itimerspec its;

    memset(&its, 0, sizeof(its));
    timerfd_settime(io->poll_fd, 0, &its, NULL);
    f->running = 0;
    if (io->stream == SND_PCM_STREAM_PLAYBACK)
        write_peaks(f);
    return 0;
}

static snd_pcm_sframes_t fake_pointer(snd_pcm_ioplug_t *io)
{
    struct fake_pcm *f = io->private_data;
    struct timespec now;
    unsigned long long pos;

    if (!f->running)
        return (snd_pcm_sframes_t)(io->hw_ptr % io->buffer_size);
    clock_gettime(CLOCK_MONOTONIC, &now);
    pos = f->frames_at_start + (unsigned long long)(elapsed(&f->start, &now) * io->rate);
    /* like real hardware: consuming past the written data is an underrun */
    if (io->stream == SND_PCM_STREAM_PLAYBACK && pos > io->appl_ptr)
        return -EPIPE;
    return (snd_pcm_sframes_t)(pos % io->buffer_size);
}

static int32_t get_s24_3le(const unsigned char *p)
{
    int32_t v = (int32_t)p[0] | ((int32_t)p[1] << 8) | ((int32_t)p[2] << 16);

    if (v & 0x800000)
        v -= 0x1000000;
    return v;
}

static void put_s24_3le(unsigned char *p, int32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
}

static snd_pcm_sframes_t fake_transfer(snd_pcm_ioplug_t *io,
                                       const snd_pcm_channel_area_t *areas,
                                       snd_pcm_uframes_t offset,
                                       snd_pcm_uframes_t size)
{
    struct fake_pcm *f = io->private_data;
    snd_pcm_uframes_t i;
    unsigned int c;

    if (io->stream == SND_PCM_STREAM_PLAYBACK)
        reset_if_requested(f);
    for (c = 0; c < io->channels && c < HW_CHANNELS; c++) {
        const snd_pcm_channel_area_t *a = &areas[c];
        unsigned char *base = (unsigned char *)a->addr + a->first / 8;
        unsigned int step = a->step / 8;

        for (i = 0; i < size; i++) {
            unsigned char *p = base + (offset + i) * step;

            if (io->stream == SND_PCM_STREAM_PLAYBACK) {
                int32_t v = get_s24_3le(p);

                if (v < 0)
                    v = -v;
                if (v > f->peak[c])
                    f->peak[c] = v;
            } else {
                put_s24_3le(p, (int32_t)(c + 1) * 65536);
            }
        }
    }
    f->frames += size;
    if (io->stream == SND_PCM_STREAM_PLAYBACK) {
        struct timespec now;

        clock_gettime(CLOCK_MONOTONIC, &now);
        if (elapsed(&f->last_write, &now) > 0.1) {
            write_peaks(f);
            f->last_write = now;
        }
    }
    return (snd_pcm_sframes_t)size;
}

static int fake_poll_revents(snd_pcm_ioplug_t *io, struct pollfd *pfd,
                             unsigned int nfds, unsigned short *revents)
{
    uint64_t expirations;

    (void)nfds;
    if (pfd[0].revents & POLLIN) {
        if (read(io->poll_fd, &expirations, sizeof(expirations)) < 0) {
            /* nothing pending; not an error */
        }
        *revents = io->stream == SND_PCM_STREAM_PLAYBACK ? POLLOUT : POLLIN;
    } else {
        *revents = 0;
    }
    return 0;
}

static int fake_close(snd_pcm_ioplug_t *io)
{
    struct fake_pcm *f = io->private_data;

    if (io->stream == SND_PCM_STREAM_PLAYBACK)
        write_peaks(f);
    close(io->poll_fd);
    free(f);
    return 0;
}

static const snd_pcm_ioplug_callback_t callbacks = {
    .start = fake_start,
    .stop = fake_stop,
    .pointer = fake_pointer,
    .transfer = fake_transfer,
    .poll_revents = fake_poll_revents,
    .close = fake_close,
};

static int set_constraints(snd_pcm_ioplug_t *io)
{
    static const unsigned int access[] = {
        SND_PCM_ACCESS_MMAP_INTERLEAVED, SND_PCM_ACCESS_RW_INTERLEAVED,
    };
    static const unsigned int formats[] = { SND_PCM_FORMAT_S24_3LE };
    static const unsigned int rates[] = { 44100, 48000, 88200, 96000, 176400, 192000 };
    int err;

    if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS, 2, access)) < 0 ||
        (err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT, 1, formats)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_CHANNELS,
                                               HW_CHANNELS, HW_CHANNELS)) < 0 ||
        (err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_RATE, 6, rates)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIOD_BYTES,
                                               HW_CHANNELS * BYTES_PER_SAMPLE * 16,
                                               HW_CHANNELS * BYTES_PER_SAMPLE * 16384)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_BUFFER_BYTES,
                                               HW_CHANNELS * BYTES_PER_SAMPLE * 64,
                                               HW_CHANNELS * BYTES_PER_SAMPLE * 65536)) < 0 ||
        (err = snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64)) < 0)
        return err;
    return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(ff802fake)
{
    snd_config_iterator_t i, next;
    const char *card = "";
    const char *id_want = getenv("FF802_FAKE_ID");
    const char *dir = getenv("FF802_FAKE_PCM_DIR");
    struct fake_pcm *f;
    long dev = 0;
    int err;

    (void)root;

    snd_config_for_each(i, next, conf) {
        snd_config_t *n = snd_config_iterator_entry(i);
        const char *id;

        if (snd_config_get_id(n, &id) < 0)
            continue;
        if (!strcmp(id, "comment") || !strcmp(id, "type") || !strcmp(id, "hint") ||
            !strcmp(id, "subdevice"))
            continue;
        if (!strcmp(id, "card")) {
            long num;

            if (snd_config_get_string(n, &card) < 0) {
                if (snd_config_get_integer(n, &num) < 0)
                    return -EINVAL;
                card = num == 1 ? "1" : "";
            }
            continue;
        }
        if (!strcmp(id, "device")) {
            if (snd_config_get_integer(n, &dev) < 0)
                return -EINVAL;
            continue;
        }
        SNDERR("ff802fake pcm: unknown field %s", id);
        return -EINVAL;
    }

    /* only the fake card exists: hw:1 or its id, device 0 */
    if (id_want == NULL || id_want[0] == '\0')
        id_want = "F80224047128";
    if ((strcmp(card, "1") != 0 && strcmp(card, id_want) != 0) || dev != 0)
        return -ENODEV;

    f = calloc(1, sizeof(*f));
    if (f == NULL)
        return -ENOMEM;
    if (dir != NULL && dir[0] != '\0' && stream == SND_PCM_STREAM_PLAYBACK &&
        snprintf(f->peaks_path, sizeof(f->peaks_path), "%s/playback.peaks", dir) >=
            (int)sizeof(f->peaks_path)) {
        free(f);
        return -ENAMETOOLONG;
    }

    f->io.version = SND_PCM_IOPLUG_VERSION;
    f->io.name = "Fireface 802 fake PCM";
    f->io.flags = SND_PCM_IOPLUG_FLAG_MONOTONIC;
    f->io.mmap_rw = 0;
    f->io.callback = &callbacks;
    f->io.private_data = f;
    f->io.poll_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    f->io.poll_events = POLLIN;
    if (f->io.poll_fd < 0) {
        free(f);
        return -errno;
    }

    err = snd_pcm_ioplug_create(&f->io, name, stream, mode);
    if (err < 0) {
        close(f->io.poll_fd);
        free(f);
        return err;
    }
    err = set_constraints(&f->io);
    if (err < 0) {
        snd_pcm_ioplug_delete(&f->io);
        return err;
    }
    *pcmp = f->io.pcm;
    return 0;
}

SND_PCM_PLUGIN_SYMBOL(ff802fake)
