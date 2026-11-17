/*
 * PipeWire device reservation for ff802-chanmap.
 *
 * pw-reserve -n Audio<N> -r asks the current owner (WirePlumber) to release
 * the card over the D-Bus ReserveDevice1 protocol and holds the reservation
 * until it receives SIGINT. WirePlumber then acquires the card again.
 *
 * The child runs in its own process group, so a Ctrl+C in the terminal
 * reaches only ff802-chanmap, which first stops the audio and then ends the
 * reservation. PR_SET_PDEATHSIG ends the reservation if ff802-chanmap dies.
 */
#define _GNU_SOURCE
#include "ff802/chanmap.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void sleep_ms(unsigned ms)
{
    struct timespec ts = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    while (nanosleep(&ts, &ts) < 0 && errno == EINTR) {
    }
}

int chanmap_reserve_start(struct chanmap_reserve *r, const char *prog,
                          int card, const char *logfile)
{
    char name[32];
    pid_t parent = getpid();
    pid_t pid;

    r->pid = 0;
    r->exit_status = 0;
    if (card < 0 || card > 255)
        return -EINVAL;
    snprintf(name, sizeof name, "Audio%d", card);

    pid = fork();
    if (pid < 0)
        return -errno;
    if (pid == 0) {
        int fd;
        setpgid(0, 0);
        prctl(PR_SET_PDEATHSIG, SIGINT);
        if (getppid() != parent)
            _exit(126);
        fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) {
            dup2(fd, 0);
            close(fd);
        }
        fd = open(logfile ? logfile : "/dev/null", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, 1);
            dup2(fd, 2);
            close(fd);
        }
        execlp(prog, prog, "-n", name, "-r", (char *)NULL);
        _exit(127);
    }
    setpgid(pid, pid); /* also from the parent, avoids a race */
    r->pid = pid;
    return 0;
}

int chanmap_reserve_alive(struct chanmap_reserve *r)
{
    int st;
    pid_t w;

    if (r->pid <= 0)
        return 0;
    w = waitpid(r->pid, &st, WNOHANG);
    if (w == 0)
        return 1;
    if (w == r->pid)
        r->exit_status = st;
    r->pid = 0;
    return 0;
}

int chanmap_reserve_stop(struct chanmap_reserve *r, unsigned timeout_ms)
{
    unsigned waited = 0;
    int st;

    if (r->pid <= 0)
        return r->exit_status;
    kill(r->pid, SIGINT);
    while (waited < timeout_ms) {
        pid_t w = waitpid(r->pid, &st, WNOHANG);
        if (w == r->pid) {
            r->pid = 0;
            r->exit_status = st;
            return st;
        }
        if (w < 0) {
            r->pid = 0;
            return -1;
        }
        sleep_ms(10);
        waited += 10;
    }
    kill(r->pid, SIGKILL);
    if (waitpid(r->pid, &st, 0) == r->pid)
        r->exit_status = st;
    r->pid = 0;
    return r->exit_status;
}

enum chanmap_status chanmap_open_with_reserve(const struct chanmap_backend *be,
                                              struct chanmap_reserve *r,
                                              unsigned timeout_ms,
                                              volatile sig_atomic_t *abort_flag)
{
    unsigned waited = 0;

    for (;;) {
        int rc = be->open(be->ctx);
        if (rc == 0)
            return CHANMAP_OK;
        if (rc != -EBUSY)
            return CHANMAP_ERR_OPEN;
        if (r && !chanmap_reserve_alive(r))
            return CHANMAP_ERR_RESERVE;
        if (abort_flag && *abort_flag)
            return CHANMAP_ABORT_SIGNAL;
        if (waited >= timeout_ms)
            return CHANMAP_ERR_BUSY;
        sleep_ms(100);
        waited += 100;
    }
}
