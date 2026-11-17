#define _POSIX_C_SOURCE 200809L

#include "ff802/auto_sysfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff802/device_id.h"

/* Parse decimal 1..max without leading zeros; advances *p. */
static int parse_num(const char **p, unsigned max, unsigned *out)
{
    const char *s = *p;
    unsigned v = 0;
    unsigned digits = 0;

    if (*s < '1' || *s > '9')
        return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10u + (unsigned)(*s - '0');
        if (v > max || ++digits > 3u)
            return -1;
        s++;
    }
    *p = s;
    *out = v;
    return 0;
}

int ff802_sysfs_valid_name(const char *name)
{
    const char *p = name;
    unsigned v;
    unsigned ports = 0;

    if (name == NULL || strlen(name) >= FF802_SYSFS_NAME_MAX)
        return 0;
    if (parse_num(&p, 255u, &v) != 0 || *p != '-')
        return 0;
    p++;
    for (;;) {
        if (parse_num(&p, 255u, &v) != 0)
            return 0;
        if (++ports > FF802_MAX_PORT_DEPTH)
            return 0;
        if (*p == '\0')
            return 1;
        if (*p != '.')
            return 0;
        p++;
    }
}

/* Read one attribute (at most size-1 bytes), strip one trailing newline. */
static int read_attr(const char *dir, const char *attr, char *buf, size_t size)
{
    char path[256];
    int n = snprintf(path, sizeof path, "%s/%s", dir, attr);
    if (n < 0 || (size_t)n >= sizeof path)
        return -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    ssize_t r = read(fd, buf, size - 1u);
    close(fd);
    if (r < 0)
        return -1;
    buf[r] = '\0';
    size_t len = (size_t)r;
    if (len > 0 && buf[len - 1] == '\n')
        buf[--len] = '\0';
    for (size_t i = 0; i < len; i++)
        if ((unsigned char)buf[i] < 0x20u || (unsigned char)buf[i] > 0x7Eu)
            return -1;   /* printable ASCII only */
    return 0;
}

static int parse_hex4(const char *s, uint16_t *out)
{
    unsigned v = 0;
    if (strlen(s) != 4u)
        return -1;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9')      d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return -1;
        v = (v << 4) | d;
    }
    *out = (uint16_t)v;
    return 0;
}

/* sysfs busnum/devnum: decimal, no leading zeros, 1..max. */
static int parse_dec(const char *s, unsigned max, unsigned *out)
{
    const char *p = s;
    if (parse_num(&p, max, out) != 0 || *p != '\0')
        return -1;
    return 0;
}

ff802_sysfs_status ff802_sysfs_read(const char *root, const char *name,
                                    ff802_sysfs_dev *out)
{
    char dir[200];
    char buf[FF802_SYSFS_STR_MAX];
    unsigned v;
    struct stat st;

    memset(out, 0, sizeof *out);
    if (root == NULL || !ff802_sysfs_valid_name(name))
        return FF802_SYSFS_BAD_NAME;
    int n = snprintf(dir, sizeof dir, "%s/bus/usb/devices/%s", root, name);
    if (n < 0 || (size_t)n >= sizeof dir)
        return FF802_SYSFS_BAD_NAME;
    if (stat(dir, &st) != 0)
        return errno == ENOENT ? FF802_SYSFS_ABSENT : FF802_SYSFS_UNREADABLE;
    memcpy(out->kernel_name, name, strlen(name) + 1u);

    if (read_attr(dir, "idVendor", buf, sizeof buf) != 0 ||
        parse_hex4(buf, &out->vid) != 0)
        return FF802_SYSFS_UNREADABLE;
    if (read_attr(dir, "idProduct", buf, sizeof buf) != 0 ||
        parse_hex4(buf, &out->pid) != 0)
        return FF802_SYSFS_UNREADABLE;
    if (read_attr(dir, "busnum", buf, sizeof buf) != 0 ||
        parse_dec(buf, 255u, &v) != 0)
        return FF802_SYSFS_UNREADABLE;
    out->bus = (uint8_t)v;
    if (read_attr(dir, "devnum", buf, sizeof buf) != 0 ||
        parse_dec(buf, 127u, &v) != 0)
        return FF802_SYSFS_UNREADABLE;
    out->devnum = (uint8_t)v;
    if (read_attr(dir, "speed", out->speed, sizeof out->speed) != 0)
        return FF802_SYSFS_UNREADABLE;
    /* String attributes exist only if the device reports them. */
    if (read_attr(dir, "manufacturer", out->manufacturer,
                  sizeof out->manufacturer) != 0)
        out->manufacturer[0] = '\0';
    if (read_attr(dir, "product", out->product, sizeof out->product) != 0)
        out->product[0] = '\0';
    return FF802_SYSFS_OK;
}

int ff802_sysfs_product_ok(const char *product)
{
    static const char base[] = "Fireface 802";
    const size_t blen = sizeof base - 1u;

    if (product == NULL || strncmp(product, base, blen) != 0)
        return 0;
    const char *p = product + blen;
    if (*p == '\0')
        return 1;
    if (p[0] != ' ' || p[1] != '(')
        return 0;
    p += 2;
    size_t digits = 0;
    while (*p >= '0' && *p <= '9') {
        p++;
        digits++;
    }
    return digits >= 1u && digits <= 16u && p[0] == ')' && p[1] == '\0';
}

ff802_identity ff802_sysfs_identify(const ff802_sysfs_dev *d)
{
    ff802_mode mode = ff802_classify(d->vid, d->pid);
    if (mode != FF802_MODE_PROPRIETARY && mode != FF802_MODE_CLASS_COMPLIANT)
        return FF802_ID_UNKNOWN_DEVICE;
    if (strcmp(d->manufacturer, "RME") != 0)
        return FF802_ID_BAD_MANUFACTURER;
    if (!ff802_sysfs_product_ok(d->product))
        return FF802_ID_BAD_PRODUCT;
    if (strcmp(d->speed, "480") != 0)
        return FF802_ID_NOT_HIGH_SPEED;
    return mode == FF802_MODE_PROPRIETARY ? FF802_ID_PROPRIETARY
                                          : FF802_ID_CLASS_COMPLIANT;
}

const char *ff802_identity_str(ff802_identity id)
{
    switch (id) {
    case FF802_ID_PROPRIETARY:      return "Fireface 802, proprietary mode";
    case FF802_ID_CLASS_COMPLIANT:  return "Fireface 802, class-compliant mode";
    case FF802_ID_UNKNOWN_DEVICE:   return "not a Fireface 802 (USB ID)";
    case FF802_ID_BAD_MANUFACTURER: return "manufacturer string is not 'RME'";
    case FF802_ID_BAD_PRODUCT:      return "product string is not 'Fireface 802 (...)'";
    case FF802_ID_NOT_HIGH_SPEED:   return "not connected at high speed (480 Mbit/s)";
    }
    return "unknown identity";
}

int ff802_sysfs_same_connection(const ff802_sysfs_dev *a,
                                const ff802_sysfs_dev *b)
{
    return strcmp(a->kernel_name, b->kernel_name) == 0 &&
           a->bus == b->bus && a->devnum == b->devnum &&
           a->vid == b->vid && a->pid == b->pid &&
           strcmp(a->speed, b->speed) == 0 &&
           strcmp(a->manufacturer, b->manufacturer) == 0 &&
           strcmp(a->product, b->product) == 0;
}

int ff802_sysfs_matches_usb_dev(const ff802_sysfs_dev *s,
                                const ff802_usb_dev *u)
{
    char port[FF802_SYSFS_NAME_MAX + 8u];
    int n = ff802_format_port_path(u, port, sizeof port);
    if (n < 0 || (size_t)n >= sizeof port)
        return 0;
    return u->bus == s->bus && u->address == s->devnum &&
           u->vid == s->vid && u->pid == s->pid &&
           strcmp(port, s->kernel_name) == 0;
}
