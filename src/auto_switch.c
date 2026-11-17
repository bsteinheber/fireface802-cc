#define _POSIX_C_SOURCE 200809L

#include "ff802/auto_switch.h"

#include <stdarg.h>
#include <string.h>

#include "ff802/cc_mode.h"

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

static void set_detail(ff802_auto_result *res, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(res->detail, sizeof res->detail, fmt, ap);
    va_end(ap);
}

/* Decimal 1..max, leading zeros allowed (udev pads BUSNUM/DEVNUM). */
static int parse_padded(const char *s, size_t len, unsigned max, unsigned *out)
{
    unsigned v = 0;
    if (len == 0 || len > 3u)
        return -1;
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9')
            return -1;
        v = v * 10u + (unsigned)(s[i] - '0');
    }
    if (v < 1u || v > max)
        return -1;
    *out = v;
    return 0;
}

int ff802_auto_parse_instance(const char *s, ff802_auto_event *ev)
{
    unsigned bus, dev;

    if (s == NULL || ev == NULL)
        return -1;
    memset(ev, 0, sizeof *ev);
    const char *c1 = strchr(s, ':');
    if (c1 == NULL)
        return -1;
    const char *c2 = strchr(c1 + 1, ':');
    if (c2 == NULL || strchr(c2 + 1, ':') != NULL)
        return -1;
    size_t nlen = (size_t)(c1 - s);
    if (nlen == 0 || nlen >= sizeof ev->kernel_name)
        return -1;
    memcpy(ev->kernel_name, s, nlen);
    ev->kernel_name[nlen] = '\0';
    if (!ff802_sysfs_valid_name(ev->kernel_name))
        return -1;
    if (parse_padded(c1 + 1, (size_t)(c2 - c1 - 1), 255u, &bus) != 0 ||
        parse_padded(c2 + 1, strlen(c2 + 1), 127u, &dev) != 0)
        return -1;
    /* The bus number is also the first part of the kernel name. */
    char prefix[8];
    snprintf(prefix, sizeof prefix, "%u-", bus);
    if (strncmp(ev->kernel_name, prefix, strlen(prefix)) != 0)
        return -1;
    ev->bus = (uint8_t)bus;
    ev->devnum = (uint8_t)dev;
    return 0;
}

const char *ff802_auto_outcome_str(ff802_auto_outcome o)
{
    switch (o) {
    case FF802_AUTO_OFF:             return "off";
    case FF802_AUTO_ALREADY_HANDLED: return "already-handled";
    case FF802_AUTO_CC_ACTIVE:       return "cc-active";
    case FF802_AUTO_SENT:            return "sent";
    case FF802_AUTO_NOT_SENT:        return "not-sent";
    case FF802_AUTO_WRITE_FAILED:    return "write-failed";
    case FF802_AUTO_REFUSED:         return "refused";
    case FF802_AUTO_ERROR:           break;
    }
    return "error";
}

int ff802_auto_exit_code(ff802_auto_outcome o)
{
    switch (o) {
    case FF802_AUTO_OFF:
    case FF802_AUTO_ALREADY_HANDLED:
    case FF802_AUTO_CC_ACTIVE:
    case FF802_AUTO_SENT:
        return FF802_AUTO_EXIT_OK;
    case FF802_AUTO_WRITE_FAILED:
        return FF802_AUTO_EXIT_WRITE_FAILED;
    case FF802_AUTO_NOT_SENT:
    case FF802_AUTO_REFUSED:
    case FF802_AUTO_ERROR:
        break;
    }
    return FF802_AUTO_EXIT_NOT_SENT;
}

/* Context of the last check right before the claim. */
typedef struct revalidate_ctx {
    const ff802_auto_paths *paths;
    const ff802_sysfs_dev  *snapshot;
    FILE                   *log;
    char                    why[128];
} revalidate_ctx;

static const uint8_t ENABLE_CMD[FF802_REG_WORD_SIZE] = { 0x01, 0x00, 0x44, 0xBD };

/*
 * Called by cc_switch once, after open and kernel driver check, right
 * before claim and write. Non-interactive replacement of the terminal
 * confirmation of ff802-cc enable: every condition is checked again.
 */
static int revalidate(void *user, const ff802_usb_dev *target,
                      const uint8_t cmd[FF802_REG_WORD_SIZE])
{
    revalidate_ctx *c = user;
    ff802_sysfs_dev now;

    if (memcmp(cmd, ENABLE_CMD, sizeof ENABLE_CMD) != 0) {
        snprintf(c->why, sizeof c->why, "unexpected command bytes");
        return 0;
    }
    if (ff802_auto_read_mode(c->paths) != FF802_AUTO_MODE_ON) {
        snprintf(c->why, sizeof c->why, "automation was switched off");
        return 0;
    }
    if (ff802_sysfs_read(c->paths->sysfs_root, c->snapshot->kernel_name, &now)
            != FF802_SYSFS_OK) {
        snprintf(c->why, sizeof c->why, "device disappeared before the write");
        return 0;
    }
    if (!ff802_sysfs_same_connection(c->snapshot, &now)) {
        snprintf(c->why, sizeof c->why, "device changed before the write "
                 "(devnum %u -> %u)", (unsigned)c->snapshot->devnum,
                 (unsigned)now.devnum);
        return 0;
    }
    if (ff802_sysfs_identify(&now) != FF802_ID_PROPRIETARY) {
        snprintf(c->why, sizeof c->why, "device no longer identified as "
                 "Fireface 802 in proprietary mode");
        return 0;
    }
    if (!ff802_sysfs_matches_usb_dev(&now, target)) {
        snprintf(c->why, sizeof c->why, "opened USB device is not the "
                 "connection of the event");
        return 0;
    }
    logf_(c->log, "Re-validated %s (bus %u, devnum %u) right before the write.",
          now.kernel_name, (unsigned)now.bus, (unsigned)now.devnum);
    return 1;
}

static void write_event(const ff802_auto_paths *p, ff802_auto_result *res,
                        ff802_auto_event_kind kind, const ff802_sysfs_dev *d,
                        FILE *log)
{
    long seq = ff802_auto_write_event(p, kind, d, res->detail);
    if (seq < 0)
        logf_(log, "Warning: could not write the notification event.");
    else
        res->event_seq = seq;
}

/* Everything below the lock. */
static ff802_auto_outcome handle_locked(const ff802_auto_paths *p,
                                        const ff802_auto_event *ev,
                                        const ff802_auto_backend_factory *f,
                                        FILE *log, ff802_auto_result *res)
{
    ff802_sysfs_dev d;

    ff802_sysfs_status ss = ff802_sysfs_read(p->sysfs_root, ev->kernel_name, &d);
    if (ss == FF802_SYSFS_ABSENT) {
        set_detail(res, "%s is no longer connected; nothing sent", ev->kernel_name);
        return FF802_AUTO_REFUSED;
    }
    if (ss != FF802_SYSFS_OK) {
        set_detail(res, "cannot read the sysfs attributes of %s; nothing sent",
                   ev->kernel_name);
        return FF802_AUTO_REFUSED;
    }
    if (d.bus != ev->bus || d.devnum != ev->devnum) {
        set_detail(res, "%s was reconnected since the event (devnum %u, now %u); "
                   "nothing sent", ev->kernel_name, (unsigned)ev->devnum,
                   (unsigned)d.devnum);
        return FF802_AUTO_REFUSED;
    }

    res->identity = ff802_sysfs_identify(&d);
    res->have_identity = 1;
    logf_(log, "Connection %s bus %u devnum %u: ID %04x:%04x, '%s' '%s', "
               "speed %s: %s.", d.kernel_name, (unsigned)d.bus,
          (unsigned)d.devnum, (unsigned)d.vid, (unsigned)d.pid,
          d.manufacturer, d.product, d.speed, ff802_identity_str(res->identity));

    if (res->identity == FF802_ID_CLASS_COMPLIANT) {
        set_detail(res, "Fireface 802 in class-compliant mode at %s; nothing sent",
                   d.kernel_name);
        if (ff802_auto_pending_for(p, d.kernel_name)) {
            (void)ff802_auto_clear_pending(p);
            write_event(p, res, FF802_EVENT_CC_ACTIVE, &d, log);
        }
        return FF802_AUTO_CC_ACTIVE;
    }
    if (d.vid != 0x2A39u || d.pid != 0x3FCDu) {
        set_detail(res, "%s is not a Fireface 802 (%s); nothing sent",
                   d.kernel_name, ff802_identity_str(res->identity));
        return FF802_AUTO_REFUSED;
    }

    ff802_auto_mode mode = ff802_auto_read_mode(p);
    if (mode != FF802_AUTO_MODE_ON) {
        set_detail(res, "automation is %s (ff802-cc-auto status); nothing sent",
                   ff802_auto_mode_str(mode));
        return FF802_AUTO_OFF;
    }

    int claimed = ff802_auto_claim_connection(p, &d);
    if (claimed < 0) {
        set_detail(res, "cannot record the connection in %s; nothing sent",
                   p->run_dir);
        return FF802_AUTO_ERROR;
    }
    if (claimed == 0) {
        set_detail(res, "connection %s devnum %u was handled before; nothing sent",
                   d.kernel_name, (unsigned)d.devnum);
        return FF802_AUTO_ALREADY_HANDLED;
    }

    if (res->identity != FF802_ID_PROPRIETARY) {
        set_detail(res, "USB ID 2a39:3fcd, but %s; nothing sent",
                   ff802_identity_str(res->identity));
        write_event(p, res, FF802_EVENT_FAILED, &d, log);
        return FF802_AUTO_REFUSED;
    }

    ff802_usb_backend be;
    memset(&be, 0, sizeof be);
    ff802_usb_err e = f->open(f->ctx, &be);
    if (e != FF802_USB_OK) {
        set_detail(res, "USB initialisation failed (%s); nothing sent",
                   ff802_usb_err_str(e));
        write_event(p, res, FF802_EVENT_FAILED, &d, log);
        return FF802_AUTO_NOT_SENT;
    }
    res->backend_created = 1;

    revalidate_ctx rc = { .paths = p, .snapshot = &d, .log = log, .why = "" };
    ff802_sw_options opt;
    memset(&opt, 0, sizeof opt);
    opt.action = FF802_SW_ENABLE_CC;
    opt.select_location = 1;
    opt.bus = d.bus;
    opt.address = d.devnum;
    opt.confirm = revalidate;
    opt.confirm_user = &rc;

    ff802_switch_run(&be, &opt, log, &res->sw);
    res->switch_ran = 1;
    f->close(f->ctx, &be);

    const char *st = ff802_sw_status_str(res->sw.status);
    if (res->sw.status == FF802_SW_OK) {
        set_detail(res, "enable command transferred to %s (%zu bytes); "
                   "disconnect and reconnect the USB cable", d.kernel_name,
                   res->sw.transferred);
        if (ff802_auto_set_pending(p, &d) != 0)
            logf_(log, "Warning: could not write the pending marker.");
        write_event(p, res, FF802_EVENT_PREPARED, &d, log);
        return FF802_AUTO_SENT;
    }
    if (res->sw.write_attempted) {
        set_detail(res, "transfer failed (%s, %zu of 4 bytes); device state "
                   "unknown, nothing retried; power-cycle the device", st,
                   res->sw.transferred);
        write_event(p, res, FF802_EVENT_FAILED, &d, log);
        return FF802_AUTO_WRITE_FAILED;
    }
    if (res->sw.status == FF802_SW_ABORTED)
        set_detail(res, "%s; nothing sent", rc.why[0] ? rc.why : st);
    else if (res->sw.status == FF802_SW_UNEXPECTED_LAYOUT)
        set_detail(res, "%s: %s; nothing sent", st,
                   ff802_layout_status_str(res->sw.layout));
    else
        set_detail(res, "%s; nothing sent", st);
    write_event(p, res, FF802_EVENT_FAILED, &d, log);
    return res->sw.status == FF802_SW_ABORTED ? FF802_AUTO_REFUSED
                                              : FF802_AUTO_NOT_SENT;
}

ff802_auto_outcome ff802_auto_handle(const ff802_auto_paths *p,
                                     const ff802_auto_event *ev,
                                     const ff802_auto_backend_factory *f,
                                     FILE *log, ff802_auto_result *res)
{
    if (res == NULL)
        return FF802_AUTO_ERROR;
    memset(res, 0, sizeof *res);
    if (p == NULL || ev == NULL || f == NULL || f->open == NULL || f->close == NULL) {
        set_detail(res, "internal error");
        return res->outcome = FF802_AUTO_ERROR;
    }

    int lock = ff802_auto_lock(p);
    if (lock < 0) {
        set_detail(res, "cannot lock %s/lock; nothing sent", p->run_dir);
        logf_(log, "%s", res->detail);
        return res->outcome = FF802_AUTO_ERROR;
    }
    res->outcome = handle_locked(p, ev, f, log, res);
    logf_(log, "Result: %s: %s", ff802_auto_outcome_str(res->outcome), res->detail);
    if (ff802_auto_write_result(p, ff802_auto_outcome_str(res->outcome),
                                ev->kernel_name, res->detail) != 0)
        logf_(log, "Warning: could not write %s/last-result.", p->run_dir);
    ff802_auto_unlock(lock);
    return res->outcome;
}
