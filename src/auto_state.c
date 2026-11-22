#define _POSIX_C_SOURCE 200809L

#include "ff802/auto_state.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int set_path(char *dst, const char *a, const char *b)
{
    int n = snprintf(dst, FF802_AUTO_PATH_MAX, "%s%s", a, b);
    return (n < 0 || (size_t)n >= FF802_AUTO_PATH_MAX) ? -1 : 0;
}

void ff802_auto_default_paths(ff802_auto_paths *p)
{
    memset(p, 0, sizeof *p);
    (void)set_path(p->run_dir, "/run/ff802-cc-auto", "");
    (void)set_path(p->state_dir, "/var/lib/ff802-cc-auto", "");
    (void)set_path(p->sysfs_root, "/sys", "");
}

int ff802_auto_paths_below(ff802_auto_paths *p, const char *root)
{
    memset(p, 0, sizeof *p);
    if (root == NULL || root[0] != '/')
        return -1;
    if (set_path(p->run_dir, root, "/run/ff802-cc-auto") != 0 ||
        set_path(p->state_dir, root, "/var/lib/ff802-cc-auto") != 0 ||
        set_path(p->sysfs_root, root, "/sys") != 0)
        return -1;
    return 0;
}

static int join(char *dst, size_t size, const char *dir, const char *name)
{
    int n = snprintf(dst, size, "%s/%s", dir, name);
    return (n < 0 || (size_t)n >= size) ? -1 : 0;
}

static ff802_auto_mode read_onoff(const ff802_auto_paths *p, const char *name)
{
    char path[FF802_AUTO_PATH_MAX + 16u];
    char buf[16];

    if (join(path, sizeof path, p->state_dir, name) != 0)
        return FF802_AUTO_MODE_INVALID;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? FF802_AUTO_MODE_OFF : FF802_AUTO_MODE_INVALID;
    ssize_t r = read(fd, buf, sizeof buf - 1u);
    close(fd);
    if (r < 0)
        return FF802_AUTO_MODE_INVALID;
    buf[r] = '\0';
    if (strcmp(buf, "on") == 0 || strcmp(buf, "on\n") == 0)
        return FF802_AUTO_MODE_ON;
    if (strcmp(buf, "off") == 0 || strcmp(buf, "off\n") == 0)
        return FF802_AUTO_MODE_OFF;
    return FF802_AUTO_MODE_INVALID;
}

ff802_auto_mode ff802_auto_read_mode(const ff802_auto_paths *p)
{
    return read_onoff(p, "mode");
}

ff802_auto_mode ff802_auto_read_mixer_mode(const ff802_auto_paths *p)
{
    return read_onoff(p, "mixer-init");
}

const char *ff802_auto_mode_str(ff802_auto_mode m)
{
    switch (m) {
    case FF802_AUTO_MODE_ON:      return "on";
    case FF802_AUTO_MODE_OFF:     return "off";
    case FF802_AUTO_MODE_INVALID: break;
    }
    return "invalid (treated as off)";
}

static int ensure_dir(const char *dir)
{
    if (mkdir(dir, 0755) == 0 || errno == EEXIST) {
        struct stat st;
        if (lstat(dir, &st) == 0 && S_ISDIR(st.st_mode))
            return 0;
    }
    return -1;
}

int ff802_auto_lock(const ff802_auto_paths *p)
{
    char path[FF802_AUTO_PATH_MAX + 16u];

    if (ensure_dir(p->run_dir) != 0 ||
        join(path, sizeof path, p->run_dir, "lock") != 0)
        return -1;
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return -1;
    while (flock(fd, LOCK_EX) != 0) {
        if (errno != EINTR) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

int ff802_auto_lock_existing(const ff802_auto_paths *p)
{
    char path[FF802_AUTO_PATH_MAX + 16u];

    if (join(path, sizeof path, p->run_dir, "lock") != 0)
        return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? -2 : -1;
    while (flock(fd, LOCK_EX) != 0) {
        if (errno != EINTR) {
            close(fd);
            return -1;
        }
    }
    return fd;
}

void ff802_auto_unlock(int fd)
{
    if (fd >= 0) {
        (void)flock(fd, LOCK_UN);
        close(fd);
    }
}

int ff802_auto_claim_connection(const ff802_auto_paths *p,
                                const ff802_sysfs_dev *d)
{
    char dir[FF802_AUTO_PATH_MAX + 16u];
    char name[FF802_SYSFS_NAME_MAX + 16u];
    char path[sizeof dir + sizeof name + 2u];

    if (join(dir, sizeof dir, p->run_dir, "attempts") != 0 ||
        ensure_dir(p->run_dir) != 0 || ensure_dir(dir) != 0)
        return -1;
    int n = snprintf(name, sizeof name, "%s_%u_%u", d->kernel_name,
                     (unsigned)d->bus, (unsigned)d->devnum);
    if (n < 0 || (size_t)n >= sizeof name || join(path, sizeof path, dir, name) != 0)
        return -1;
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return errno == EEXIST ? 0 : -1;
    char line[64];
    n = snprintf(line, sizeof line, "%lld\n", (long long)time(NULL));
    if (n > 0)
        (void)!write(fd, line, (size_t)n);
    close(fd);
    return 1;
}

/* Write content to <run_dir>/<name> atomically. */
static int write_atomic(const char *dir, const char *name, const char *content)
{
    char path[FF802_AUTO_PATH_MAX + 32u];
    char tmp[FF802_AUTO_PATH_MAX + 40u];

    if (ensure_dir(dir) != 0 || join(path, sizeof path, dir, name) != 0)
        return -1;
    int n = snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof tmp)
        return -1;
    (void)unlink(tmp);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0)
        return -1;
    size_t len = strlen(content);
    ssize_t w = write(fd, content, len);
    if (w < 0 || (size_t)w != len || fsync(fd) != 0) {
        close(fd);
        (void)unlink(tmp);
        return -1;
    }
    close(fd);
    if (rename(tmp, path) != 0) {
        (void)unlink(tmp);
        return -1;
    }
    return 0;
}

static void sanitize(char *dst, size_t size, const char *src)
{
    size_t i = 0;
    if (size == 0)
        return;
    for (; src != NULL && src[i] != '\0' && i + 1u < size; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20u || c > 0x7Eu) ? '?' : (char)c;
    }
    dst[i] = '\0';
}

const char *ff802_auto_event_kind_str(ff802_auto_event_kind k)
{
    switch (k) {
    case FF802_EVENT_PREPARED:  return "prepared";
    case FF802_EVENT_CC_ACTIVE: return "cc-active";
    case FF802_EVENT_FAILED:    return "failed";
    case FF802_EVENT_MIXER_FAILED: return "mixer-failed";
    }
    return "failed";
}

static long read_seq(const char *run_dir)
{
    char path[FF802_AUTO_PATH_MAX + 16u];
    char buf[32];

    if (join(path, sizeof path, run_dir, "seq") != 0)
        return -1;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    ssize_t r = read(fd, buf, sizeof buf - 1u);
    close(fd);
    if (r <= 0)
        return 0;
    buf[r] = '\0';
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (v < 0 || end == buf)
        return 0;
    return v;
}

static long write_event_file(const ff802_auto_paths *p, const char *file,
                             ff802_auto_event_kind kind,
                             const ff802_sysfs_dev *d, const char *detail)
{
    char clean[160];
    char buf[512];

    long seq = read_seq(p->run_dir);
    if (seq < 0)
        return -1;
    seq++;
    sanitize(clean, sizeof clean, detail);
    int n = snprintf(buf, sizeof buf, "%ld\n", seq);
    if (n < 0 || (size_t)n >= sizeof buf || write_atomic(p->run_dir, "seq", buf) != 0)
        return -1;
    n = snprintf(buf, sizeof buf,
                 "seq=%ld\nkind=%s\ntime=%lld\nport=%s\nbusnum=%u\ndevnum=%u\ndetail=%s\n",
                 seq, ff802_auto_event_kind_str(kind), (long long)time(NULL),
                 d->kernel_name, (unsigned)d->bus, (unsigned)d->devnum, clean);
    if (n < 0 || (size_t)n >= sizeof buf || write_atomic(p->run_dir, file, buf) != 0)
        return -1;
    return seq;
}

long ff802_auto_write_event(const ff802_auto_paths *p,
                            ff802_auto_event_kind kind,
                            const ff802_sysfs_dev *d, const char *detail)
{
    return write_event_file(p, "event", kind, d, detail);
}

long ff802_auto_write_mixer_event(const ff802_auto_paths *p,
                                  const ff802_sysfs_dev *d, const char *detail)
{
    return write_event_file(p, "mixer-event", FF802_EVENT_MIXER_FAILED, d, detail);
}

int ff802_auto_write_mixer_result(const ff802_auto_paths *p,
                                  const ff802_sysfs_dev *d, const char *outcome,
                                  unsigned written, unsigned total, int card,
                                  int client, int port, const char *port_name,
                                  const char *detail)
{
    char clean[200];
    char pname[80];
    char oc[32];
    char buf[640];

    sanitize(clean, sizeof clean, detail);
    sanitize(pname, sizeof pname, port_name);
    sanitize(oc, sizeof oc, outcome);
    int n = snprintf(buf, sizeof buf,
                     "time=%lld\noutcome=%s\nport=%s\nbusnum=%u\ndevnum=%u\n"
                     "written=%u/%u\ncard=%d\nmidi_port=%d:%d %s\ndetail=%s\n",
                     (long long)time(NULL), oc, d->kernel_name, (unsigned)d->bus,
                     (unsigned)d->devnum, written, total, card, client, port,
                     pname, clean);
    if (n < 0 || (size_t)n >= sizeof buf)
        return -1;
    return write_atomic(p->run_dir, "mixer-result", buf);
}

int ff802_auto_set_pending(const ff802_auto_paths *p, const ff802_sysfs_dev *d)
{
    char buf[128];
    int n = snprintf(buf, sizeof buf, "port=%s\nbusnum=%u\ndevnum=%u\n",
                     d->kernel_name, (unsigned)d->bus, (unsigned)d->devnum);
    if (n < 0 || (size_t)n >= sizeof buf)
        return -1;
    return write_atomic(p->run_dir, "pending", buf);
}

int ff802_auto_pending_for(const ff802_auto_paths *p, const char *kernel_name)
{
    char path[FF802_AUTO_PATH_MAX + 16u];
    char buf[128];
    char want[FF802_SYSFS_NAME_MAX + 8u];

    if (join(path, sizeof path, p->run_dir, "pending") != 0)
        return 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return 0;
    ssize_t r = read(fd, buf, sizeof buf - 1u);
    close(fd);
    if (r <= 0)
        return 0;
    buf[r] = '\0';
    int n = snprintf(want, sizeof want, "port=%s\n", kernel_name);
    if (n < 0 || (size_t)n >= sizeof want)
        return 0;
    return strncmp(buf, want, strlen(want)) == 0;
}

int ff802_auto_clear_pending(const ff802_auto_paths *p)
{
    char path[FF802_AUTO_PATH_MAX + 16u];
    if (join(path, sizeof path, p->run_dir, "pending") != 0)
        return -1;
    return (unlink(path) == 0 || errno == ENOENT) ? 0 : -1;
}

int ff802_auto_write_result(const ff802_auto_paths *p, const char *outcome,
                            const char *kernel_name, const char *detail)
{
    char clean[160];
    char port[FF802_SYSFS_NAME_MAX];
    char buf[320];

    sanitize(clean, sizeof clean, detail);
    sanitize(port, sizeof port, kernel_name);
    int n = snprintf(buf, sizeof buf, "time=%lld\noutcome=%s\nport=%s\ndetail=%s\n",
                     (long long)time(NULL), outcome, port, clean);
    if (n < 0 || (size_t)n >= sizeof buf)
        return -1;
    return write_atomic(p->run_dir, "last-result", buf);
}
