#define _POSIX_C_SOURCE 200809L

#include "ff802/mixer_port.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "ff802/auto_sysfs.h"

#define PATH_LEN 512u
#define CARD_MAX 255

/* "card<N>" with N 0..CARD_MAX, no leading zeros; -1 otherwise. */
static int card_number(const char *name)
{
    if (strncmp(name, "card", 4) != 0 || name[4] == '\0')
        return -1;
    const char *s = name + 4;
    if (s[0] == '0' && s[1] != '\0')
        return -1;
    int v = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9')
            return -1;
        v = v * 10 + (*s - '0');
        if (v > CARD_MAX)
            return -1;
    }
    return v;
}

/* "<k>:1.<n>" with n decimal: an interface of configuration 1 of <k>. */
static int is_interface(const char *entry, const char *k)
{
    size_t kl = strlen(k);
    if (strncmp(entry, k, kl) != 0 || strncmp(entry + kl, ":1.", 3) != 0)
        return 0;
    const char *n = entry + kl + 3;
    if (*n == '\0')
        return 0;
    for (; *n != '\0'; n++)
        if (*n < '0' || *n > '9')
            return 0;
    return 1;
}

static int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

ff802_mport_status ff802_mixer_find_card(const char *sysfs_root,
                                         const char *kernel_name, int *card)
{
    char dev[PATH_LEN];
    char path[PATH_LEN];
    int found = -1;

    if (sysfs_root == NULL || card == NULL || kernel_name == NULL ||
        !ff802_sysfs_valid_name(kernel_name))
        return FF802_MPORT_BAD;
    int n = snprintf(dev, sizeof dev, "%s/bus/usb/devices/%s", sysfs_root, kernel_name);
    if (n < 0 || (size_t)n >= sizeof dev)
        return FF802_MPORT_BAD;
    DIR *dh = opendir(dev);
    if (dh == NULL)
        return FF802_MPORT_NOT_YET;
    struct dirent *de;
    ff802_mport_status st = FF802_MPORT_NOT_YET;
    while ((de = readdir(dh)) != NULL) {
        if (!is_interface(de->d_name, kernel_name))
            continue;
        n = snprintf(path, sizeof path, "%s/%s/sound", dev, de->d_name);
        if (n < 0 || (size_t)n >= sizeof path)
            continue;
        DIR *sh = opendir(path);
        if (sh == NULL)
            continue;
        struct dirent *ce;
        while ((ce = readdir(sh)) != NULL) {
            int c = card_number(ce->d_name);
            if (c < 0)
                continue;
            char midi[PATH_LEN + 64u];
            n = snprintf(midi, sizeof midi, "%s/card%d/midiC%dD0", path, c, c);
            if (n < 0 || (size_t)n >= sizeof midi || !is_dir(midi))
                continue;
            if (found >= 0 && found != c)
                st = FF802_MPORT_AMBIGUOUS;
            found = c;
        }
        closedir(sh);
    }
    closedir(dh);
    if (st == FF802_MPORT_AMBIGUOUS)
        return st;
    if (found < 0)
        return FF802_MPORT_NOT_YET;
    *card = found;
    return FF802_MPORT_FOUND;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

ff802_seqsel ff802_mixer_select_port(const ff802_seq_port *ports, size_t n,
                                     int card, size_t *index)
{
    int client = -1;

    if (ports == NULL && n > 0)
        return FF802_SEQSEL_NO_CLIENT;
    for (size_t i = 0; i < n; i++) {
        if (!ports[i].kernel_client || ports[i].card != card || card < 0)
            continue;
        if (client >= 0 && ports[i].client != client)
            return FF802_SEQSEL_AMBIGUOUS;
        client = ports[i].client;
    }
    if (client < 0)
        return FF802_SEQSEL_NO_CLIENT;
    for (size_t i = 0; i < n; i++) {
        if (ports[i].client != client || ports[i].port != FF802_MIXER_PORT_INDEX)
            continue;
        if (!ports[i].writable || !ends_with(ports[i].name, FF802_MIXER_PORT_SUFFIX))
            return FF802_SEQSEL_MISMATCH;
        if (index != NULL)
            *index = i;
        return FF802_SEQSEL_FOUND;
    }
    return FF802_SEQSEL_NO_PORT;
}

const char *ff802_mport_status_str(ff802_mport_status s)
{
    switch (s) {
    case FF802_MPORT_FOUND:     return "found";
    case FF802_MPORT_NOT_YET:   return "no sound card with MIDI device for the connection";
    case FF802_MPORT_AMBIGUOUS: return "more than one sound card for the connection";
    case FF802_MPORT_BAD:       break;
    }
    return "invalid connection name or unreadable sysfs";
}

const char *ff802_seqsel_str(ff802_seqsel s)
{
    switch (s) {
    case FF802_SEQSEL_FOUND:     return "found";
    case FF802_SEQSEL_NO_CLIENT: return "no sequencer client of the card";
    case FF802_SEQSEL_NO_PORT:   return "sequencer client has no port 1";
    case FF802_SEQSEL_AMBIGUOUS: return "more than one sequencer client of the card";
    case FF802_SEQSEL_MISMATCH:  break;
    }
    return "port 1 is not a writable 'Port 2'";
}

ff802_unit_status ff802_mixer_find_unit(const char *sysfs_root, const char *name,
                                        ff802_sysfs_dev *out, unsigned *count)
{
    char dir[PATH_LEN];
    unsigned n = 0;

    if (count != NULL)
        *count = 0;
    if (sysfs_root == NULL || out == NULL)
        return FF802_UNIT_BAD;
    if (name != NULL) {
        ff802_sysfs_dev d;
        if (!ff802_sysfs_valid_name(name))
            return FF802_UNIT_BAD;
        ff802_sysfs_status st = ff802_sysfs_read(sysfs_root, name, &d);
        if (st == FF802_SYSFS_ABSENT)
            return FF802_UNIT_NONE;
        if (st != FF802_SYSFS_OK)
            return FF802_UNIT_BAD;
        if (ff802_sysfs_identify(&d) != FF802_ID_CLASS_COMPLIANT)
            return FF802_UNIT_NOT_CC;
        *out = d;
        if (count != NULL)
            *count = 1;
        return FF802_UNIT_FOUND;
    }
    int k = snprintf(dir, sizeof dir, "%s/bus/usb/devices", sysfs_root);
    if (k < 0 || (size_t)k >= sizeof dir)
        return FF802_UNIT_BAD;
    DIR *dh = opendir(dir);
    if (dh == NULL)
        return FF802_UNIT_BAD;
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        ff802_sysfs_dev d;
        if (!ff802_sysfs_valid_name(de->d_name) ||
            ff802_sysfs_read(sysfs_root, de->d_name, &d) != FF802_SYSFS_OK ||
            ff802_sysfs_identify(&d) != FF802_ID_CLASS_COMPLIANT)
            continue;
        if (n++ == 0)
            *out = d;
    }
    closedir(dh);
    if (count != NULL)
        *count = n;
    if (n == 0)
        return FF802_UNIT_NONE;
    return n == 1 ? FF802_UNIT_FOUND : FF802_UNIT_MULTIPLE;
}

const char *ff802_unit_status_str(ff802_unit_status s)
{
    switch (s) {
    case FF802_UNIT_FOUND:    return "found";
    case FF802_UNIT_NONE:     return "no original Fireface 802 in class-compliant mode";
    case FF802_UNIT_MULTIPLE: return "more than one Fireface 802 in class-compliant mode "
                                     "(name one with --connection)";
    case FF802_UNIT_NOT_CC:   return "not an original Fireface 802 in class-compliant mode";
    case FF802_UNIT_BAD:      break;
    }
    return "invalid connection name or unreadable sysfs";
}
