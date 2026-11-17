/*
 * Result files and the comparison with the profile. Also writes three
 * result directories (verified, mismatch, incomplete) below the scratch
 * directory, which the CLI tests of 'ff802-chanmap report' read.
 *
 * Usage: test_chanmap_report <scratch dir>
 */
#include "../test_common.h"
#include "ff802/chanmap.h"

#include <stdlib.h>
#include <sys/stat.h>

#define MAXF 16

static struct chanmap_pos_file files[MAXF];
static size_t nfiles;

/* A measured result as the device would give it if the profile is right. */
static void ideal(const char *pos, unsigned input, struct chanmap_result *r)
{
    unsigned play[2];
    int pol[2];
    unsigned n = chanmap_position_expect(pos, play, pol);
    memset(r, 0, sizeof *r);
    r->status = CHANMAP_OK;
    r->steps_done = CHANMAP_TEST_PLAY;
    for (unsigned k = 0; k < n; k++) {
        struct chanmap_hit *h = &r->hits[r->n_hits++];
        h->play = play[k];
        h->capture = input - 1u;
        h->level_dbfs = -40.1;
        h->snr_db = 75.0;
        h->latency_ms = 91.9;
        h->polarity = pol[k];
    }
}

/* Write through a file and read it back (round trip). */
static void add(const char *pos, unsigned input, const struct chanmap_result *r)
{
    FILE *f = tmpfile();
    CHECK(f != NULL);
    if (!f)
        return;
    CHECK(chanmap_write_result(f, pos, input, "F80224047128", r) == 0);
    rewind(f);
    CHECK(chanmap_read_result(f, &files[nfiles]) == 0);
    fclose(f);
    nfiles++;
}

static void store(const char *dir, const char *pos, unsigned input, const struct chanmap_result *r)
{
    char path[1024];
    FILE *f;
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/%s-in%u.txt", dir, pos, input);
    f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
        chanmap_write_result(f, pos, input, "F80224047128", r);
        fclose(f);
    }
}

static const char *all[] = { "out1", "out2", "out3", "out4", "out5", "out6",
                             "out7", "out8", "hp1", "hp2" };

static enum chanmap_verdict verdict(void)
{
    FILE *sink = fopen("/dev/null", "w");
    enum chanmap_verdict v = chanmap_report(files, nfiles, sink ? sink : stderr);
    if (sink)
        fclose(sink);
    return v;
}

static void full_set(void)
{
    struct chanmap_result r;
    nfiles = 0;
    for (size_t i = 0; i < 10; i++) {
        ideal(all[i], 1, &r);
        add(all[i], 1, &r);
    }
    ideal("out1", 2, &r);
    add("out1", 2, &r);
}

int main(int argc, char **argv)
{
    struct chanmap_result r;
    const char *base = argc > 1 ? argv[1] : ".";
    char d[1024];
    unsigned play[2];
    int pol[2];
    FILE *f;
    struct chanmap_pos_file p;

    mkdir(base, 0755);

    /* positions */
    CHECK(chanmap_position_valid("out1") && chanmap_position_valid("out8"));
    CHECK(chanmap_position_valid("hp1") && chanmap_position_valid("hp2"));
    CHECK(!chanmap_position_valid("out9") && !chanmap_position_valid("out0"));
    CHECK(!chanmap_position_valid("hp3") && !chanmap_position_valid("") &&
          !chanmap_position_valid("out1 "));
    CHECK(chanmap_position_expect("out3", play, pol) == 1 && play[0] == 2 && pol[0] == 1);
    CHECK(chanmap_position_expect("hp2", play, pol) == 2 && play[0] == 10 && play[1] == 11 &&
          pol[0] == 1 && pol[1] == -1);

    /* round trip keeps every field */
    nfiles = 0;
    ideal("hp1", 1, &r);
    add("hp1", 1, &r);
    CHECK(nfiles == 1 && strcmp(files[0].position, "hp1") == 0 && files[0].input == 1);
    CHECK(files[0].n_hits == 2 && files[0].hits[1].play == 9 && files[0].hits[1].polarity == -1);
    CHECK(files[0].status == CHANMAP_OK);

    /* broken files are rejected */
    f = tmpfile();
    if (f) {
        fputs("something else\n", f);
        rewind(f);
        CHECK(chanmap_read_result(f, &p) < 0);
        fclose(f);
    }
    f = tmpfile();
    if (f) {
        fputs("ff802-chanmap-result 1\nposition\tout1\ninput\t1\nstatus\t0\tok\n", f); /* no end */
        rewind(f);
        CHECK(chanmap_read_result(f, &p) < 0);
        fclose(f);
    }
    f = tmpfile();
    if (f) {
        fputs("ff802-chanmap-result 1\nposition\tout9\ninput\t1\nstatus\t0\tok\nend\n", f);
        rewind(f);
        CHECK(chanmap_read_result(f, &p) < 0);
        fclose(f);
    }

    /* everything as in the profile */
    full_set();
    CHECK(verdict() == CHANMAP_VERIFIED);

    /* a line output inverted by its cable is only a remark */
    full_set();
    files[4].hits[0].polarity = -1;
    CHECK(verdict() == CHANMAP_VERIFIED);

    /* headphones: left/right swapped */
    full_set();
    files[8].hits[0].polarity = -1;
    files[8].hits[1].polarity = 1;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* out3 carries playback 3 instead of 2 */
    full_set();
    files[2].hits[0].play = 3;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* signal arrives on an unexpected input channel */
    full_set();
    files[0].hits[0].capture = 4;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* additional hit (internal routing) */
    full_set();
    files[1].hits[files[1].n_hits] = files[1].hits[0];
    files[1].hits[files[1].n_hits].capture = 7;
    files[1].n_hits++;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* nothing arrived for hp2 */
    full_set();
    files[9].n_hits = 0;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* a position missing, input 2 missing, an aborted measurement */
    full_set();
    nfiles = 9; /* drop hp2 and the input 2 file */
    CHECK(verdict() == CHANMAP_INCOMPLETE);
    full_set();
    nfiles = 10;
    CHECK(verdict() == CHANMAP_INCOMPLETE);
    full_set();
    files[5].status = CHANMAP_ABORT_SIGNAL;
    CHECK(verdict() == CHANMAP_INCOMPLETE);

    /* a later file for the same position replaces the earlier one */
    full_set();
    files[nfiles] = files[2];
    files[nfiles].hits[0].play = 5;
    nfiles++;
    CHECK(verdict() == CHANMAP_MISMATCH);

    /* directories for the CLI tests */
    snprintf(d, sizeof d, "%s/verified", base);
    for (size_t i = 0; i < 10; i++) {
        ideal(all[i], 1, &r);
        store(d, all[i], 1, &r);
    }
    ideal("out1", 2, &r);
    store(d, "out1", 2, &r);

    snprintf(d, sizeof d, "%s/mismatch", base);
    for (size_t i = 0; i < 10; i++) {
        ideal(all[i], 1, &r);
        if (i == 8) {
            r.hits[0].polarity = -1;
            r.hits[1].polarity = 1;
        }
        store(d, all[i], 1, &r);
    }
    ideal("out1", 2, &r);
    store(d, "out1", 2, &r);

    snprintf(d, sizeof d, "%s/incomplete", base);
    ideal("out1", 1, &r);
    store(d, "out1", 1, &r);

    TEST_EXIT();
}
