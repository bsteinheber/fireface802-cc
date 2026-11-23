#define _POSIX_C_SOURCE 200809L

#include "ff802/mixer_run.h"

#include <stdarg.h>
#include <string.h>

#include "ff802/mixer_init.h"

static void logf_(FILE *log, const char *fmt, ...)
{
    if (log == NULL)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log, fmt, ap);
    va_end(ap);
    fputc('\n', log);
    fflush(log);
}

__attribute__((format(printf, 3, 4)))
static ff802_mixer_outcome finish(ff802_mixer_result *res, ff802_mixer_outcome o,
                                  const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->detail, sizeof res->detail, fmt, ap);
    va_end(ap);
    return res->outcome = o;
}

static const char *err_str(int rc)
{
    return rc < 0 ? strerror(-rc) : "unknown error";
}

void ff802_mixer_default_options(ff802_mixer_options *o)
{
    o->timeout_ms = FF802_MIXER_TIMEOUT_MS;
    o->poll_ms = FF802_MIXER_POLL_MS;
    o->gap_ms = FF802_MIXER_GAP_MS;
    o->settle_ms = FF802_MIXER_SETTLE_MS;
    o->dry_run = 0;
    o->confirm = NULL;
    o->confirm_user = NULL;
}

const char *ff802_mixer_outcome_str(ff802_mixer_outcome o)
{
    switch (o) {
    case FF802_MIXER_OFF:            return "off";
    case FF802_MIXER_DONE:           return "done";
    case FF802_MIXER_DISABLED_BUILD: return "disabled-build";
    case FF802_MIXER_UNAVAILABLE:    return "unavailable";
    case FF802_MIXER_NO_PORT:        return "no-port";
    case FF802_MIXER_REFUSED:        return "refused";
    case FF802_MIXER_NOT_SENT:       return "not-sent";
    case FF802_MIXER_INCOMPLETE:     return "incomplete";
    case FF802_MIXER_DRY_RUN:        return "dry-run";
    case FF802_MIXER_ABORTED:        break;
    }
    return "aborted";
}

int ff802_mixer_outcome_failed(ff802_mixer_outcome o)
{
    return o != FF802_MIXER_OFF && o != FF802_MIXER_DONE && o != FF802_MIXER_DRY_RUN &&
           o != FF802_MIXER_ABORTED;
}

static void log_ports(FILE *log, const ff802_seq_port *ports, size_t n, int card)
{
    for (size_t i = 0; i < n; i++)
        if (ports[i].card == card || ports[i].kernel_client)
            logf_(log, "mixer: sequencer port %d:%d '%s' (%s client, card %d%s)",
                  ports[i].client, ports[i].port, ports[i].name,
                  ports[i].kernel_client ? "kernel" : "user", ports[i].card,
                  ports[i].writable ? ", writable" : "");
}

/* The connection is still the one of the event, in CC mode, same card. */
static int still_same(const char *root, const ff802_sysfs_dev *d, int card,
                      char *why, size_t size)
{
    ff802_sysfs_dev now;
    int c = -1;
    if (ff802_sysfs_read(root, d->kernel_name, &now) != FF802_SYSFS_OK) {
        snprintf(why, size, "%s is no longer connected", d->kernel_name);
        return 0;
    }
    if (now.bus != d->bus || now.devnum != d->devnum) {
        snprintf(why, size, "%s was reconnected (devnum %u, now %u)",
                 d->kernel_name, (unsigned)d->devnum, (unsigned)now.devnum);
        return 0;
    }
    if (ff802_sysfs_identify(&now) != FF802_ID_CLASS_COMPLIANT) {
        snprintf(why, size, "%s is no longer a Fireface 802 in class-compliant mode",
                 d->kernel_name);
        return 0;
    }
    if (ff802_mixer_find_card(root, d->kernel_name, &c) != FF802_MPORT_FOUND || c != card) {
        snprintf(why, size, "sound card of %s changed", d->kernel_name);
        return 0;
    }
    return 1;
}

static ff802_mixer_outcome run_open(const char *root, const ff802_sysfs_dev *d,
                                    const ff802_midi_factory *f,
                                    const ff802_mixer_options *o, unsigned waited,
                                    ff802_midi_backend *be, FILE *log,
                                    ff802_mixer_result *res)
{
    ff802_seq_port ports[FF802_SEQ_MAX_PORTS];
    size_t n = 0, idx = 0;
    int rc;

    for (;;) {
        rc = be->list_ports(be->self, ports, FF802_SEQ_MAX_PORTS, &n);
        if (rc < 0)
            return finish(res, FF802_MIXER_NOT_SENT,
                          "cannot list the ALSA sequencer ports (%s); nothing sent",
                          err_str(rc));
        ff802_seqsel sel = ff802_mixer_select_port(ports, n, res->card, &idx);
        if (sel == FF802_SEQSEL_FOUND)
            break;
        if (sel == FF802_SEQSEL_AMBIGUOUS || sel == FF802_SEQSEL_MISMATCH) {
            log_ports(log, ports, n, res->card);
            return finish(res, FF802_MIXER_REFUSED, "MIDI port of card %d: %s; nothing sent",
                          res->card, ff802_seqsel_str(sel));
        }
        if (waited >= o->timeout_ms) {
            log_ports(log, ports, n, res->card);
            return finish(res, FF802_MIXER_NO_PORT,
                          "MIDI port of card %d not available after %u ms (%s); nothing sent",
                          res->card, waited, ff802_seqsel_str(sel));
        }
        f->sleep_ms(f->ctx, o->poll_ms);
        waited += o->poll_ms;
    }
    res->client = ports[idx].client;
    res->port = ports[idx].port;
    snprintf(res->port_name, sizeof res->port_name, "%s", ports[idx].name);
    logf_(log, "mixer: MIDI port %d:%d '%s' of card %d for %s.", res->client,
          res->port, res->port_name, res->card, d->kernel_name);

    if (o->dry_run)
        return finish(res, FF802_MIXER_DRY_RUN, "dry run: port %d:%d found; nothing sent",
                      res->client, res->port);
    if (!f->write_enabled)
        return finish(res, FF802_MIXER_DISABLED_BUILD,
                      "port %d:%d found, but mixer writes are not compiled into "
                      "this build; nothing sent", res->client, res->port);
    if (o->confirm != NULL && !o->confirm(o->confirm_user, res))
        return finish(res, FF802_MIXER_ABORTED, "not confirmed; nothing sent");

    char why[128];
    if (!still_same(root, d, res->card, why, sizeof why))
        return finish(res, FF802_MIXER_REFUSED, "%s; nothing sent", why);

    rc = be->connect(be->self, res->client, res->port);
    if (rc < 0)
        return finish(res, FF802_MIXER_NOT_SENT, "cannot connect to MIDI port %d:%d (%s); "
                      "nothing sent", res->client, res->port, err_str(rc));

    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        uint8_t msg[FF802_MIXER_SYSEX_SIZE];
        char label[64];
        ff802_mixer_entry_label(&t[i], label, sizeof label);
        if (ff802_rme_sysex_reg(t[i].addr, t[i].value, msg) != FF802_OK)
            return finish(res, FF802_MIXER_INCOMPLETE, "cannot encode register 0x%04X; "
                          "%u of %u written, nothing retried", (unsigned)t[i].addr,
                          res->written, FF802_MIXER_INIT_COUNT);
        rc = be->send_sysex(be->self, msg, sizeof msg);
        if (rc < 0) {
            logf_(log, "mixer: FAILED 0x%04X = 0x%04X (%s) [%u/%u]: %s",
                  (unsigned)t[i].addr, (unsigned)t[i].value, label, i + 1u,
                  FF802_MIXER_INIT_COUNT, err_str(rc));
            return finish(res, FF802_MIXER_INCOMPLETE,
                          "sending register %u of %u (0x%04X) failed (%s); %u written, "
                          "nothing retried", i + 1u, FF802_MIXER_INIT_COUNT,
                          (unsigned)t[i].addr, err_str(rc), res->written);
        }
        res->written++;
        logf_(log, "mixer: set 0x%04X = 0x%04X (%s) [%u/%u]", (unsigned)t[i].addr,
              (unsigned)t[i].value, label, i + 1u, FF802_MIXER_INIT_COUNT);
        if (o->gap_ms > 0 && i + 1u < FF802_MIXER_INIT_COUNT)
            f->sleep_ms(f->ctx, o->gap_ms);
    }
    if (o->settle_ms > 0)
        f->sleep_ms(f->ctx, o->settle_ms);
    return finish(res, FF802_MIXER_DONE, "%u registers sent to MIDI port %d:%d '%s' (card %d)",
                  FF802_MIXER_INIT_COUNT, res->client, res->port, res->port_name, res->card);
}

ff802_mixer_outcome ff802_mixer_run(const char *sysfs_root,
                                    const ff802_sysfs_dev *d,
                                    const ff802_midi_factory *f,
                                    const ff802_mixer_options *opt,
                                    FILE *log, ff802_mixer_result *res)
{
    ff802_mixer_options o;
    unsigned waited = 0;

    if (res == NULL)
        return FF802_MIXER_REFUSED;
    memset(res, 0, sizeof *res);
    res->card = res->client = res->port = -1;
    if (sysfs_root == NULL || d == NULL)
        return finish(res, FF802_MIXER_REFUSED, "internal error; nothing sent");
    if (opt != NULL)
        o = *opt;
    else
        ff802_mixer_default_options(&o);
    if (o.poll_ms == 0)
        o.poll_ms = FF802_MIXER_POLL_MS;

    if (ff802_mixer_init_verify() != 0)
        return finish(res, FF802_MIXER_REFUSED, "register table check failed; nothing sent");
    logf_(log, "mixer: configuration %s, %u entries, checksum 0x%08X (verified).",
          FF802_MIXER_INIT_ID, FF802_MIXER_INIT_COUNT, (unsigned)ff802_mixer_init_checksum());
    if (f == NULL || f->open == NULL || f->close == NULL || f->sleep_ms == NULL)
        return finish(res, FF802_MIXER_UNAVAILABLE,
                      "no MIDI backend in this program; nothing sent");

    for (;;) {
        ff802_mport_status st = ff802_mixer_find_card(sysfs_root, d->kernel_name, &res->card);
        if (st == FF802_MPORT_FOUND)
            break;
        if (st != FF802_MPORT_NOT_YET)
            return finish(res, FF802_MIXER_REFUSED, "%s: %s; nothing sent",
                          d->kernel_name, ff802_mport_status_str(st));
        if (waited >= o.timeout_ms)
            return finish(res, FF802_MIXER_NO_PORT, "%s: %s after %u ms; nothing sent",
                          d->kernel_name, ff802_mport_status_str(st), waited);
        f->sleep_ms(f->ctx, o.poll_ms);
        waited += o.poll_ms;
    }
    logf_(log, "mixer: sound card %d for %s (after %u ms).", res->card, d->kernel_name, waited);

    ff802_midi_backend be;
    memset(&be, 0, sizeof be);
    int rc = f->open(f->ctx, &be);
    if (rc < 0)
        return finish(res, FF802_MIXER_NOT_SENT,
                      "cannot open the ALSA sequencer (%s); nothing sent", err_str(rc));
    if (be.list_ports == NULL || be.connect == NULL || be.send_sysex == NULL) {
        f->close(f->ctx, &be);
        return finish(res, FF802_MIXER_UNAVAILABLE, "incomplete MIDI backend; nothing sent");
    }
    ff802_mixer_outcome out = run_open(sysfs_root, d, f, &o, waited, &be, log, res);
    f->close(f->ctx, &be);
    return out;
}
