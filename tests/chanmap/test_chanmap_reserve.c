/*
 * Control of the pw-reserve child process, with a fake pw-reserve:
 * arguments, own process group, release on SIGINT, SIGKILL fallback,
 * release when ff802-chanmap dies, failing program.
 *
 * Usage: test_chanmap_reserve <fake-pw-reserve.sh> <scratch dir>
 */
#define _GNU_SOURCE
#include "../test_common.h"
#include "ff802/chanmap.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char dir[512];

static int exists(const char *name)
{
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    return access(p, F_OK) == 0;
}

static int wait_for(const char *name, int ms)
{
    struct timespec ts = { 0, 10000000L };
    for (int i = 0; i < ms / 10; i++) {
        if (exists(name))
            return 1;
        nanosleep(&ts, NULL);
    }
    return exists(name);
}

static void slurp(const char *name, char *buf, size_t len)
{
    char p[1024];
    FILE *f;
    buf[0] = '\0';
    snprintf(p, sizeof p, "%s/%s", dir, name);
    f = fopen(p, "r");
    if (f) {
        if (!fgets(buf, (int)len, f))
            buf[0] = '\0';
        fclose(f);
    }
    buf[strcspn(buf, "\n")] = '\0';
}

static void fresh(const char *base, const char *sub)
{
    char cmd[1200];
    snprintf(dir, sizeof dir, "%s/%s", base, sub);
    snprintf(cmd, sizeof cmd, "rm -rf -- '%s' && mkdir -p -- '%s'", dir, dir);
    if (system(cmd) != 0)
        exit(2);
    setenv("FAKE_RESERVE_DIR", dir, 1);
}

int main(int argc, char **argv)
{
    struct chanmap_reserve r;
    char buf[256], pg[32], logf[1024];
    const char *fake, *base;
    int st;

    if (argc < 3) {
        fprintf(stderr, "usage: %s <fake-pw-reserve> <scratch>\n", argv[0]);
        return 2;
    }
    fake = argv[1];
    base = argv[2];
    mkdir(base, 0755);

    /* normal: start, hold, release on stop */
    fresh(base, "normal");
    snprintf(logf, sizeof logf, "%s/log", dir);
    CHECK(chanmap_reserve_start(&r, fake, 1, logf) == 0);
    CHECK(r.pid > 0);
    CHECK(wait_for("acquired", 3000));
    CHECK(chanmap_reserve_alive(&r) == 1);
    slurp("args", buf, sizeof buf);
    CHECK(strcmp(buf, "-n Audio1 -r") == 0);
    /* own process group: a Ctrl+C in the terminal does not reach it */
    slurp("pgid", pg, sizeof pg);
    CHECK(atoi(pg) == (int)r.pid);
    CHECK(atoi(pg) != (int)getpgrp());
    st = chanmap_reserve_stop(&r, 3000);
    CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    CHECK(exists("released"));
    CHECK(r.pid == 0);
    CHECK(chanmap_reserve_alive(&r) == 0);
    /* stopping twice is harmless */
    chanmap_reserve_stop(&r, 100);

    /* card number goes into the reservation name */
    fresh(base, "card7");
    CHECK(chanmap_reserve_start(&r, fake, 7, NULL) == 0);
    CHECK(wait_for("acquired", 3000));
    slurp("args", buf, sizeof buf);
    CHECK(strcmp(buf, "-n Audio7 -r") == 0);
    chanmap_reserve_stop(&r, 3000);
    CHECK(chanmap_reserve_start(&r, fake, 256, NULL) < 0);
    CHECK(chanmap_reserve_start(&r, fake, -1, NULL) < 0);

    /* child ignores SIGINT: SIGKILL after the timeout */
    fresh(base, "stubborn");
    setenv("FAKE_RESERVE_IGNORE_INT", "1", 1);
    CHECK(chanmap_reserve_start(&r, fake, 1, NULL) == 0);
    CHECK(wait_for("acquired", 3000));
    st = chanmap_reserve_stop(&r, 300);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    CHECK(r.pid == 0);
    unsetenv("FAKE_RESERVE_IGNORE_INT");

    /* ff802-chanmap dies: the reservation ends (PR_SET_PDEATHSIG) */
    fresh(base, "parentdies");
    {
        pid_t p = fork();
        if (p == 0) {
            struct chanmap_reserve rr;
            if (chanmap_reserve_start(&rr, fake, 1, NULL) != 0)
                _exit(1);
            wait_for("acquired", 3000);
            _exit(0); /* without stopping it */
        }
        waitpid(p, &st, 0);
        CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
        CHECK(wait_for("released", 3000));
    }

    /* program missing: exits 127, not alive */
    fresh(base, "missing");
    CHECK(chanmap_reserve_start(&r, "/nonexistent/pw-reserve", 1, NULL) == 0);
    {
        struct timespec ts = { 0, 200000000L };
        nanosleep(&ts, NULL);
    }
    CHECK(chanmap_reserve_alive(&r) == 0);
    CHECK(WIFEXITED(r.exit_status) && WEXITSTATUS(r.exit_status) == 127);

    TEST_EXIT();
}
