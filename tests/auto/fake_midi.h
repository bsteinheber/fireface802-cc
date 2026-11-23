/*
 * In-memory MIDI backend for the C tests of the mixer initialisation.
 * Records every call; the port list, errors and sleep hooks are set by
 * the test. Nothing is opened.
 */
#ifndef FF802_FAKE_MIDI_H
#define FF802_FAKE_MIDI_H

#include <errno.h>
#include <string.h>

#include "ff802/midi_backend.h"
#include "ff802/mixer_init.h"

typedef struct fake_midi {
    ff802_seq_port ports[16];
    size_t nports;
    unsigned ports_after_lists;     /* port list empty for the first n lists */
    int open_err, list_err, connect_err;
    unsigned send_err_at;           /* 1-based message number that fails, 0: none */
    int write_enabled;
    /* records */
    unsigned n_open, n_close, n_list, n_connect, n_sleep, slept_ms;
    int conn_client, conn_port;
    uint8_t sent[2u * FF802_MIXER_INIT_COUNT + 4u][FF802_MIXER_SYSEX_SIZE];   /* two passes */
    unsigned n_sent;                /* messages accepted */
    unsigned n_send_calls;
    void (*on_sleep)(struct fake_midi *m);
    void *user;
} fake_midi;

static int fm_list(void *self, ff802_seq_port *out, size_t max, size_t *n)
{
    fake_midi *m = self;
    m->n_list++;
    if (m->list_err)
        return m->list_err;
    *n = 0;
    if (m->n_list <= m->ports_after_lists)
        return 0;
    for (size_t i = 0; i < m->nports && i < max; i++)
        out[(*n)++] = m->ports[i];
    return 0;
}

static int fm_connect(void *self, int client, int port)
{
    fake_midi *m = self;
    m->n_connect++;
    m->conn_client = client;
    m->conn_port = port;
    return m->connect_err;
}

static int fm_send(void *self, const uint8_t *msg, size_t len)
{
    fake_midi *m = self;
    m->n_send_calls++;
    if (m->send_err_at != 0 && m->n_send_calls == m->send_err_at)
        return -EIO;
    if (len != FF802_MIXER_SYSEX_SIZE || m->n_sent >= 2u * FF802_MIXER_INIT_COUNT + 4u)
        return -EINVAL;
    memcpy(m->sent[m->n_sent++], msg, len);
    return 0;
}

static int fm_open(void *ctx, ff802_midi_backend *be)
{
    fake_midi *m = ctx;
    m->n_open++;
    if (m->open_err)
        return m->open_err;
    be->self = m;
    be->list_ports = fm_list;
    be->connect = fm_connect;
    be->send_sysex = fm_send;
    return 0;
}

static void fm_close(void *ctx, ff802_midi_backend *be)
{
    fake_midi *m = ctx;
    (void)be;
    m->n_close++;
}

static void fm_sleep(void *ctx, unsigned ms)
{
    fake_midi *m = ctx;
    m->n_sleep++;
    m->slept_ms += ms;
    if (m->on_sleep != NULL)
        m->on_sleep(m);
}

/* The listing of the unit in phase 2a, card <card>, client <client>. */
static inline void fake_midi_init(fake_midi *m, int card, int client)
{
    memset(m, 0, sizeof *m);
    m->write_enabled = 1;
    m->conn_client = m->conn_port = -1;
    m->ports[0] = (ff802_seq_port){ 0, 0, 1, -1, 0, "Timer" };
    m->ports[1] = (ff802_seq_port){ 14, 0, 1, -1, 1, "Midi Through Port-0" };
    m->ports[2] = (ff802_seq_port){ client, 0, 1, card, 1, "Fireface 802 (24047128) Port 1" };
    m->ports[3] = (ff802_seq_port){ client, 1, 1, card, 1, "Fireface 802 (24047128) Port 2" };
    m->ports[4] = (ff802_seq_port){ 144, 0, 0, -1, 1, "input" };
    m->nports = 5;
}

static inline ff802_midi_factory fake_midi_factory(fake_midi *m)
{
    ff802_midi_factory f = { .ctx = m, .open = fm_open, .close = fm_close,
                             .sleep_ms = fm_sleep, .write_enabled = m->write_enabled };
    return f;
}

#endif /* FF802_FAKE_MIDI_H */
