#define _XOPEN_SOURCE 700

#include "ff802/usb_port.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

const char *ff802_port_status_str(ff802_port_status st)
{
    switch (st) {
    case FF802_PORT_OK:         return "port found";
    case FF802_PORT_BAD_NAME:   return "invalid USB device name";
    case FF802_PORT_NO_DEVICE:  return "device not present in sysfs";
    case FF802_PORT_NOT_FOUND:  return "hub port of the device not found";
    case FF802_PORT_MISMATCH:   return "hub port belongs to another device";
    case FF802_PORT_NO_DISABLE: return "hub port has no 'disable' attribute "
                                       "(kernel too old?)";
    }
    return "unknown port status";
}

/* realpath of <root>/bus/usb/devices/<name>, 0 on success */
static int device_realpath(const char *root, const char *name, char *out)
{
    char p[FF802_PORT_PATH_MAX];
    int n = snprintf(p, sizeof p, "%s/bus/usb/devices/%s", root, name);
    if (n < 0 || (size_t)n >= sizeof p)
        return -1;
    return realpath(p, out) != NULL ? 0 : -1;
}

int ff802_port_belongs_to(const ff802_usb_port *port, const char *sysfs_root,
                          const char *kernel_name)
{
    char link[FF802_PORT_PATH_MAX + 8u];
    char a[PATH_MAX], b[PATH_MAX];

    int n = snprintf(link, sizeof link, "%s/device", port->dir);
    if (n < 0 || (size_t)n >= sizeof link)
        return 0;
    if (realpath(link, a) == NULL || device_realpath(sysfs_root, kernel_name, b) != 0)
        return 0;
    return strcmp(a, b) == 0;
}

ff802_port_status ff802_port_resolve(const char *sysfs_root,
                                     const char *kernel_name,
                                     ff802_usb_port *out)
{
    char dev[PATH_MAX];
    char iface_prefix[FF802_SYSFS_NAME_MAX + 4u];
    char portname[FF802_SYSFS_NAME_MAX + 16u];
    char hubdir[FF802_PORT_PATH_MAX];
    struct stat st;

    memset(out, 0, sizeof *out);
    if (sysfs_root == NULL || !ff802_sysfs_valid_name(kernel_name))
        return FF802_PORT_BAD_NAME;
    if (device_realpath(sysfs_root, kernel_name, dev) != 0)
        return FF802_PORT_NO_DEVICE;

    /* "B-P1.P2...Pn": hub "B-P1...P(n-1)", or the root hub "usbB" */
    const char *dash = strchr(kernel_name, '-');
    const char *dot = strrchr(kernel_name, '.');
    const char *last = dot != NULL ? dot + 1 : dash + 1;
    out->number = (unsigned)strtoul(last, NULL, 10);
    size_t buslen = (size_t)(dash - kernel_name);
    if (dot == NULL) {
        snprintf(out->hub, sizeof out->hub, "usb%.*s", (int)buslen, kernel_name);
        snprintf(iface_prefix, sizeof iface_prefix, "%.*s-0:", (int)buslen, kernel_name);
    } else {
        snprintf(out->hub, sizeof out->hub, "%.*s", (int)(dot - kernel_name), kernel_name);
        snprintf(iface_prefix, sizeof iface_prefix, "%s:", out->hub);
    }
    snprintf(portname, sizeof portname, "%s-port%u", out->hub, out->number);

    int n = snprintf(hubdir, sizeof hubdir, "%s/bus/usb/devices/%s", sysfs_root, out->hub);
    if (n < 0 || (size_t)n >= sizeof hubdir)
        return FF802_PORT_NOT_FOUND;
    DIR *dh = opendir(hubdir);
    if (dh == NULL)
        return FF802_PORT_NOT_FOUND;
    int found = 0;
    struct dirent *de;
    while ((de = readdir(dh)) != NULL) {
        if (strncmp(de->d_name, iface_prefix, strlen(iface_prefix)) != 0)
            continue;
        char cand[FF802_PORT_PATH_MAX];
        n = snprintf(cand, sizeof cand, "%s/%s/%s", hubdir, de->d_name, portname);
        if (n < 0 || (size_t)n >= sizeof cand || stat(cand, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        if (found) {          /* two hub interfaces with this port: refuse */
            closedir(dh);
            return FF802_PORT_NOT_FOUND;
        }
        memcpy(out->dir, cand, (size_t)n + 1u);
        found = 1;
    }
    closedir(dh);
    if (!found)
        return FF802_PORT_NOT_FOUND;
    if (!ff802_port_belongs_to(out, sysfs_root, kernel_name))
        return FF802_PORT_MISMATCH;
    char attr[FF802_PORT_PATH_MAX + 16u];
    n = snprintf(attr, sizeof attr, "%s/disable", out->dir);
    if (n < 0 || (size_t)n >= sizeof attr || stat(attr, &st) != 0 || !S_ISREG(st.st_mode))
        return FF802_PORT_NO_DISABLE;
    return FF802_PORT_OK;
}

static void do_sleep(const ff802_port_ops *ops, unsigned ms)
{
    if (ops != NULL && ops->sleep_ms != NULL) {
        ops->sleep_ms(ops->user, ms);
        return;
    }
    struct timespec ts = { .tv_sec = (time_t)(ms / 1000u),
                           .tv_nsec = (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
        ;
}

/* Write one value to <port>/disable. Returns 0 or errno. */
static int write_disable(const ff802_usb_port *port, const char *value)
{
    char attr[FF802_PORT_PATH_MAX + 16u];
    int n = snprintf(attr, sizeof attr, "%s/disable", port->dir);
    if (n < 0 || (size_t)n >= sizeof attr)
        return ENAMETOOLONG;
    int fd = open(attr, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return errno;
    size_t len = strlen(value);
    ssize_t w = write(fd, value, len);
    int err = (w < 0) ? errno : ((size_t)w != len ? EIO : 0);
    if (close(fd) != 0 && err == 0)
        err = errno;
    return err;
}

int ff802_port_cycle(const ff802_usb_port *port, unsigned off_ms,
                     const ff802_port_ops *ops, ff802_port_cycle_result *r)
{
    sigset_t block, old;

    memset(r, 0, sizeof *r);
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGHUP);
    sigaddset(&block, SIGQUIT);
    int masked = sigprocmask(SIG_BLOCK, &block, &old) == 0;

    r->err_disable = write_disable(port, "1\n");
    r->disabled = r->err_disable == 0;
    if (r->disabled)
        do_sleep(ops, off_ms);
    /* always try to enable the port again */
    r->err_enable = write_disable(port, "0\n");
    r->enabled = r->err_enable == 0;

    if (masked)
        sigprocmask(SIG_SETMASK, &old, NULL);
    return (r->disabled && r->enabled) ? 0 : -1;
}

ff802_wait_status ff802_port_wait_mode(const char *sysfs_root,
                                       const char *kernel_name,
                                       ff802_mode want, unsigned timeout_ms,
                                       const ff802_port_ops *ops,
                                       ff802_sysfs_dev *found)
{
    ff802_sysfs_dev d;
    int seen = 0;

    memset(found, 0, sizeof *found);
    for (unsigned waited = 0;; waited += 100u) {
        if (ff802_sysfs_read(sysfs_root, kernel_name, &d) == FF802_SYSFS_OK) {
            *found = d;
            seen = 1;
            if (ff802_classify(d.vid, d.pid) == want)
                return FF802_WAIT_OK;
        }
        if (waited >= timeout_ms)
            break;
        do_sleep(ops, 100u);
    }
    return seen ? FF802_WAIT_OTHER : FF802_WAIT_TIMEOUT;
}
