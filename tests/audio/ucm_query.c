/*
 * Minimal, read-only stand-in for alsaucm, built on the public alsa-lib
 * use case API. It supports exactly the queries of the host test T1 and
 * reproduces the output format of alsaucm from alsa-utils 1.2.15.2
 * (alsaucm/usecase.c and the text renderer in alsaucm/dump.c), so the T1
 * script can run unchanged in the container together with the fake control
 * device fake_ctl_ff802.c:
 *
 *   ucm_query -c <card> list _verbs
 *   ucm_query -c <card> list _devices/<verb>
 *   ucm_query -c <card> dump text
 *
 * One additional command, not offered by alsaucm:
 *
 *   ucm_query -c <card> alibcfg     print the private alsa-lib configuration
 *                                   (_alibcfg) and check that it parses
 *
 * It never sets a verb or device and never opens a PCM.
 */
#include <alsa/asoundlib.h>
#include <alsa/use-case.h>

#include <stdio.h>
#include <string.h>

static const char *prog = "ucm_query";

/* Quoting as done by tesc() in alsaucm/dump.c. */
static const char *esc(const char *s, char *buf, size_t len)
{
    char *dst = buf;
    char c = '\0';

    if (strchr(s, '"') || strchr(s, ' ') || strchr(s, '.')) {
        *dst++ = c = '"';
        len--;
    }
    while (*s && len > 2) {
        if (*s == '"') {
            if (len > 3) {
                *dst++ = '\\';
                *dst++ = *s++;
                len -= 2;
                continue;
            }
            break;
        }
        *dst++ = *s++;
    }
    if (c)
        *dst++ = c;
    *dst = '\0';
    return buf;
}

static int list(snd_use_case_mgr_t *mgr, const char *what)
{
    const char **l;
    int n, i;

    n = snd_use_case_get_list(mgr, what, &l);
    if (n < 0) {
        fprintf(stderr, "%s: error failed to get list %s: %s\n", prog, what,
                snd_strerror(n));
        return 1;
    }
    if (n == 0) {
        printf("  list is empty\n");
        return 0;
    }
    for (i = 0; i < n / 2; i++) {
        printf("  %i: %s\n", i, l[i * 2]);
        if (l[i * 2 + 1])
            printf("    %s\n", l[i * 2 + 1]);
    }
    snd_use_case_free_list(l, n);
    return 0;
}

static int dump_values(snd_use_case_mgr_t *mgr, const char *dev, const char *verb)
{
    char q[512], b1[256], b2[256];
    const char **ids;
    int n, i;

    snprintf(q, sizeof q, "_identifiers/%s/%s", dev, verb);
    n = snd_use_case_get_list(mgr, q, &ids);
    if (n < 0) {
        fprintf(stderr, "%s: unable to get _identifiers for verb '%s' for device '%s': %s\n",
                prog, verb, dev, snd_strerror(n));
        return 1;
    }
    if (n == 0)
        return 0;
    printf("\t\tValues {\n");
    for (i = 0; i < n; i++) {
        const char *v = NULL;
        int err;

        snprintf(q, sizeof q, "%s/%s/%s", ids[i], dev, verb);
        err = snd_use_case_get(mgr, q, &v);
        if (err < 0) {
            fprintf(stderr, "%s: unable to get value '%s' for verb '%s' for device '%s': %s\n",
                    prog, ids[i], verb, dev, snd_strerror(err));
            snd_use_case_free_list(ids, n);
            return 1;
        }
        printf("\t\t\t%s %s\n", esc(ids[i], b1, sizeof b1), esc(v, b2, sizeof b2));
        free((void *)v);
    }
    printf("\t\t}\n");
    snd_use_case_free_list(ids, n);
    return 0;
}

static int dump(snd_use_case_mgr_t *mgr)
{
    const char **verbs, **devs;
    char q[256], b1[256], b2[256];
    int nv, nd, i, j;

    nv = snd_use_case_get_list(mgr, "_verbs", &verbs);
    if (nv < 0) {
        fprintf(stderr, "%s: unable to get verbs: %s\n", prog, snd_strerror(nv));
        return 1;
    }
    for (i = 0; i < nv / 2; i++) {
        const char *verb = verbs[i * 2], *comment = verbs[i * 2 + 1];

        printf("Verb.%s {\n", esc(verb, b1, sizeof b1));
        if (comment && comment[0])
            printf("\tComment %s\n", esc(comment, b2, sizeof b2));
        snprintf(q, sizeof q, "_devices/%s", verb);
        nd = snd_use_case_get_list(mgr, q, &devs);
        if (nd < 0) {
            fprintf(stderr, "%s: unable to get devices: %s\n", prog, snd_strerror(nd));
            snd_use_case_free_list(verbs, nv);
            return 1;
        }
        for (j = 0; j < nd / 2; j++) {
            const char *dev = devs[j * 2], *dc = devs[j * 2 + 1];

            printf("\tDevice.%s {\n", esc(dev, b1, sizeof b1));
            if (dc && dc[0])
                printf("\t\tComment %s\n", esc(dc, b2, sizeof b2));
            if (dump_values(mgr, dev, verb)) {
                snd_use_case_free_list(devs, nd);
                snd_use_case_free_list(verbs, nv);
                return 1;
            }
            printf("\t}\n");
        }
        snd_use_case_free_list(devs, nd);
        printf("}\n");
    }
    snd_use_case_free_list(verbs, nv);
    return 0;
}

static int alibcfg(snd_use_case_mgr_t *mgr)
{
    const char *cfg = NULL;
    snd_config_t *top;
    snd_input_t *in;
    int err;

    err = snd_use_case_get(mgr, "_alibcfg", &cfg);
    if (err < 0 || cfg == NULL) {
        fprintf(stderr, "%s: no _alibcfg: %s\n", prog, snd_strerror(err));
        return 1;
    }
    fputs(cfg, stdout);
    err = snd_config_top(&top);
    if (err == 0) {
        err = snd_input_buffer_open(&in, cfg, (ssize_t)strlen(cfg));
        if (err == 0) {
            err = snd_config_load(top, in);
            snd_input_close(in);
        }
        snd_config_delete(top);
    }
    free((void *)cfg);
    if (err < 0) {
        fprintf(stderr, "%s: _alibcfg does not parse: %s\n", prog, snd_strerror(err));
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    snd_use_case_mgr_t *mgr;
    int err, rc;

    if (argc < 4 || strcmp(argv[1], "-c") != 0) {
        fprintf(stderr, "usage: %s -c <card> list <what> | dump text | alibcfg\n", argv[0]);
        return 2;
    }
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "set") || !strcmp(argv[i], "reset") ||
            !strcmp(argv[i], "_boot") || !strcmp(argv[i], "_fboot")) {
            fprintf(stderr, "%s: '%s' is not supported (read-only tool)\n", prog, argv[i]);
            return 2;
        }
    }

    err = snd_use_case_mgr_open(&mgr, argv[2]);
    if (err < 0) {
        fprintf(stderr, "%s: error failed to open sound card %s: %s\n", prog, argv[2],
                snd_strerror(err));
        return 1;
    }

    if (!strcmp(argv[3], "list") && argc == 5)
        rc = list(mgr, argv[4]);
    else if (!strcmp(argv[3], "dump") && argc == 5 && !strcmp(argv[4], "text"))
        rc = dump(mgr);
    else if (!strcmp(argv[3], "alibcfg") && argc == 4)
        rc = alibcfg(mgr);
    else {
        fprintf(stderr, "%s: unsupported command\n", prog);
        rc = 2;
    }

    snd_use_case_mgr_close(mgr);
    return rc;
}
