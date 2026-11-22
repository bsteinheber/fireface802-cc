#define _POSIX_C_SOURCE 200809L

#include "sim_midi.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct simm {
    char root[FF802_AUTO_PATH_MAX];
    char log_path[FF802_AUTO_PATH_MAX + 16u];
    char ports_path[FF802_AUTO_PATH_MAX + 16u];
    unsigned sends;
    unsigned lists;
} simm;

static simm g_sim;

__attribute__((format(printf, 2, 3)))
static void mlog(simm *s, const char *fmt, ...)
{
    FILE *f = fopen(s->log_path, "a");
    if (f == NULL)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int env_is(const char *name, const char *value)
{
    const char *v = getenv(name);
    return v != NULL && strcmp(v, value) == 0;
}

static int s_list(void *self, ff802_seq_port *out, size_t max, size_t *n)
{
    simm *s = self;
    char line[256];

    *n = 0;
    mlog(s, "seq list");
    if (env_is("FF802_SIM_SEQ_LIST_ERR", "1"))
        return -EIO;
    s->lists++;
    const char *after = getenv("FF802_SIM_SEQ_PORTS_AFTER");
    if (after != NULL && s->lists <= (unsigned)atoi(after))
        return 0;                       /* the port appears later */
    FILE *f = fopen(s->ports_path, "r");
    if (f == NULL)
        return 0;
    while (fgets(line, sizeof line, f) != NULL && *n < max) {
        ff802_seq_port p;
        char type[16], w[4];
        int off = 0;
        memset(&p, 0, sizeof p);
        if (sscanf(line, "%d %d %15s %d %3s %n", &p.client, &p.port, type, &p.card,
                   w, &off) < 5 || off == 0)
            continue;
        line[strcspn(line, "\n")] = '\0';
        p.kernel_client = strcmp(type, "kernel") == 0;
        p.writable = strcmp(w, "w") == 0;
        snprintf(p.name, sizeof p.name, "%s", line + off);
        out[(*n)++] = p;
    }
    fclose(f);
    return 0;
}

static int s_connect(void *self, int client, int port)
{
    simm *s = self;
    mlog(s, "seq connect %d:%d", client, port);
    if (env_is("FF802_SIM_SEQ_CONNECT_ERR", "busy"))
        return -EBUSY;
    if (env_is("FF802_SIM_SEQ_CONNECT_ERR", "perm"))
        return -EPERM;
    return 0;
}

static int s_send(void *self, const uint8_t *msg, size_t len)
{
    simm *s = self;
    char hex[3u * 64u + 1u];
    size_t o = 0;

    s->sends++;
    const char *at = getenv("FF802_SIM_SEQ_SEND_ERR_AT");
    if (at != NULL && (unsigned)atoi(at) == s->sends) {
        mlog(s, "sysex FAILED");
        return -EIO;
    }
    for (size_t i = 0; i < len && i < 64u; i++)
        o += (size_t)snprintf(hex + o, sizeof hex - o, i ? " %02X" : "%02X", msg[i]);
    hex[o] = '\0';
    mlog(s, "sysex %s", hex);
    return 0;
}

static int f_open(void *ctx, ff802_midi_backend *be)
{
    simm *s = ctx;
    mlog(s, "seq open");
    if (env_is("FF802_SIM_SEQ_OPEN_ERR", "1"))
        return -ENOENT;
    s->sends = 0;
    s->lists = 0;
    be->self = s;
    be->list_ports = s_list;
    be->connect = s_connect;
    be->send_sysex = s_send;
    return 0;
}

static void f_close(void *ctx, ff802_midi_backend *be)
{
    (void)be;
    mlog((simm *)ctx, "seq close");
}

static void f_sleep(void *ctx, unsigned ms)
{
    (void)ctx;
    struct timespec ts = { .tv_sec = (time_t)(ms / 1000u),
                           .tv_nsec = (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

void sim_midi_setup(const ff802_auto_paths *p, ff802_midi_factory *f,
                    ff802_mixer_options *o)
{
    simm *s = &g_sim;
    memset(s, 0, sizeof *s);
    /* the root is the parent of <root>/sys */
    snprintf(s->root, sizeof s->root, "%s", p->sysfs_root);
    char *slash = strrchr(s->root, '/');
    if (slash != NULL)
        *slash = '\0';
    snprintf(s->log_path, sizeof s->log_path, "%s/midi.log", s->root);
    snprintf(s->ports_path, sizeof s->ports_path, "%s/seq-ports", s->root);

    *f = (ff802_midi_factory){ .ctx = s, .open = f_open, .close = f_close,
                               .sleep_ms = f_sleep,
                               .write_enabled = !env_is("FF802_SIM_MIXER_WRITE", "0") };
    ff802_mixer_default_options(o);
    o->timeout_ms = 300;
    const char *t = getenv("FF802_SIM_MIXER_TIMEOUT_MS");
    if (t != NULL && t[0] != '\0')
        o->timeout_ms = (unsigned)atoi(t);
}
