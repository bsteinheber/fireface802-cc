/*
 * ALSA backend of ff802-chanmap.
 *
 * Read-only towards the mixer: this file only uses snd_ctl_card_info,
 * snd_ctl_elem_list, snd_ctl_elem_info and snd_ctl_elem_read. The build
 * checks the linked binary for forbidden write symbols.
 */
#include "ff802/chanmap_alsa.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CH CHANMAP_CHANNELS

static int id_valid(const char *id)
{
    size_t n = strlen(id);
    if (n == 0 || n > 31)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char)id[i]) && id[i] != '_')
            return 0;
    return 1;
}

int chanmap_alsa_setup(struct chanmap_alsa *a, const char *card_id)
{
    memset(a, 0, sizeof *a);
    if (!id_valid(card_id))
        return -EINVAL;
    snprintf(a->pcm_name, sizeof a->pcm_name, "hw:CARD=%s,DEV=0", card_id);
    snprintf(a->ctl_name, sizeof a->ctl_name, "hw:CARD=%s", card_id);
    a->verify_card = 1;
    return 0;
}

int chanmap_alsa_setup_unverified(struct chanmap_alsa *a, const char *pcm_name)
{
    memset(a, 0, sizeof *a);
    snprintf(a->pcm_name, sizeof a->pcm_name, "%s", pcm_name);
    a->verify_card = 0;
    return 0;
}

static void alsa_close(void *ctx)
{
    struct chanmap_alsa *a = ctx;
    if (a->play) {
        snd_pcm_drop(a->play);
        if (a->linked)
            snd_pcm_unlink(a->play);
        a->linked = 0;
        snd_pcm_close(a->play);
        a->play = NULL;
    }
    if (a->cap) {
        snd_pcm_drop(a->cap);
        snd_pcm_close(a->cap);
        a->cap = NULL;
    }
    if (a->ctl) {
        snd_ctl_close(a->ctl);
        a->ctl = NULL;
    }
    free(a->raw);
    a->raw = NULL;
}

static int set_params(struct chanmap_alsa *a, snd_pcm_t *pcm, int is_play)
{
    static const snd_pcm_format_t formats[] = {
        SND_PCM_FORMAT_S32_LE, SND_PCM_FORMAT_S24_3LE, SND_PCM_FORMAT_S24_LE,
        SND_PCM_FORMAT_S16_LE
    };
    snd_pcm_hw_params_t *hw;
    snd_pcm_sw_params_t *sw;
    snd_pcm_uframes_t period = CHANMAP_PERIOD, buffer = 4u * CHANMAP_PERIOD;
    unsigned rate = CHANMAP_RATE;
    int err, dir = 0;
    size_t i;

    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_sw_params_alloca(&sw);
    if ((err = snd_pcm_hw_params_any(pcm, hw)) < 0)
        return err;
    if ((err = snd_pcm_hw_params_set_rate_resample(pcm, hw, 0)) < 0 ||
        (err = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0 ||
        (err = snd_pcm_hw_params_set_channels(pcm, hw, CH)) < 0)
        return err;
    for (i = 0; i < sizeof formats / sizeof formats[0]; i++) {
        /* playback decides, capture must use the same format */
        if (!is_play && formats[i] != a->format)
            continue;
        if (snd_pcm_hw_params_test_format(pcm, hw, formats[i]) == 0)
            break;
    }
    if (i == sizeof formats / sizeof formats[0])
        return -EINVAL;
    if ((err = snd_pcm_hw_params_set_format(pcm, hw, formats[i])) < 0)
        return err;
    a->format = formats[i];
    if ((err = snd_pcm_hw_params_set_rate(pcm, hw, rate, 0)) < 0)
        return err;
    if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hw, &period, &dir)) < 0 ||
        (err = snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &buffer)) < 0)
        return err;
    if ((err = snd_pcm_hw_params(pcm, hw)) < 0)
        return err;

    if ((err = snd_pcm_sw_params_current(pcm, sw)) < 0)
        return err;
    /* playback starts once the buffer is full (prefilled with silence) */
    if ((err = snd_pcm_sw_params_set_start_threshold(pcm, sw, is_play ? buffer : 1)) < 0)
        return err;
    if ((err = snd_pcm_sw_params(pcm, sw)) < 0)
        return err;
    if (is_play)
        snprintf(a->info, sizeof a->info, "%s %s %u Hz %u ch period %lu buffer %lu",
                 a->pcm_name, snd_pcm_format_name(a->format), rate, CH,
                 (unsigned long)period, (unsigned long)buffer);
    return (int)buffer;
}

static int verify(struct chanmap_alsa *a)
{
    snd_ctl_card_info_t *info;
    int err;

    snd_ctl_card_info_alloca(&info);
    if ((err = snd_ctl_open(&a->ctl, a->ctl_name, 0)) < 0)
        return err;
    if ((err = snd_ctl_card_info(a->ctl, info)) < 0)
        return err;
    if (!chanmap_card_info_ok(snd_ctl_card_info_get_driver(info),
                              snd_ctl_card_info_get_components(info),
                              snd_ctl_card_info_get_longname(info)))
        return -ENODEV;
    return 0;
}

static int alsa_open(void *ctx)
{
    struct chanmap_alsa *a = ctx;
    int err, buffer;

    if (a->verify_card && (err = verify(a)) < 0)
        goto fail;
    err = snd_pcm_open(&a->play, a->pcm_name, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0)
        goto fail;
    err = snd_pcm_open(&a->cap, a->pcm_name, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (err < 0)
        goto fail;
    if ((err = snd_pcm_nonblock(a->play, 0)) < 0 || (err = snd_pcm_nonblock(a->cap, 0)) < 0)
        goto fail;
    if ((buffer = set_params(a, a->play, 1)) < 0) {
        err = buffer;
        goto fail;
    }
    if ((err = set_params(a, a->cap, 0)) < 0)
        goto fail;
    a->bytes = (unsigned)snd_pcm_format_physical_width(a->format) / 8u;
    a->raw = calloc((size_t)CHANMAP_PERIOD * CH, a->bytes);
    if (!a->raw) {
        err = -ENOMEM;
        goto fail;
    }
    /* start both directions together (the null plugin of the tests cannot
     * link; then capture is started right after the playback prefill) */
    a->linked = snd_pcm_link(a->play, a->cap) == 0;
    /* prefill the playback buffer with silence; this starts the streams */
    memset(a->raw, 0, (size_t)CHANMAP_PERIOD * CH * a->bytes);
    for (int f = 0; f < buffer; f += (int)CHANMAP_PERIOD) {
        snd_pcm_sframes_t n = snd_pcm_writei(a->play, a->raw, CHANMAP_PERIOD);
        if (n < 0) {
            err = (int)n;
            goto fail;
        }
    }
    if (!a->linked && (err = snd_pcm_start(a->cap)) < 0)
        goto fail;
    return 0;

fail:
    alsa_close(a);
    return err;
}

static double full_scale(snd_pcm_format_t f)
{
    switch (f) {
    case SND_PCM_FORMAT_S32_LE: return 2147483647.0;
    case SND_PCM_FORMAT_S24_3LE:
    case SND_PCM_FORMAT_S24_LE: return 8388607.0;
    default: return 32767.0;
    }
}

int chanmap_alsa_encode(snd_pcm_format_t format, const float *in, size_t n, void *out)
{
    double fs = full_scale(format);
    /* second, independent guard on the integer samples */
    int64_t limit = (int64_t)floor(CHANMAP_MAX_PEAK * fs);
    uint8_t *p = out;

    for (size_t i = 0; i < n; i++) {
        double x = (double)in[i];
        int64_t v;
        if (!(fabs(x) <= 1.0)) /* also NaN */
            return -EDOM;
        v = (int64_t)lrint(x * fs);
        if (v > limit || v < -limit)
            return -EDOM;
        switch (format) {
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S24_LE: {
            int32_t s = (int32_t)v;
            memcpy(p + i * 4, &s, 4);
            break;
        }
        case SND_PCM_FORMAT_S24_3LE: {
            uint32_t s = (uint32_t)(int32_t)v;
            p[i * 3] = (uint8_t)(s & 0xff);
            p[i * 3 + 1] = (uint8_t)((s >> 8) & 0xff);
            p[i * 3 + 2] = (uint8_t)((s >> 16) & 0xff);
            break;
        }
        case SND_PCM_FORMAT_S16_LE: {
            int16_t s = (int16_t)v;
            memcpy(p + i * 2, &s, 2);
            break;
        }
        default:
            return -EINVAL;
        }
    }
    return 0;
}

int chanmap_alsa_decode(snd_pcm_format_t format, const void *in, size_t n, float *out)
{
    double fs = full_scale(format);
    const uint8_t *p = in;

    for (size_t i = 0; i < n; i++) {
        int32_t s;
        switch (format) {
        case SND_PCM_FORMAT_S32_LE:
            memcpy(&s, p + i * 4, 4);
            break;
        case SND_PCM_FORMAT_S24_LE:
            memcpy(&s, p + i * 4, 4);
            s = (int32_t)((uint32_t)s << 8) >> 8; /* sign extend 24 bit */
            break;
        case SND_PCM_FORMAT_S24_3LE: {
            uint32_t u = (uint32_t)p[i * 3] | ((uint32_t)p[i * 3 + 1] << 8) |
                         ((uint32_t)p[i * 3 + 2] << 16);
            s = (int32_t)(u << 8) >> 8;
            break;
        }
        case SND_PCM_FORMAT_S16_LE: {
            int16_t h;
            memcpy(&h, p + i * 2, 2);
            s = h;
            break;
        }
        default:
            return -EINVAL;
        }
        out[i] = (float)((double)s / fs);
    }
    return 0;
}

static int alsa_write(void *ctx, const float *buf, size_t frames)
{
    struct chanmap_alsa *a = ctx;
    uint8_t *p = a->raw;
    size_t done = 0;
    int err;

    if (frames > CHANMAP_PERIOD || !a->play)
        return -EINVAL;
    if ((err = chanmap_alsa_encode(a->format, buf, frames * CH, p)) < 0)
        return err;
    while (done < frames) {
        snd_pcm_sframes_t n = snd_pcm_writei(a->play, p + done * CH * a->bytes, frames - done);
        if (n == -EINTR || n == -EAGAIN)
            continue;
        if (n < 0)
            return (int)n; /* -EPIPE (underrun), -ENODEV (unplugged), ... */
        done += (size_t)n;
    }
    return 0;
}

static int alsa_read(void *ctx, float *buf, size_t frames)
{
    struct chanmap_alsa *a = ctx;
    uint8_t *p = a->raw;
    size_t done = 0;

    if (frames > CHANMAP_PERIOD || !a->cap)
        return -EINVAL;
    /* never interpret stale data (e.g. our own playback) as input */
    memset(p, 0, frames * CH * a->bytes);
    while (done < frames) {
        snd_pcm_sframes_t n = snd_pcm_readi(a->cap, p + done * CH * a->bytes, frames - done);
        if (n == -EINTR || n == -EAGAIN)
            continue;
        if (n < 0)
            return (int)n;
        done += (size_t)n;
    }
    return chanmap_alsa_decode(a->format, p, frames * CH, buf);
}

static int alsa_mixer_snapshot(void *ctx, char *out, size_t len)
{
    struct chanmap_alsa *a = ctx;
    snd_ctl_elem_list_t *list;
    snd_ctl_elem_info_t *info;
    snd_ctl_elem_value_t *val;
    size_t o = 0;
    unsigned n;
    int err;

    out[0] = '\0';
    if (!a->ctl)
        return 0;
    snd_ctl_elem_list_alloca(&list);
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_value_alloca(&val);
    if ((err = snd_ctl_elem_list(a->ctl, list)) < 0)
        return err;
    n = snd_ctl_elem_list_get_count(list);
    if ((err = snd_ctl_elem_list_alloc_space(list, n)) < 0)
        return err;
    if ((err = snd_ctl_elem_list(a->ctl, list)) < 0) {
        snd_ctl_elem_list_free_space(list);
        return err;
    }
    for (unsigned i = 0; i < n && o < len; i++) {
        snd_ctl_elem_id_t *id;
        snd_ctl_elem_type_t type;
        unsigned count;

        snd_ctl_elem_id_alloca(&id);
        snd_ctl_elem_list_get_id(list, i, id);
        snd_ctl_elem_info_set_id(info, id);
        if (snd_ctl_elem_info(a->ctl, info) < 0)
            continue;
        type = snd_ctl_elem_info_get_type(info);
        count = snd_ctl_elem_info_get_count(info);
        o += (size_t)snprintf(out + o, len - o, "%u '%s' %u:", snd_ctl_elem_id_get_numid(id),
                              snd_ctl_elem_id_get_name(id), snd_ctl_elem_id_get_index(id));
        if (o >= len)
            break;
        snd_ctl_elem_value_set_id(val, id);
        if (!snd_ctl_elem_info_is_readable(info) || snd_ctl_elem_read(a->ctl, val) < 0) {
            o += (size_t)snprintf(out + o, len - o, " (not readable)\n");
            continue;
        }
        for (unsigned k = 0; k < count && k < 32 && o < len; k++) {
            switch (type) {
            case SND_CTL_ELEM_TYPE_BOOLEAN:
                o += (size_t)snprintf(out + o, len - o, " %s",
                                      snd_ctl_elem_value_get_boolean(val, k) ? "on" : "off");
                break;
            case SND_CTL_ELEM_TYPE_INTEGER:
                o += (size_t)snprintf(out + o, len - o, " %ld",
                                      snd_ctl_elem_value_get_integer(val, k));
                break;
            case SND_CTL_ELEM_TYPE_INTEGER64:
                o += (size_t)snprintf(out + o, len - o, " %lld",
                                      snd_ctl_elem_value_get_integer64(val, k));
                break;
            case SND_CTL_ELEM_TYPE_ENUMERATED:
                o += (size_t)snprintf(out + o, len - o, " #%u",
                                      snd_ctl_elem_value_get_enumerated(val, k));
                break;
            case SND_CTL_ELEM_TYPE_BYTES:
                o += (size_t)snprintf(out + o, len - o, " %02x",
                                      snd_ctl_elem_value_get_byte(val, k));
                break;
            default:
                o += (size_t)snprintf(out + o, len - o, " ?");
                break;
            }
        }
        if (o < len)
            o += (size_t)snprintf(out + o, len - o, "\n");
    }
    snd_ctl_elem_list_free_space(list);
    if (o >= len)
        out[len - 1] = '\0';
    return 0;
}

void chanmap_alsa_backend(struct chanmap_alsa *a, struct chanmap_backend *be)
{
    be->ctx = a;
    be->open = alsa_open;
    be->write = alsa_write;
    be->read = alsa_read;
    be->close = alsa_close;
    be->mixer_snapshot = alsa_mixer_snapshot;
}
