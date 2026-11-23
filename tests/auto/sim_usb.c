#define _POSIX_C_SOURCE 200809L

#include "sim_usb.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "ff802/auto_sysfs.h"

#define SIM_MAX_DEVS 16

typedef struct sim {
    ff802_auto_paths paths;
    char log_path[FF802_AUTO_PATH_MAX + 16u];
    ff802_usb_dev devs[SIM_MAX_DEVS];
    char names[SIM_MAX_DEVS][FF802_SYSFS_NAME_MAX];
    size_t ndevs;
    int opened;   /* index of the opened device, -1 */
} sim;

__attribute__((format(printf, 2, 3)))
static void slog(sim *s, const char *fmt, ...)
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

static ff802_usb_err env_err(const char *name)
{
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0') return FF802_USB_OK;
    if (strcmp(v, "access") == 0)  return FF802_USB_ERR_ACCESS;
    if (strcmp(v, "nodev") == 0)   return FF802_USB_ERR_NO_DEVICE;
    if (strcmp(v, "busy") == 0)    return FF802_USB_ERR_BUSY;
    if (strcmp(v, "timeout") == 0) return FF802_USB_ERR_TIMEOUT;
    if (strcmp(v, "pipe") == 0)    return FF802_USB_ERR_PIPE;
    return FF802_USB_ERR_IO;
}

/* "3-2.4.1" -> ports 2,4,1 */
static void parse_ports(const char *name, ff802_usb_dev *d)
{
    const char *p = strchr(name, '-');
    d->port_count = 0;
    while (p != NULL && d->port_count < FF802_MAX_PORT_DEPTH) {
        d->ports[d->port_count++] = (uint8_t)atoi(p + 1);
        p = strchr(p + 1, '.');
    }
}

static ff802_usb_err s_list(void *self, ff802_usb_dev **devs, size_t *n)
{
    sim *s = self;
    char dir[FF802_AUTO_PATH_MAX + 32u];
    snprintf(dir, sizeof dir, "%s/bus/usb/devices", s->paths.sysfs_root);
    s->ndevs = 0;
    DIR *dh = opendir(dir);
    if (dh != NULL) {
        struct dirent *de;
        while ((de = readdir(dh)) != NULL && s->ndevs < SIM_MAX_DEVS) {
            ff802_sysfs_dev sd;
            if (!ff802_sysfs_valid_name(de->d_name) ||
                ff802_sysfs_read(s->paths.sysfs_root, de->d_name, &sd) != FF802_SYSFS_OK)
                continue;
            ff802_usb_dev *u = &s->devs[s->ndevs];
            memset(u, 0, sizeof *u);
            u->bus = sd.bus;
            u->address = sd.devnum;
            u->vid = sd.vid;
            u->pid = sd.pid;
            u->bcd_device = 0x0001;
            parse_ports(de->d_name, u);
            size_t nl = strlen(de->d_name);   /* < FF802_SYSFS_NAME_MAX (valid name) */
            memcpy(s->names[s->ndevs], de->d_name, nl + 1u);
            s->ndevs++;
        }
        closedir(dh);
    }
    slog(s, "list %zu", s->ndevs);
    *devs = s->devs;
    *n = s->ndevs;
    return FF802_USB_OK;
}

static void s_free(void *self, ff802_usb_dev *devs)
{
    (void)self;
    (void)devs;
}

static ff802_usb_err s_layout(void *self, const ff802_usb_dev *dev, uint8_t iface,
                              uint8_t alt, ff802_dev_layout *out)
{
    sim *s = self;
    memset(out, 0, sizeof *out);
    slog(s, "layout %u:%u if %u alt %u", (unsigned)dev->bus,
         (unsigned)dev->address, (unsigned)iface, (unsigned)alt);
    out->config_value = 1;
    out->has_interface = 1;
    out->num_endpoints = 2;
    out->eps[0] = (ff802_ep_info){ .address = 0x8C, .attributes = 0x02, .max_packet = 512 };
    out->eps[1] = (ff802_ep_info){ .address = env_is("FF802_SIM_LAYOUT", "bad") ? 0x0D : 0x0C,
                                   .attributes = 0x02, .max_packet = 512 };
    return FF802_USB_OK;
}

static void sysfs_attr(sim *s, const char *name, const char *attr, const char *val)
{
    char path[FF802_AUTO_PATH_MAX + 96u];
    snprintf(path, sizeof path, "%s/bus/usb/devices/%s/%s", s->paths.sysfs_root,
             name, attr);
    FILE *f = fopen(path, "w");
    if (f != NULL) {
        fprintf(f, "%s\n", val);
        fclose(f);
    }
}

static void sysfs_remove(sim *s, const char *name)
{
    char cmd[FF802_AUTO_PATH_MAX + 96u];
    snprintf(cmd, sizeof cmd, "rm -rf -- '%s/bus/usb/devices/%s'",
             s->paths.sysfs_root, name);
    if (system(cmd) != 0)
        slog(s, "sim: removing %s failed", name);
}

static ff802_usb_err s_open(void *self, const ff802_usb_dev *dev, ff802_usb_handle *h)
{
    sim *s = self;
    *h = NULL;
    slog(s, "open %u:%u", (unsigned)dev->bus, (unsigned)dev->address);
    ff802_usb_err e = env_err("FF802_SIM_OPEN_ERR");
    if (e != FF802_USB_OK)
        return e;
    s->opened = -1;
    for (size_t i = 0; i < s->ndevs; i++)
        if (s->devs[i].bus == dev->bus && s->devs[i].address == dev->address)
            s->opened = (int)i;
    if (s->opened < 0)
        return FF802_USB_ERR_NO_DEVICE;
    const char *name = s->names[s->opened];
    if (env_is("FF802_SIM_REPLUG_ON_OPEN", "1")) {
        char buf[8];
        snprintf(buf, sizeof buf, "%u", (unsigned)dev->address + 1u);
        sysfs_attr(s, name, "devnum", buf);
        slog(s, "sim: %s reconnected as devnum %s", name, buf);
    }
    if (env_is("FF802_SIM_UNPLUG_ON_OPEN", "1")) {
        sysfs_remove(s, name);
        slog(s, "sim: %s removed", name);
    }
    *h = s;
    return FF802_USB_OK;
}

static void s_close(void *self, ff802_usb_handle h)
{
    (void)h;
    slog(self, "close");
}

static ff802_usb_err s_kdrv(void *self, ff802_usb_handle h, uint8_t iface, int *active)
{
    (void)h;
    slog(self, "kdrv if %u", (unsigned)iface);
    *active = env_is("FF802_SIM_KDRV", "1");
    return FF802_USB_OK;
}

static ff802_usb_err s_claim(void *self, ff802_usb_handle h, uint8_t iface)
{
    (void)h;
    slog(self, "claim if %u", (unsigned)iface);
    return env_err("FF802_SIM_CLAIM_ERR");
}

static ff802_usb_err s_release(void *self, ff802_usb_handle h, uint8_t iface)
{
    (void)h;
    slog(self, "release if %u", (unsigned)iface);
    return FF802_USB_OK;
}

static ff802_usb_err s_write(void *self, ff802_usb_handle h, uint8_t ep,
                             const uint8_t *data, size_t len, unsigned timeout_ms,
                             size_t *transferred)
{
    sim *s = self;
    (void)h;
    const char *delay = getenv("FF802_SIM_WRITE_DELAY_MS");
    if (delay != NULL) {
        long ms = atol(delay);
        struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
        nanosleep(&ts, NULL);
    }
    char bytes[64] = "";
    for (size_t i = 0; i < len && i < 8; i++) {
        char b[4];
        snprintf(b, sizeof b, "%s%02X", i ? " " : "", data[i]);
        strncat(bytes, b, sizeof bytes - strlen(bytes) - 1u);
    }
    const ff802_usb_dev *d = s->opened >= 0 ? &s->devs[s->opened] : NULL;
    slog(s, "write ep=0x%02X bytes=%s bus=%u addr=%u timeout=%u", (unsigned)ep,
         bytes, d ? (unsigned)d->bus : 0u, d ? (unsigned)d->address : 0u, timeout_ms);
    *transferred = len;
    const char *shortw = getenv("FF802_SIM_WRITE_SHORT");
    if (shortw != NULL)
        *transferred = (size_t)atoi(shortw);
    if (env_is("FF802_SIM_UNPLUG_ON_WRITE", "1") && d != NULL) {
        sysfs_remove(s, s->names[s->opened]);
        *transferred = 0;
        return FF802_USB_ERR_NO_DEVICE;
    }
    ff802_usb_err e = env_err("FF802_SIM_WRITE_ERR");
    if (e != FF802_USB_OK && shortw == NULL)
        *transferred = 0;
    return e;
}

ff802_usb_err sim_usb_open(const ff802_auto_paths *p, ff802_usb_backend *be)
{
    memset(be, 0, sizeof *be);
    sim *s = calloc(1, sizeof *s);
    if (s == NULL)
        return FF802_USB_ERR_NO_MEM;
    s->paths = *p;
    s->opened = -1;
    const char *root = getenv("FF802_AUTO_ROOT");
    snprintf(s->log_path, sizeof s->log_path, "%s/usb.log", root ? root : "/nonexistent");
    if (env_is("FF802_SIM_INIT_ERR", "1")) {
        slog(s, "init failed");
        free(s);
        return FF802_USB_ERR_OTHER;
    }
    slog(s, "init");
    be->self = s;
    be->write_enabled = 1;
    be->list_devices = s_list;
    be->free_devices = s_free;
    be->get_layout = s_layout;
    be->open = s_open;
    be->close = s_close;
    be->kernel_driver_active = s_kdrv;
    be->claim = s_claim;
    be->release = s_release;
    be->bulk_write = s_write;
    return FF802_USB_OK;
}

void sim_usb_close(ff802_usb_backend *be)
{
    sim *s = be->self;
    if (s != NULL) {
        slog(s, "exit");
        free(s);
    }
    memset(be, 0, sizeof *be);
}

/* --- simulated port reset ------------------------------------------------- */
typedef struct sim_port {
    sim      s;              /* only paths and log_path are used */
    char     name[FF802_SYSFS_NAME_MAX];
    char     saved[FF802_AUTO_PATH_MAX + 32u];
    char     port_dir[FF802_PORT_PATH_MAX];
    unsigned calls;
} sim_port;

static sim_port g_port;

static void read_port_disable(sim_port *sp, char *buf, size_t size)
{
    char path[FF802_PORT_PATH_MAX + 16u];
    snprintf(buf, size, "?");
    if (sp->port_dir[0] == '\0')
        return;
    snprintf(path, sizeof path, "%s/disable", sp->port_dir);
    FILE *f = fopen(path, "r");
    if (f == NULL)
        return;
    if (fgets(buf, (int)size, f) == NULL)
        snprintf(buf, size, "?");
    buf[strcspn(buf, "\n")] = '\0';
    fclose(f);
}

static void port_sleep(void *user, unsigned ms)
{
    sim_port *sp = user;
    char dev[FF802_AUTO_PATH_MAX + 96u];
    char val[16];
    snprintf(dev, sizeof dev, "%s/bus/usb/devices/%s", sp->s.paths.sysfs_root, sp->name);

    sp->calls++;
    if (sp->calls == 1u) {
        /* off phase: the device disappears */
        read_port_disable(sp, val, sizeof val);
        slog(&sp->s, "port off %u ms disable=%s", ms, val);
        if (rename(dev, sp->saved) != 0)
            slog(&sp->s, "sim: %s was not there", sp->name);
        if (env_is("FF802_SIM_PORT_STUCK", "1") && sp->port_dir[0] != '\0') {
            /* "disable" can no longer be written: "0" fails (EISDIR) */
            char attr[FF802_PORT_PATH_MAX + 16u];
            snprintf(attr, sizeof attr, "%s/disable", sp->port_dir);
            if (unlink(attr) != 0 || mkdir(attr, 0755) != 0)
                slog(&sp->s, "sim: cannot break %s", attr);
        }
        return;
    }
    if (sp->calls == 2u) {
        read_port_disable(sp, val, sizeof val);
        slog(&sp->s, "port on disable=%s", val);
    }
    const char *mode = getenv("FF802_SIM_REENUM");
    const char *after = getenv("FF802_SIM_REENUM_AFTER");
    unsigned polls = after != NULL ? (unsigned)atoi(after) : 3u;
    if (mode == NULL || strcmp(mode, "none") == 0 || sp->calls != polls + 2u)
        return;
    if (rename(sp->saved, dev) != 0) {
        slog(&sp->s, "sim: cannot re-create %s", sp->name);
        return;
    }
    char num[16];
    FILE *f;
    char path[FF802_AUTO_PATH_MAX + 128u];
    snprintf(path, sizeof path, "%s/devnum", dev);
    unsigned old = 0;
    if ((f = fopen(path, "r")) != NULL) {
        if (fscanf(f, "%u", &old) != 1)
            old = 0;
        fclose(f);
    }
    snprintf(num, sizeof num, "%u", old + 1u);
    sysfs_attr(&sp->s, sp->name, "devnum", num);
    if (strcmp(mode, "cc") == 0) {
        sysfs_attr(&sp->s, sp->name, "idVendor", "0424");
        sysfs_attr(&sp->s, sp->name, "idProduct", "3fdd");
    }
    slog(&sp->s, "sim: %s re-enumerated (%s) as devnum %s after %u polls", sp->name,
         mode, num, polls);
}

const ff802_port_ops *sim_port_setup(const ff802_auto_paths *p, const char *kernel_name,
                                     ff802_port_ops *ops)
{
    memset(&g_port, 0, sizeof g_port);
    g_port.s.paths = *p;
    const char *root = getenv("FF802_AUTO_ROOT");
    snprintf(g_port.s.log_path, sizeof g_port.s.log_path, "%s/usb.log",
             root ? root : "/nonexistent");
    snprintf(g_port.name, sizeof g_port.name, "%s", kernel_name);
    snprintf(g_port.saved, sizeof g_port.saved, "%s/sim-unplugged",
             root ? root : "/nonexistent");
    ff802_usb_port port;
    if (ff802_port_resolve(p->sysfs_root, kernel_name, &port) == FF802_PORT_OK)
        snprintf(g_port.port_dir, sizeof g_port.port_dir, "%s", port.dir);
    ops->sleep_ms = port_sleep;
    ops->user = &g_port;
    return ops;
}
