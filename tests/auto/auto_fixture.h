/*
 * Test fixture for T4: a private directory with a fake sysfs tree, run
 * and state directories. Never touches the real /sys, /run or /var.
 */
#ifndef FF802_AUTO_FIXTURE_H
#define FF802_AUTO_FIXTURE_H

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff802/auto_state.h"

typedef struct fixture {
    char root[128];
    ff802_auto_paths paths;
} fixture;

static inline void fx_write(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        perror(path);
        exit(2);
    }
    fputs(content, f);
    fclose(f);
}

static inline void fx_rm_rf(const char *path)
{
    char cmd[256];
    if (strncmp(path, "/tmp/ff802-auto-test.", 21) != 0)
        return;   /* never remove anything else */
    snprintf(cmd, sizeof cmd, "rm -rf -- '%s'", path);
    if (system(cmd) != 0)
        fprintf(stderr, "cleanup of %s failed\n", path);
}

static inline void fx_init(fixture *fx)
{
    char tmpl[] = "/tmp/ff802-auto-test.XXXXXX";
    if (mkdtemp(tmpl) == NULL) {
        perror("mkdtemp");
        exit(2);
    }
    snprintf(fx->root, sizeof fx->root, "%s", tmpl);
    if (ff802_auto_paths_below(&fx->paths, fx->root) != 0)
        exit(2);
    char d[256];
    mkdir(fx->paths.state_dir, 0755);
    snprintf(d, sizeof d, "%s/bus", fx->paths.sysfs_root);
    mkdir(fx->paths.sysfs_root, 0755);
    mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/bus/usb", fx->paths.sysfs_root);
    mkdir(d, 0755);
    snprintf(d, sizeof d, "%s/bus/usb/devices", fx->paths.sysfs_root);
    mkdir(d, 0755);
}

static inline void fx_done(fixture *fx) { fx_rm_rf(fx->root); }

/* Create (or replace) a USB device directory. NULL strings are omitted. */
static inline void fx_dev(fixture *fx, const char *name, const char *vid,
                   const char *pid, unsigned bus, unsigned devnum,
                   const char *speed, const char *manufacturer,
                   const char *product)
{
    char dir[256], path[320], buf[32];
    snprintf(dir, sizeof dir, "%s/bus/usb/devices/%s", fx->paths.sysfs_root, name);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/idVendor", dir);  snprintf(buf, sizeof buf, "%s\n", vid);   fx_write(path, buf);
    snprintf(path, sizeof path, "%s/idProduct", dir); snprintf(buf, sizeof buf, "%s\n", pid);   fx_write(path, buf);
    snprintf(path, sizeof path, "%s/busnum", dir);    snprintf(buf, sizeof buf, "%u\n", bus);   fx_write(path, buf);
    snprintf(path, sizeof path, "%s/devnum", dir);    snprintf(buf, sizeof buf, "%u\n", devnum); fx_write(path, buf);
    snprintf(path, sizeof path, "%s/speed", dir);     snprintf(buf, sizeof buf, "%s\n", speed); fx_write(path, buf);
    snprintf(path, sizeof path, "%s/manufacturer", dir);
    if (manufacturer) { char m[96]; snprintf(m, sizeof m, "%s\n", manufacturer); fx_write(path, m); }
    else unlink(path);
    snprintf(path, sizeof path, "%s/product", dir);
    if (product) { char m[96]; snprintf(m, sizeof m, "%s\n", product); fx_write(path, m); }
    else unlink(path);
}

/* The Fireface 802 of the host in proprietary mode at 3-2.4.1.1.3. */
static inline void fx_ff802_prop(fixture *fx, unsigned devnum)
{
    fx_dev(fx, "3-2.4.1.1.3", "2a39", "3fcd", 3, devnum, "480", "RME",
           "Fireface 802 (24047128)");
}

static inline void fx_ff802_cc(fixture *fx, unsigned devnum)
{
    fx_dev(fx, "3-2.4.1.1.3", "0424", "3fdd", 3, devnum, "480", "RME",
           "Fireface 802 (24047128)");
}

static inline void fx_unplug(fixture *fx, const char *name)
{
    char dir[256];
    snprintf(dir, sizeof dir, "%s/bus/usb/devices/%s", fx->paths.sysfs_root, name);
    fx_rm_rf(dir);
}

static inline void fx_mode(fixture *fx, const char *content)
{
    char path[256];
    snprintf(path, sizeof path, "%s/mode", fx->paths.state_dir);
    if (content == NULL)
        unlink(path);
    else
        fx_write(path, content);
}

static inline int fx_exists(fixture *fx, const char *rel)
{
    char path[320];
    struct stat st;
    snprintf(path, sizeof path, "%s/%s", fx->root, rel);
    return stat(path, &st) == 0;
}

/* Read a file below the root into buf (empty string if missing). */
static inline const char *fx_read(fixture *fx, const char *rel, char *buf, size_t size)
{
    char path[320];
    snprintf(path, sizeof path, "%s/%s", fx->root, rel);
    buf[0] = '\0';
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return buf;
    size_t n = fread(buf, 1, size - 1u, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

#endif /* FF802_AUTO_FIXTURE_H */
