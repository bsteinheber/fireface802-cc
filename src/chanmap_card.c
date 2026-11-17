/*
 * Identification of the original Fireface 802 among the ALSA cards.
 * Reads procfs only; nothing is opened here.
 */
#include "ff802/chanmap.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FF802_USBID "0424:3fdd"

int chanmap_card_info_ok(const char *driver, const char *components,
                         const char *longname)
{
    return driver && components && longname &&
           strcmp(driver, "USB-Audio") == 0 &&
           strstr(components, "USB0424:3fdd") != NULL &&
           strstr(longname, "Fireface 802") != NULL &&
           strstr(longname, "802 FS") == NULL;
}

static int read_line(const char *path, char *buf, size_t len)
{
    FILE *f = fopen(path, "r");
    size_t n;

    if (!f)
        return -1;
    if (!fgets(buf, (int)len, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    n = strlen(buf);
    while (n > 0 && isspace((unsigned char)buf[n - 1]))
        buf[--n] = '\0';
    return 0;
}

/* Long name of card `index` from <proc_root>/cards: the line after
 * " N [id ]: driver - name". */
static int longname_of(const char *proc_root, int index, char *buf, size_t len)
{
    char path[512], line[512];
    FILE *f;
    int found = 0;

    snprintf(path, sizeof path, "%s/cards", proc_root);
    f = fopen(path, "r");
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *end;
        long n = strtol(line, &end, 10);
        if (end != line && n == index && strchr(end, '[') != NULL) {
            if (fgets(line, sizeof line, f)) {
                char *p = line;
                size_t l;
                while (*p == ' ' || *p == '\t')
                    p++;
                l = strlen(p);
                while (l > 0 && isspace((unsigned char)p[l - 1]))
                    p[--l] = '\0';
                snprintf(buf, len, "%s", p);
                found = 1;
            }
            break;
        }
    }
    fclose(f);
    return found ? 0 : -1;
}

int chanmap_find_card(const char *proc_root, struct chanmap_card *card,
                      char *why, size_t why_len)
{
    DIR *d = opendir(proc_root);
    struct dirent *e;
    int found = 0;

    if (why_len)
        why[0] = '\0';
    if (!d) {
        snprintf(why, why_len, "cannot read %s", proc_root);
        return -1;
    }
    while ((e = readdir(d)) != NULL) {
        char path[512], usbid[64], longname[128], id[32];
        char *end;
        long index;

        if (strncmp(e->d_name, "card", 4) != 0 || !isdigit((unsigned char)e->d_name[4]))
            continue;
        index = strtol(e->d_name + 4, &end, 10);
        if (*end != '\0' || index < 0 || index > 255)
            continue;
        snprintf(path, sizeof path, "%s/%s/usbid", proc_root, e->d_name);
        if (read_line(path, usbid, sizeof usbid) < 0 || strcmp(usbid, FF802_USBID) != 0)
            continue;
        if (longname_of(proc_root, (int)index, longname, sizeof longname) < 0) {
            snprintf(why, why_len, "card %ld: long name not found", index);
            continue;
        }
        if (strstr(longname, "Fireface 802") == NULL || strstr(longname, "802 FS") != NULL) {
            snprintf(why, why_len, "card %ld (%s) is not an original Fireface 802", index, longname);
            continue;
        }
        snprintf(path, sizeof path, "%s/%s/id", proc_root, e->d_name);
        if (read_line(path, id, sizeof id) < 0 || id[0] == '\0') {
            snprintf(why, why_len, "card %ld: id not readable", index);
            continue;
        }
        found++;
        if (found == 1) {
            card->index = (int)index;
            snprintf(card->id, sizeof card->id, "%s", id);
            snprintf(card->longname, sizeof card->longname, "%s", longname);
        }
    }
    closedir(d);
    if (found > 1) {
        snprintf(why, why_len, "%d original Fireface 802 cards found; refusing to guess", found);
        return -2;
    }
    if (found == 0 && why_len && why[0] == '\0')
        snprintf(why, why_len, "no ALSA card with USB id " FF802_USBID " (is the Fireface in CC mode?)");
    return found;
}
