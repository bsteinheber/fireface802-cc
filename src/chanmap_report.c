/*
 * Cable positions, result files and the comparison with the UCM profile.
 *
 * Expected assignment (audio/ucm2/USB-Audio/RME/Fireface-802.conf):
 *   out1..out8  playback 0..7      one channel each, polarity +
 *   hp1         playback 8 (L, +), 9 (R, -)   TRS into a balanced input
 *   hp2         playback 10 (L, +), 11 (R, -)
 *   Line In 1/2 capture 0/1
 * A balanced input sees tip minus ring, so the right channel of a
 * headphone output arrives with inverted polarity. That tells L from R.
 */
#include "ff802/chanmap.h"

#include <stdlib.h>
#include <string.h>

#define RESULT_MAGIC "ff802-chanmap-result 1"

static const char *const positions[] = {
    "out1", "out2", "out3", "out4", "out5", "out6", "out7", "out8", "hp1", "hp2"
};
#define N_POSITIONS (sizeof positions / sizeof positions[0])

int chanmap_position_valid(const char *name)
{
    for (size_t i = 0; i < N_POSITIONS; i++)
        if (strcmp(name, positions[i]) == 0)
            return 1;
    return 0;
}

unsigned chanmap_position_expect(const char *name, unsigned play[2], int polarity[2])
{
    if (strncmp(name, "out", 3) == 0 && name[3] >= '1' && name[3] <= '8' && name[4] == '\0') {
        play[0] = (unsigned)(name[3] - '1');
        polarity[0] = 1;
        return 1;
    }
    if (strcmp(name, "hp1") == 0 || strcmp(name, "hp2") == 0) {
        unsigned base = name[2] == '1' ? 8u : 10u;
        play[0] = base;
        polarity[0] = 1;
        play[1] = base + 1u;
        polarity[1] = -1;
        return 2;
    }
    return 0;
}

int chanmap_write_result(FILE *f, const char *position, unsigned input,
                         const char *card_id, const struct chanmap_result *res)
{
    fprintf(f, "%s\n", RESULT_MAGIC);
    fprintf(f, "position\t%s\n", position);
    fprintf(f, "input\t%u\n", input);
    fprintf(f, "card\t%s\n", card_id);
    fprintf(f, "status\t%d\t%s\n", (int)res->status, chanmap_status_text(res->status));
    fprintf(f, "detail\t%s\n", res->detail);
    fprintf(f, "steps\t%u\n", res->steps_done);
    fprintf(f, "max_output_dbfs\t%.2f\n", chanmap_dbfs(res->max_output_peak));
    fprintf(f, "max_input_dbfs\t%.2f\n", chanmap_dbfs(res->max_input_peak));
    fprintf(f, "mixer_checked\t%d\n", res->mixer_checked);
    fprintf(f, "# hit play capture level_dbfs snr_db latency_ms polarity\n");
    for (unsigned i = 0; i < res->n_hits; i++) {
        const struct chanmap_hit *h = &res->hits[i];
        fprintf(f, "hit\t%u\t%u\t%.2f\t%.2f\t%.3f\t%+d\n", h->play, h->capture,
                h->level_dbfs, h->snr_db, h->latency_ms, h->polarity);
    }
    fprintf(f, "end\n");
    return ferror(f) ? -1 : 0;
}

int chanmap_read_result(FILE *f, struct chanmap_pos_file *p)
{
    char line[512];
    int have_end = 0, have_status = 0;

    memset(p, 0, sizeof *p);
    if (!fgets(line, sizeof line, f) || strncmp(line, RESULT_MAGIC, strlen(RESULT_MAGIC)) != 0)
        return -1;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = '\0';
        if (strncmp(line, "position\t", 9) == 0) {
            size_t l = strlen(line + 9);
            if (l >= sizeof p->position)
                return -1; /* too long: not a valid position */
            memcpy(p->position, line + 9, l + 1);
        } else if (strncmp(line, "input\t", 6) == 0) {
            p->input = (unsigned)strtoul(line + 6, NULL, 10);
        } else if (strncmp(line, "status\t", 7) == 0) {
            p->status = (enum chanmap_status)strtol(line + 7, NULL, 10);
            have_status = 1;
        } else if (strncmp(line, "hit\t", 4) == 0 && p->n_hits < CHANMAP_MAX_HITS) {
            struct chanmap_hit *h = &p->hits[p->n_hits];
            if (sscanf(line + 4, "%u\t%u\t%lf\t%lf\t%lf\t%d", &h->play, &h->capture,
                       &h->level_dbfs, &h->snr_db, &h->latency_ms, &h->polarity) == 6)
                p->n_hits++;
            else
                return -1;
        } else if (strcmp(line, "end") == 0) {
            have_end = 1;
            break;
        }
    }
    if (!have_end || !have_status || !chanmap_position_valid(p->position) ||
        (p->input != 1 && p->input != 2))
        return -1;
    return 0;
}

/* Last file for (position, input), or NULL. */
static const struct chanmap_pos_file *find(const struct chanmap_pos_file *files, size_t n,
                                           const char *pos, unsigned input)
{
    const struct chanmap_pos_file *r = NULL;
    for (size_t i = 0; i < n; i++)
        if (strcmp(files[i].position, pos) == 0 && files[i].input == input)
            r = &files[i];
    return r;
}

static void describe(const struct chanmap_pos_file *p, char *buf, size_t len)
{
    size_t o = 0;
    buf[0] = '\0';
    if (p->n_hits == 0)
        snprintf(buf, len, "nothing");
    for (unsigned i = 0; i < p->n_hits && o < len; i++)
        o += (size_t)snprintf(buf + o, len - o, "%sP%u->C%u%c", i ? " " : "",
                              p->hits[i].play, p->hits[i].capture,
                              p->hits[i].polarity > 0 ? '+' : '-');
}

/* 0 ok, 1 mismatch, 2 not usable. note receives remarks. */
static int check_pos(const struct chanmap_pos_file *p, const char *name, unsigned input,
                     char *note, size_t len)
{
    unsigned play[2];
    int pol[2];
    unsigned n = chanmap_position_expect(name, play, pol);
    unsigned capture = input - 1u;
    int seen[2] = { 0, 0 };
    int rc = 0;

    note[0] = '\0';
    if (p->status != CHANMAP_OK) {
        snprintf(note, len, "measurement not valid (%s)", chanmap_status_text(p->status));
        return 2;
    }
    for (unsigned i = 0; i < p->n_hits; i++) {
        const struct chanmap_hit *h = &p->hits[i];
        int match = -1;
        for (unsigned k = 0; k < n; k++)
            if (h->play == play[k])
                match = (int)k;
        if (match < 0 || h->capture != capture) {
            snprintf(note, len, "unexpected P%u->C%u", h->play, h->capture);
            return 1;
        }
        if (seen[match]) {
            snprintf(note, len, "P%u arrives on several channels", h->play);
            return 1;
        }
        seen[match] = 1;
        if (h->polarity != pol[match]) {
            if (n == 2) {
                snprintf(note, len, "left/right swapped (P%u polarity %+d)", h->play, h->polarity);
                rc = 1;
            } else {
                snprintf(note, len, "polarity inverted (cable?)");
            }
        }
    }
    for (unsigned k = 0; k < n; k++) {
        if (!seen[k]) {
            snprintf(note, len, "P%u not detected", play[k]);
            return 1;
        }
    }
    return rc;
}

enum chanmap_verdict chanmap_report(const struct chanmap_pos_file *files, size_t n, FILE *out)
{
    int mismatch = 0, incomplete = 0;
    int input2_done = 0;
    char got[256], note[128], exp[64];

    fprintf(out, "%-6s %-5s %-18s %-30s %s\n", "cable", "input", "expected", "measured", "result");
    for (size_t i = 0; i < N_POSITIONS; i++) {
        const struct chanmap_pos_file *p = find(files, n, positions[i], 1);
        unsigned play[2];
        int pol[2];
        unsigned k = chanmap_position_expect(positions[i], play, pol);
        int rc;

        if (k == 1)
            snprintf(exp, sizeof exp, "P%u->C0+", play[0]);
        else
            snprintf(exp, sizeof exp, "P%u->C0+ P%u->C0-", play[0], play[1]);
        if (!p) {
            fprintf(out, "%-6s %-5u %-18s %-30s %s\n", positions[i], 1u, exp, "-", "MISSING");
            incomplete = 1;
            continue;
        }
        describe(p, got, sizeof got);
        rc = check_pos(p, positions[i], 1, note, sizeof note);
        fprintf(out, "%-6s %-5u %-18s %-30s %s%s%s\n", positions[i], 1u, exp, got,
                rc == 0 ? "OK" : rc == 1 ? "MISMATCH" : "INVALID",
                note[0] ? ": " : "", note);
        if (rc == 1)
            mismatch = 1;
        else if (rc == 2)
            incomplete = 1;
    }
    /* Line In 2: any position measured with the cable on input 2. */
    for (size_t i = 0; i < N_POSITIONS; i++) {
        const struct chanmap_pos_file *p = find(files, n, positions[i], 2);
        int rc;
        if (!p)
            continue;
        describe(p, got, sizeof got);
        rc = check_pos(p, positions[i], 2, note, sizeof note);
        fprintf(out, "%-6s %-5u %-18s %-30s %s%s%s\n", positions[i], 2u, "capture C1", got,
                rc == 0 ? "OK" : rc == 1 ? "MISMATCH" : "INVALID",
                note[0] ? ": " : "", note);
        if (rc == 1)
            mismatch = 1;
        else if (rc == 2)
            incomplete = 1;
        else
            input2_done = 1;
    }
    if (!input2_done) {
        fprintf(out, "%-6s %-5u %-18s %-30s %s\n", "any", 2u, "capture C1", "-", "MISSING");
        incomplete = 1;
    }

    if (mismatch) {
        fprintf(out, "verdict: MISMATCH - the profile does not match the device\n");
        return CHANMAP_MISMATCH;
    }
    if (incomplete) {
        fprintf(out, "verdict: INCOMPLETE - measure the missing positions\n");
        return CHANMAP_INCOMPLETE;
    }
    fprintf(out, "verdict: VERIFIED - all channel pairs of the profile confirmed\n");
    return CHANMAP_VERIFIED;
}
