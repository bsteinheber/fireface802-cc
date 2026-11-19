#define _XOPEN_SOURCE 700

#include "ff802/disable_check.h"

#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ff802/device_id.h"

void ff802_dchk_default_roots(ff802_dchk_roots *r)
{
    memset(r, 0, sizeof *r);
    ff802_auto_default_paths(&r->paths);
    snprintf(r->proc_asound, sizeof r->proc_asound, "/proc/asound");
}

const char *ff802_dchk_status_str(ff802_dchk_status st)
{
    switch (st) {
    case FF802_DCHK_OK:            return "preconditions met";
    case FF802_DCHK_NOT_FOUND:     return "no Fireface 802 in class-compliant mode found";
    case FF802_DCHK_AMBIGUOUS:     return "more than one Fireface 802 in class-compliant mode found";
    case FF802_DCHK_IDENTITY:      return "device is not identified as an original Fireface 802";
    case FF802_DCHK_INTERFACE:     return "USB interface 3 is not as expected";
    case FF802_DCHK_DRIVER:        return "interface 3 is not bound to snd-usb-audio";
    case FF802_DCHK_AUTOMATION_ON: return "the automatic CC switch (ff802-cc-auto) is on";
    case FF802_DCHK_AUDIO_ACTIVE:  return "audio is playing or recording on the Fireface";
    case FF802_DCHK_ERROR:         break;
    }
    return "cannot read the USB devices in sysfs";
}

static void why_set(char *why, size_t size, const char *fmt, ...)
{
    if (why == NULL || size == 0)
        return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, size, fmt, ap);
    va_end(ap);
}

/* Read a small attribute below dir, strip the newline; "" if missing. */
static void attr(const char *dir, const char *name, char *buf, size_t size)
{
    char p[FF802_AUTO_PATH_MAX + 96u];
    buf[0] = '\0';
    int n = snprintf(p, sizeof p, "%s/%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof p)
        return;
    FILE *f = fopen(p, "r");
    if (f == NULL)
        return;
    if (fgets(buf, (int)size, f) == NULL)
        buf[0] = '\0';
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
}

static ff802_dchk_status check_interface(const ff802_dchk_roots *r,
                                         const ff802_sysfs_dev *d,
                                         char *why, size_t ws)
{
    char dev[FF802_AUTO_PATH_MAX + 48u], ifc[FF802_AUTO_PATH_MAX + 64u];
    char ep[FF802_AUTO_PATH_MAX + 80u], v[64];

    snprintf(dev, sizeof dev, "%s/bus/usb/devices/%s", r->paths.sysfs_root, d->kernel_name);
    attr(dev, "bConfigurationValue", v, sizeof v);
    if (strcmp(v, "1") != 0) {
        why_set(why, ws, "active configuration is '%s', expected 1", v);
        return FF802_DCHK_INTERFACE;
    }
    snprintf(ifc, sizeof ifc, "%s/bus/usb/devices/%s:1.3", r->paths.sysfs_root, d->kernel_name);
    attr(ifc, "bInterfaceClass", v, sizeof v);
    char sub[16];
    attr(ifc, "bInterfaceSubClass", sub, sizeof sub);
    if (strcmp(v, "01") != 0 || strcmp(sub, "03") != 0) {
        why_set(why, ws, "interface 3 is class '%s' subclass '%s', expected USB-MIDI "
                "streaming (01/03)", v, sub);
        return FF802_DCHK_INTERFACE;
    }
    snprintf(ep, sizeof ep, "%s/ep_07", ifc);
    char a[16], mp[16], dir[16];
    attr(ep, "bmAttributes", a, sizeof a);
    attr(ep, "wMaxPacketSize", mp, sizeof mp);
    attr(ep, "direction", dir, sizeof dir);
    if (strcmp(a, "02") != 0 || strcmp(mp, "0200") != 0 || strcmp(dir, "out") != 0) {
        why_set(why, ws, "endpoint 0x07 of interface 3: attributes '%s', max packet '%s', "
                "direction '%s'; expected bulk OUT with 512 bytes", a, mp, dir);
        return FF802_DCHK_INTERFACE;
    }
    char link[FF802_AUTO_PATH_MAX + 80u], target[PATH_MAX];
    snprintf(link, sizeof link, "%s/driver", ifc);
    ssize_t n = readlink(link, target, sizeof target - 1u);
    if (n < 0) {
        why_set(why, ws, "no kernel driver bound to interface 3, expected snd-usb-audio");
        return FF802_DCHK_DRIVER;
    }
    target[n] = '\0';
    const char *base = strrchr(target, '/');
    base = base ? base + 1 : target;
    if (strcmp(base, "snd-usb-audio") != 0) {
        why_set(why, ws, "interface 3 is bound to '%s', expected snd-usb-audio", base);
        return FF802_DCHK_DRIVER;
    }
    return FF802_DCHK_OK;
}

/* ALSA card of the unit: <device>:1.0/sound/card<N>; -1 if none. */
static int find_card(const ff802_dchk_roots *r, const ff802_sysfs_dev *d)
{
    char p[FF802_AUTO_PATH_MAX + 64u];
    snprintf(p, sizeof p, "%s/bus/usb/devices/%s:1.0/sound", r->paths.sysfs_root,
             d->kernel_name);
    DIR *dh = opendir(p);
    if (dh == NULL)
        return -1;
    int card = -1;
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        if (strncmp(de->d_name, "card", 4) != 0)
            continue;
        char *end = NULL;
        long v = strtol(de->d_name + 4, &end, 10);
        if (end != de->d_name + 4 && *end == '\0' && v >= 0 && v < 32)
            card = (int)v;
    }
    closedir(dh);
    return card;
}

/* Name of the first PCM substream that is not idle, or NULL. */
static int audio_active(const ff802_dchk_roots *r, int card, char *what, size_t ws)
{
    char cdir[FF802_AUTO_PATH_MAX + 16u];
    snprintf(cdir, sizeof cdir, "%s/card%d", r->proc_asound, card);
    DIR *ch = opendir(cdir);
    if (ch == NULL)
        return 0;
    int active = 0;
    struct dirent *pe;
    while (!active && (pe = readdir(ch)) != NULL) {
        if (strncmp(pe->d_name, "pcm", 3) != 0)
            continue;
        char pdir[FF802_AUTO_PATH_MAX + 300u];
        snprintf(pdir, sizeof pdir, "%s/%s", cdir, pe->d_name);
        DIR *ph = opendir(pdir);
        if (ph == NULL)
            continue;
        struct dirent *se;
        while (!active && (se = readdir(ph)) != NULL) {
            if (strncmp(se->d_name, "sub", 3) != 0)
                continue;
            char sdir[FF802_AUTO_PATH_MAX + 600u], line[64];
            snprintf(sdir, sizeof sdir, "%s/%s", pdir, se->d_name);
            attr(sdir, "status", line, sizeof line);
            static const char *busy[] = { "state: PREPARED", "state: RUNNING",
                                          "state: XRUN", "state: DRAINING",
                                          "state: PAUSED" };
            for (size_t i = 0; i < sizeof busy / sizeof busy[0]; i++)
                if (strcmp(line, busy[i]) == 0) {
                    why_set(what, ws, "card%d/%s/%s: %s", card, pe->d_name,
                            se->d_name, line + 7);
                    active = 1;
                }
        }
        closedir(ph);
    }
    closedir(ch);
    return active;
}

ff802_dchk_status ff802_disable_check(const ff802_dchk_roots *r,
                                      int select_location, uint8_t bus,
                                      uint8_t devnum, ff802_dchk_target *t,
                                      char *why, size_t ws)
{
    char dir[FF802_AUTO_PATH_MAX + 32u];
    ff802_sysfs_dev pick;
    unsigned candidates = 0;
    int bad_identity = 0;

    memset(t, 0, sizeof *t);
    t->card = -1;
    why_set(why, ws, "%s", "");
    snprintf(dir, sizeof dir, "%s/bus/usb/devices", r->paths.sysfs_root);
    DIR *dh = opendir(dir);
    if (dh == NULL) {
        why_set(why, ws, "cannot read %s", dir);
        return FF802_DCHK_ERROR;
    }
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        ff802_sysfs_dev d;
        if (!ff802_sysfs_valid_name(de->d_name) ||
            ff802_sysfs_read(r->paths.sysfs_root, de->d_name, &d) != FF802_SYSFS_OK)
            continue;
        if (d.vid != FF802_VID_CLASS_COMPLIANT || d.pid != FF802_PID_CLASS_COMPLIANT)
            continue;
        if (select_location && (d.bus != bus || d.devnum != devnum))
            continue;
        if (ff802_sysfs_identify(&d) != FF802_ID_CLASS_COMPLIANT) {
            bad_identity = 1;
            why_set(why, ws, "%s: %s", d.kernel_name,
                    ff802_identity_str(ff802_sysfs_identify(&d)));
            continue;
        }
        if (candidates++ == 0)
            pick = d;
    }
    closedir(dh);
    if (candidates == 0)
        return bad_identity ? FF802_DCHK_IDENTITY : FF802_DCHK_NOT_FOUND;
    if (candidates > 1) {
        why_set(why, ws, "%u units; select one with --device BUS:ADDRESS", candidates);
        return FF802_DCHK_AMBIGUOUS;
    }
    t->dev = pick;

    ff802_dchk_status st = check_interface(r, &pick, why, ws);
    if (st != FF802_DCHK_OK)
        return st;

    ff802_auto_mode mode = ff802_auto_read_mode(&r->paths);
    if (mode == FF802_AUTO_MODE_ON) {
        why_set(why, ws, "it would switch the unit back to CC mode at once; "
                "turn it off first: ff802-cc-auto off");
        return FF802_DCHK_AUTOMATION_ON;
    }

    t->card = find_card(r, &pick);
    if (t->card >= 0 && audio_active(r, t->card, why, ws))
        return FF802_DCHK_AUDIO_ACTIVE;
    return FF802_DCHK_OK;
}
