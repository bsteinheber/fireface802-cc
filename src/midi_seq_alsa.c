#define _POSIX_C_SOURCE 200809L

#include "ff802/midi_seq_alsa.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <alsa/asoundlib.h>

#ifndef FF802_ENABLE_MIXER_WRITE
#define FF802_ENABLE_MIXER_WRITE 0
#endif

#define CLIENT_NAME "ff802-cc-auto"

typedef struct seq_ctx {
    snd_seq_t *seq;
    int myport;          /* -1 until connect */
    int dest_client;
    int dest_port;
    int connected;
} seq_ctx;

int ff802_midi_seq_write_enabled(void)
{
    return FF802_ENABLE_MIXER_WRITE;
}

static int s_list(void *self, ff802_seq_port *out, size_t max, size_t *n)
{
    seq_ctx *c = self;
    snd_seq_client_info_t *ci = NULL;
    snd_seq_port_info_t *pi = NULL;

    *n = 0;
    if (snd_seq_client_info_malloc(&ci) < 0 || snd_seq_port_info_malloc(&pi) < 0) {
        snd_seq_client_info_free(ci);
        return -ENOMEM;
    }
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(c->seq, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        int kernel = snd_seq_client_info_get_type(ci) == SND_SEQ_KERNEL_CLIENT;
        int card = snd_seq_client_info_get_card(ci);
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(c->seq, pi) >= 0) {
            if (*n >= max)
                goto out;
            unsigned caps = snd_seq_port_info_get_capability(pi);
            const char *name = snd_seq_port_info_get_name(pi);
            ff802_seq_port *p = &out[(*n)++];
            p->client = client;
            p->port = snd_seq_port_info_get_port(pi);
            p->kernel_client = kernel;
            p->card = card < 0 ? -1 : card;
            p->writable = (caps & SND_SEQ_PORT_CAP_WRITE) != 0 &&
                          (caps & SND_SEQ_PORT_CAP_SUBS_WRITE) != 0;
            snprintf(p->name, sizeof p->name, "%s", name != NULL ? name : "");
        }
    }
out:
    snd_seq_port_info_free(pi);
    snd_seq_client_info_free(ci);
    return 0;
}

#if FF802_ENABLE_MIXER_WRITE
static int s_connect(void *self, int client, int port)
{
    seq_ctx *c = self;
    int mine = snd_seq_create_simple_port(c->seq, CLIENT_NAME " mixer init",
                                          SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC |
                                          SND_SEQ_PORT_TYPE_APPLICATION);
    if (mine < 0)
        return mine;
    c->myport = mine;
    int rc = snd_seq_connect_to(c->seq, mine, client, port);
    if (rc < 0)
        return rc;
    c->connected = 1;
    c->dest_client = client;
    c->dest_port = port;
    return 0;
}

static int s_send(void *self, const uint8_t *msg, size_t len)
{
    seq_ctx *c = self;
    snd_seq_event_t ev;

    if (!c->connected || msg == NULL || len == 0 || len > 64u)
        return -EINVAL;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, (unsigned char)c->myport);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    ev.type = SND_SEQ_EVENT_SYSEX;
    ev.flags &= (unsigned char)~SND_SEQ_EVENT_LENGTH_MASK;
    ev.flags |= SND_SEQ_EVENT_LENGTH_VARIABLE;
    ev.data.ext.len = (unsigned int)len;
    ev.data.ext.ptr = (void *)(uintptr_t)msg;
    int rc = snd_seq_event_output_direct(c->seq, &ev);
    return rc < 0 ? rc : 0;
}

static void s_disconnect(seq_ctx *c)
{
    if (c->connected)
        (void)snd_seq_disconnect_to(c->seq, c->myport, c->dest_client, c->dest_port);
    c->connected = 0;
}
#else
static int s_connect(void *self, int client, int port)
{
    (void)self; (void)client; (void)port;
    return -EPERM;
}

static int s_send(void *self, const uint8_t *msg, size_t len)
{
    (void)self; (void)msg; (void)len;
    return -EPERM;
}

static void s_disconnect(seq_ctx *c)
{
    (void)c;
}
#endif

static int f_open(void *ctx, ff802_midi_backend *be)
{
    (void)ctx;
    seq_ctx *c = calloc(1, sizeof *c);
    if (c == NULL)
        return -ENOMEM;
    c->myport = -1;
    int rc = snd_seq_open(&c->seq, "hw", SND_SEQ_OPEN_OUTPUT, 0);
    if (rc < 0) {
        free(c);
        return rc;
    }
    (void)snd_seq_set_client_name(c->seq, CLIENT_NAME);
    be->self = c;
    be->list_ports = s_list;
    be->connect = s_connect;
    be->send_sysex = s_send;
    return 0;
}

static void f_close(void *ctx, ff802_midi_backend *be)
{
    (void)ctx;
    seq_ctx *c = be->self;
    if (c == NULL)
        return;
    s_disconnect(c);
    snd_seq_close(c->seq);
    free(c);
    be->self = NULL;
}

static void f_sleep(void *ctx, unsigned ms)
{
    (void)ctx;
    struct timespec ts = { .tv_sec = (time_t)(ms / 1000u),
                           .tv_nsec = (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

ff802_midi_factory ff802_midi_seq_factory(void)
{
    ff802_midi_factory f = { .ctx = NULL, .open = f_open, .close = f_close,
                             .sleep_ms = f_sleep,
                             .write_enabled = FF802_ENABLE_MIXER_WRITE };
    return f;
}
