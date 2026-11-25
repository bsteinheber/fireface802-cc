/*
 * Tests for the fixed register list of the mixer initialisation and its
 * RME SysEx encoding.
 *
 * The expected values are literals: the registers measured with OSCMix on
 * the unit (docs/mixer_routing.md), the crossed routings switched off in
 * the hardware tests D2/D3, Input 9 -> Analog 3 of the hardware test E1,
 * the v2 list and its checksum, and SysEx bytes computed independently of the
 * module under test. They are deliberately not derived from the code
 * under test.
 */
#include "ff802/cc_mode.h"
#include "ff802/mixer_init.h"
#include "test_common.h"

/* Written with OSCMix and observed on the unit (2026-11-21). */
static const uint16_t MEASURED[][2] = {
    { 0x4020, 0x9000 }, { 0x4061, 0x9000 }, { 0x4228, 0x9000 },
    { 0x4269, 0x9000 }, { 0x42AA, 0x9000 }, { 0x42EB, 0x9000 },
    { 0x1E00, 0x0000 }, { 0x2600, 0x0000 }, { 0x2700, 0x0000 },
    { 0x2800, 0x0000 },
    { 0x2900, 0x0000 },   /* observed in register reports only */
    /* switched off through the project's send path (D3, D2 on 2026-11-23) */
    { 0x4220, 0x0000 }, { 0x4261, 0x0000 },
    /* Input 9 -> Analog 3 at 30 %, hardware test E1 (2026-11-23, passed) */
    { 0x4088, 0x2666 },
};

/* The complete expected list in send order (v3). */
static const uint16_t EXPECTED[FF802_MIXER_INIT_COUNT][2] = {
    { 0x4220, 0x0000 }, { 0x4261, 0x0000 },
    { 0x4020, 0x9000 }, { 0x4061, 0x9000 }, { 0x40A2, 0x9000 }, { 0x40E3, 0x9000 },
    { 0x4124, 0x9000 }, { 0x4165, 0x9000 }, { 0x41A6, 0x9000 }, { 0x41E7, 0x9000 },
    { 0x4228, 0x9000 }, { 0x4269, 0x9000 }, { 0x42AA, 0x9000 }, { 0x42EB, 0x9000 },
    { 0x1E00, 0x0000 }, { 0x1F00, 0x0000 }, { 0x2000, 0x0000 }, { 0x2100, 0x0000 },
    { 0x2200, 0x0000 }, { 0x2300, 0x0000 }, { 0x2400, 0x0000 }, { 0x2500, 0x0000 },
    { 0x2600, 0x0000 }, { 0x2700, 0x0000 }, { 0x2800, 0x0000 }, { 0x2900, 0x0000 },
    { 0x4008, 0x2666 }, { 0x4048, 0x2666 }, { 0x4088, 0x2666 }, { 0x40C8, 0x2666 },
    { 0x4108, 0x2666 }, { 0x4148, 0x2666 }, { 0x4188, 0x2666 }, { 0x41C8, 0x2666 },
    { 0x4208, 0x2666 }, { 0x4248, 0x2666 }, { 0x4288, 0x2666 }, { 0x42C8, 0x2666 },
    /* v4: the other 130 crossed playback routings, off (computed independently) */
    { 0x4021, 0x0000 }, { 0x4022, 0x0000 }, { 0x4023, 0x0000 }, { 0x4024, 0x0000 },
    { 0x4025, 0x0000 }, { 0x4026, 0x0000 }, { 0x4027, 0x0000 }, { 0x4028, 0x0000 },
    { 0x4029, 0x0000 }, { 0x402A, 0x0000 }, { 0x402B, 0x0000 }, { 0x4060, 0x0000 },
    { 0x4062, 0x0000 }, { 0x4063, 0x0000 }, { 0x4064, 0x0000 }, { 0x4065, 0x0000 },
    { 0x4066, 0x0000 }, { 0x4067, 0x0000 }, { 0x4068, 0x0000 }, { 0x4069, 0x0000 },
    { 0x406A, 0x0000 }, { 0x406B, 0x0000 }, { 0x40A0, 0x0000 }, { 0x40A1, 0x0000 },
    { 0x40A3, 0x0000 }, { 0x40A4, 0x0000 }, { 0x40A5, 0x0000 }, { 0x40A6, 0x0000 },
    { 0x40A7, 0x0000 }, { 0x40A8, 0x0000 }, { 0x40A9, 0x0000 }, { 0x40AA, 0x0000 },
    { 0x40AB, 0x0000 }, { 0x40E0, 0x0000 }, { 0x40E1, 0x0000 }, { 0x40E2, 0x0000 },
    { 0x40E4, 0x0000 }, { 0x40E5, 0x0000 }, { 0x40E6, 0x0000 }, { 0x40E7, 0x0000 },
    { 0x40E8, 0x0000 }, { 0x40E9, 0x0000 }, { 0x40EA, 0x0000 }, { 0x40EB, 0x0000 },
    { 0x4120, 0x0000 }, { 0x4121, 0x0000 }, { 0x4122, 0x0000 }, { 0x4123, 0x0000 },
    { 0x4125, 0x0000 }, { 0x4126, 0x0000 }, { 0x4127, 0x0000 }, { 0x4128, 0x0000 },
    { 0x4129, 0x0000 }, { 0x412A, 0x0000 }, { 0x412B, 0x0000 }, { 0x4160, 0x0000 },
    { 0x4161, 0x0000 }, { 0x4162, 0x0000 }, { 0x4163, 0x0000 }, { 0x4164, 0x0000 },
    { 0x4166, 0x0000 }, { 0x4167, 0x0000 }, { 0x4168, 0x0000 }, { 0x4169, 0x0000 },
    { 0x416A, 0x0000 }, { 0x416B, 0x0000 }, { 0x41A0, 0x0000 }, { 0x41A1, 0x0000 },
    { 0x41A2, 0x0000 }, { 0x41A3, 0x0000 }, { 0x41A4, 0x0000 }, { 0x41A5, 0x0000 },
    { 0x41A7, 0x0000 }, { 0x41A8, 0x0000 }, { 0x41A9, 0x0000 }, { 0x41AA, 0x0000 },
    { 0x41AB, 0x0000 }, { 0x41E0, 0x0000 }, { 0x41E1, 0x0000 }, { 0x41E2, 0x0000 },
    { 0x41E3, 0x0000 }, { 0x41E4, 0x0000 }, { 0x41E5, 0x0000 }, { 0x41E6, 0x0000 },
    { 0x41E8, 0x0000 }, { 0x41E9, 0x0000 }, { 0x41EA, 0x0000 }, { 0x41EB, 0x0000 },
    { 0x4221, 0x0000 }, { 0x4222, 0x0000 }, { 0x4223, 0x0000 }, { 0x4224, 0x0000 },
    { 0x4225, 0x0000 }, { 0x4226, 0x0000 }, { 0x4227, 0x0000 }, { 0x4229, 0x0000 },
    { 0x422A, 0x0000 }, { 0x422B, 0x0000 }, { 0x4260, 0x0000 }, { 0x4262, 0x0000 },
    { 0x4263, 0x0000 }, { 0x4264, 0x0000 }, { 0x4265, 0x0000 }, { 0x4266, 0x0000 },
    { 0x4267, 0x0000 }, { 0x4268, 0x0000 }, { 0x426A, 0x0000 }, { 0x426B, 0x0000 },
    { 0x42A0, 0x0000 }, { 0x42A1, 0x0000 }, { 0x42A2, 0x0000 }, { 0x42A3, 0x0000 },
    { 0x42A4, 0x0000 }, { 0x42A5, 0x0000 }, { 0x42A6, 0x0000 }, { 0x42A7, 0x0000 },
    { 0x42A8, 0x0000 }, { 0x42A9, 0x0000 }, { 0x42AB, 0x0000 }, { 0x42E0, 0x0000 },
    { 0x42E1, 0x0000 }, { 0x42E2, 0x0000 }, { 0x42E3, 0x0000 }, { 0x42E4, 0x0000 },
    { 0x42E5, 0x0000 }, { 0x42E6, 0x0000 }, { 0x42E7, 0x0000 }, { 0x42E8, 0x0000 },
    { 0x42E9, 0x0000 }, { 0x42EA, 0x0000 },
};

/* The v2 list (0.2.0~dev3/dev4, tested on the unit), literally. */
static const uint16_t V2[26][2] = {
    { 0x4220, 0x0000 }, { 0x4261, 0x0000 },
    { 0x4020, 0x9000 }, { 0x4061, 0x9000 }, { 0x40A2, 0x9000 }, { 0x40E3, 0x9000 },
    { 0x4124, 0x9000 }, { 0x4165, 0x9000 }, { 0x41A6, 0x9000 }, { 0x41E7, 0x9000 },
    { 0x4228, 0x9000 }, { 0x4269, 0x9000 }, { 0x42AA, 0x9000 }, { 0x42EB, 0x9000 },
    { 0x1E00, 0x0000 }, { 0x1F00, 0x0000 }, { 0x2000, 0x0000 }, { 0x2100, 0x0000 },
    { 0x2200, 0x0000 }, { 0x2300, 0x0000 }, { 0x2400, 0x0000 }, { 0x2500, 0x0000 },
    { 0x2600, 0x0000 }, { 0x2700, 0x0000 }, { 0x2800, 0x0000 }, { 0x2900, 0x0000 },
};

/* FNV-1a over the first n literal pairs (independent of the module). */
static uint32_t fnv_pairs(const uint16_t (*p)[2], unsigned n)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < n; i++) {
        const uint8_t b[4] = { (uint8_t)(p[i][0] & 0xFF), (uint8_t)(p[i][0] >> 8),
                               (uint8_t)(p[i][1] & 0xFF), (uint8_t)(p[i][1] >> 8) };
        for (unsigned j = 0; j < 4u; j++) {
            h ^= b[j];
            h *= 16777619u;
        }
    }
    return h;
}

/* The former list of 24 entries (until 2026-11-23), unchanged in content. */
static const uint16_t FORMER24[24][2] = {
    { 0x4020, 0x9000 }, { 0x4061, 0x9000 }, { 0x40A2, 0x9000 }, { 0x40E3, 0x9000 },
    { 0x4124, 0x9000 }, { 0x4165, 0x9000 }, { 0x41A6, 0x9000 }, { 0x41E7, 0x9000 },
    { 0x4228, 0x9000 }, { 0x4269, 0x9000 }, { 0x42AA, 0x9000 }, { 0x42EB, 0x9000 },
    { 0x1E00, 0x0000 }, { 0x1F00, 0x0000 }, { 0x2000, 0x0000 }, { 0x2100, 0x0000 },
    { 0x2200, 0x0000 }, { 0x2300, 0x0000 }, { 0x2400, 0x0000 }, { 0x2500, 0x0000 },
    { 0x2600, 0x0000 }, { 0x2700, 0x0000 }, { 0x2800, 0x0000 }, { 0x2900, 0x0000 },
};

static void test_table(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    CHECK(t != NULL);
    CHECK(ff802_mixer_init_verify() == 0);
    CHECK_EQ_U32(FF802_MIXER_INIT_COUNT, 168u);
    CHECK_EQ_U32(FF802_MIXER_INIT_V3_COUNT, 38u);
    CHECK_EQ_U32(FF802_MIXER_INIT_V2_COUNT, 26u);
    CHECK_EQ_U32(FF802_MIXER_INPUT9_COUNT, 12u);
    CHECK_EQ_U32(FF802_MIXER_CROSS_COUNT, 2u);
    CHECK_EQ_U32(FF802_MIXER_CROSS_EXTRA_COUNT, 130u);
    CHECK_EQ_U32(FF802_MIXER_PLAYBACK_MATRIX, 144u);
    CHECK(strcmp(FF802_MIXER_INIT_ID, "ff802-mixer-init-v4") == 0);
    CHECK(strcmp(FF802_MIXER_INIT_V3_ID, "ff802-mixer-init-v3") == 0);
    CHECK(strcmp(FF802_MIXER_INIT_V2_ID, "ff802-mixer-init-v2") == 0);
    /* checksums: of the module, of the constants, and recomputed from literals */
    CHECK_EQ_U32(ff802_mixer_init_checksum(), FF802_MIXER_INIT_CHECKSUM);
    CHECK_EQ_U32(FF802_MIXER_INIT_CHECKSUM, 0xC43D2879u);
    CHECK_EQ_U32(fnv_pairs(EXPECTED, 168u), 0xC43D2879u);
    CHECK_EQ_U32(ff802_mixer_init_v3_checksum(), FF802_MIXER_INIT_V3_CHECKSUM);
    CHECK_EQ_U32(FF802_MIXER_INIT_V3_CHECKSUM, 0xEC858A44u);
    CHECK_EQ_U32(fnv_pairs(EXPECTED, 38u), 0xEC858A44u);
    CHECK_EQ_U32(ff802_mixer_init_v2_checksum(), FF802_MIXER_INIT_V2_CHECKSUM);
    CHECK_EQ_U32(FF802_MIXER_INIT_V2_CHECKSUM, 0xC97A359Cu);
    CHECK_EQ_U32(fnv_pairs(V2, 26u), 0xC97A359Cu);
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        CHECK_EQ_U32(t[i].addr, EXPECTED[i][0]);
        CHECK_EQ_U32(t[i].value, EXPECTED[i][1]);
    }
    /* the two crossed routings: Playback 1 -> Phones 9, Playback 2 -> Phones 10 */
    CHECK(t[0].kind == FF802_MIXER_CROSS_OFF && t[0].source == 1 && t[0].channel == 9);
    CHECK(t[1].kind == FF802_MIXER_CROSS_OFF && t[1].source == 2 && t[1].channel == 10);
    CHECK_EQ_U32(t[0].value, 0x0000u);
    CHECK_EQ_U32(t[1].value, 0x0000u);
    for (unsigned i = 2; i < 38u; i++) {
        unsigned k = ((i - 2u) % 12u) + 1u;
        CHECK_EQ_U32(t[i].channel, k);
        if (i < 14u)
            CHECK(t[i].kind == FF802_MIXER_ROUTE && t[i].source == k);
        else if (i < 26u)
            CHECK(t[i].kind == FF802_MIXER_OUTPUT_VOLUME && t[i].source == 0);
        else
            CHECK(t[i].kind == FF802_MIXER_INPUT_ROUTE && t[i].source == 9 &&
                  t[i].value == 0x2666u);
    }
    /* 39..168: crossed playback routings, off, by output then playback */
    unsigned prev = 0;
    for (unsigned i = 38u; i < FF802_MIXER_INIT_COUNT; i++) {
        CHECK(t[i].kind == FF802_MIXER_CROSS_OFF && t[i].value == 0x0000u);
        CHECK(t[i].source != t[i].channel);
        CHECK_EQ_U32(t[i].addr, ff802_mixer_matrix_addr(t[i].source, t[i].channel));
        unsigned key = t[i].channel * 16u + t[i].source;
        CHECK(key > prev);                    /* strictly ordered, so no repetition */
        prev = key;
    }
}

/* Entries 1..38 are exactly the v3 list (tested on the unit, dev6). */
static void test_v3_unchanged(void)
{
    static const uint16_t V3_IN9[12] = { 0x4008, 0x4048, 0x4088, 0x40C8, 0x4108, 0x4148,
                                         0x4188, 0x41C8, 0x4208, 0x4248, 0x4288, 0x42C8 };
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < 26u; i++) {
        CHECK_EQ_U32(t[i].addr, V2[i][0]);
        CHECK_EQ_U32(t[i].value, V2[i][1]);
    }
    for (unsigned k = 0; k < 12u; k++) {
        CHECK_EQ_U32(t[26u + k].addr, V3_IN9[k]);
        CHECK_EQ_U32(t[26u + k].value, 0x2666u);
    }
}

/*
 * The 12 x 12 playback matrix: every Playback p -> Output o (p, o = 1..12)
 * exactly once, 12 at 0 dB (p == o), 132 off (p != o), including both
 * crossed directions inside every stereo pair; nothing outside of it.
 */
static void test_playback_matrix(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    unsigned seen[13][13];
    unsigned active = 0, off = 0, matrix = 0;
    memset(seen, 0, sizeof seen);
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        unsigned a = t[i].addr;
        if ((a & 0xF800u) != 0x4000u || (a & 0x3Fu) < 0x20u)
            continue;                         /* not a playback routing */
        unsigned o = ((a >> 6) & 0x1Fu) + 1u, p = (a & 0x3Fu) - 0x20u + 1u;
        CHECK(o >= 1u && o <= 12u && p >= 1u && p <= 12u);  /* no Playback/Output 13..30 */
        if (o > 12u || p > 12u)
            continue;
        seen[p][o]++;
        matrix++;
        if (p == o) {
            CHECK_EQ_U32(t[i].value, 0x9000u);
            CHECK(t[i].kind == FF802_MIXER_ROUTE);
            active++;
        } else {
            CHECK_EQ_U32(t[i].value, 0x0000u);
            CHECK(t[i].kind == FF802_MIXER_CROSS_OFF);
            off++;
        }
    }
    CHECK_EQ_U32(matrix, 144u);
    CHECK_EQ_U32(active, 12u);
    CHECK_EQ_U32(off, 132u);
    for (unsigned p = 1; p <= 12u; p++)
        for (unsigned o = 1; o <= 12u; o++)
            CHECK_EQ_U32(seen[p][o], 1u);
    /* inside the stereo pairs: Playback 1 -> Analog 2 and Playback 2 -> Analog 1 off ... */
    for (unsigned pair = 1; pair <= 11u; pair += 2u) {
        int left_to_right = 0, right_to_left = 0;
        for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
            if (t[i].addr == ff802_mixer_matrix_addr(pair, pair + 1u))
                left_to_right = t[i].value == 0x0000u;
            if (t[i].addr == ff802_mixer_matrix_addr(pair + 1u, pair))
                right_to_left = t[i].value == 0x0000u;
        }
        CHECK(left_to_right && right_to_left);
    }
    /* ... and the routing heard on the unit: Playback 3/4 -> Phones 11/12 */
    CHECK_EQ_U32(ff802_mixer_matrix_addr(3, 11), 0x42A2u);
    CHECK_EQ_U32(ff802_mixer_matrix_addr(4, 12), 0x42E3u);
    int found3 = 0, found4 = 0;
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        found3 += t[i].addr == 0x42A2u && t[i].value == 0x0000u;
        found4 += t[i].addr == 0x42E3u && t[i].value == 0x0000u;
    }
    CHECK(found3 == 1 && found4 == 1);
}

/* Entries 1..26 are exactly the v2 list, in the same order. */
static void test_v2_unchanged(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < 26u; i++) {
        CHECK_EQ_U32(t[i].addr, V2[i][0]);
        CHECK_EQ_U32(t[i].value, V2[i][1]);
    }
}

/* Input 9 -> Output k: the formula, the level, the mono signal on both
 * channels of every pair (same value on the left and right output). */
static void test_input9(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    /* 0x2666 = lround(0.3 * 0x8000), lower (linear) range of OSCMix setmixlevel */
    CHECK_EQ_U32((uint32_t)(0.3 * 32768.0 + 0.5), 0x2666u);
    CHECK(0x2666u <= 0x4000u);
    for (unsigned k = 1; k <= 12u; k++) {
        const ff802_mixer_entry *e = &t[26u + k - 1u];
        CHECK_EQ_U32(e->addr, 0x4000u | ((k - 1u) << 6) | 8u);
        CHECK_EQ_U32(ff802_mixer_input_addr(9, k), e->addr);
        CHECK_EQ_U32(e->value, FF802_MIXER_INPUT9_LEVEL);
        CHECK_EQ_U32(e->addr & 0x3Fu, 8u);                 /* source index 8 */
        CHECK_EQ_U32((e->addr >> 6) & 0x1Fu, k - 1u);       /* output index */
    }
    for (unsigned pair = 0; pair < 6u; pair++)
        CHECK_EQ_U32(t[26u + 2u * pair].value, t[27u + 2u * pair].value);
    CHECK_EQ_U32(ff802_mixer_input_addr(9, 3), 0x4088u);    /* E1 */
    CHECK_EQ_U32(ff802_mixer_input_addr(1, 1), 0x4000u);
    CHECK_EQ_U32(ff802_mixer_input_addr(30, 12), 0x42DDu);
    CHECK_EQ_U32(ff802_mixer_input_addr(0, 1), 0);
    CHECK_EQ_U32(ff802_mixer_input_addr(31, 1), 0);
    CHECK_EQ_U32(ff802_mixer_input_addr(9, 0), 0);
    CHECK_EQ_U32(ff802_mixer_input_addr(9, 13), 0);
}

/* The former 24 values (v1) are entries 3..26, unchanged and in the same order. */
static void test_former_24_unchanged(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < 24u; i++) {
        CHECK_EQ_U32(t[2u + i].addr, FORMER24[i][0]);
        CHECK_EQ_U32(t[2u + i].value, FORMER24[i][1]);
    }
}

static void test_no_duplicates(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++)
        for (unsigned j = i + 1u; j < FF802_MIXER_INIT_COUNT; j++)
            CHECK(t[i].addr != t[j].addr);
}

static void test_measured_registers_in_table(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (size_t m = 0; m < sizeof MEASURED / sizeof MEASURED[0]; m++) {
        int found = 0;
        for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++)
            if (t[i].addr == MEASURED[m][0] && t[i].value == MEASURED[m][1])
                found++;
        CHECK(found == 1);
    }
}

static void test_formulas(void)
{
    for (unsigned k = 1; k <= 12u; k++) {
        CHECK_EQ_U32(ff802_mixer_route_addr(k), EXPECTED[2u + k - 1u][0]);
        CHECK_EQ_U32(ff802_mixer_outvol_addr(k), EXPECTED[14u + k - 1u][0]);
        CHECK_EQ_U32(ff802_mixer_matrix_addr(k, k), ff802_mixer_route_addr(k));
    }
    CHECK_EQ_U32(ff802_mixer_matrix_addr(1, 9), 0x4220);
    CHECK_EQ_U32(ff802_mixer_matrix_addr(2, 10), 0x4261);
    CHECK_EQ_U32(ff802_mixer_matrix_addr(10, 2), 0x4069);   /* not 0x4261 */
    CHECK_EQ_U32(ff802_mixer_route_addr(0), 0);
    CHECK_EQ_U32(ff802_mixer_route_addr(13), 0);
    CHECK_EQ_U32(ff802_mixer_matrix_addr(0, 9), 0);
    CHECK_EQ_U32(ff802_mixer_matrix_addr(1, 13), 0);
    CHECK_EQ_U32(ff802_mixer_outvol_addr(0), 0);
    CHECK_EQ_U32(ff802_mixer_outvol_addr(13), 0);
}

/* Nothing outside the 168 approved writes can be in the list. */
static void test_nothing_else(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    static const uint16_t FORBIDDEN[] = {
        0x3D44, 0x3F00, 0x3F97, 0x3F99,           /* CC mode, serial, store, refresh */
        0x3D00, 0x3D01, 0x3D02, 0x3D03, 0x3D04,   /* control room */
        0x402C, 0x406D, 0x4320, 0x4333,           /* playback 13, 14; output 13 */
        0x403D, 0x42FD, 0x401E, 0x401F,           /* playback 30; between inputs and playback */
        0x4000, 0x4041, 0x4200, 0x4241,           /* inputs 1, 2 -> outputs */
        0x4007, 0x4009, 0x4087, 0x4089, 0x4308,   /* inputs 8, 10; Input 9 -> output 13 */
        0x0807, 0x0808, 0x0809,                   /* Input 9 gain, 48 V, Hi-Z */
        0x1E01, 0x1E02, 0x2601, 0x2602,           /* pan, mute */
        0x2A00, 0x2B00,                           /* outputs 13, 14 */
    };
    unsigned cross = 0, input = 0;
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        for (size_t f = 0; f < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; f++)
            CHECK(t[i].addr != FORBIDDEN[f]);
        if (t[i].kind == FF802_MIXER_ROUTE || t[i].kind == FF802_MIXER_CROSS_OFF ||
            t[i].kind == FF802_MIXER_INPUT_ROUTE) {
            unsigned out = (t[i].addr >> 6) & 0x1Fu;
            unsigned src = t[i].addr & 0x3Fu;
            CHECK((t[i].addr & 0xF800u) == 0x4000u);
            CHECK(out < 12u);
            if (t[i].kind == FF802_MIXER_INPUT_ROUTE) {
                CHECK(src == 8u);                 /* Mic/Inst 9 only */
                CHECK_EQ_U32(t[i].value, 0x2666u);
                CHECK(i >= 26u);                  /* after the whole v2 part */
                input++;
                continue;
            }
            CHECK(src >= 0x20u);                  /* a playback */
            CHECK(src < 0x20u + 12u);
            if (t[i].kind == FF802_MIXER_ROUTE) {
                CHECK(src == 0x20u + out);        /* the direct routing */
                CHECK_EQ_U32(t[i].value, 0x9000u);
            } else {
                CHECK(src != 0x20u + out);        /* a crossed routing ... */
                CHECK_EQ_U32(t[i].value, 0x0000u); /* ... only ever switched off */
                cross++;
            }
        } else {
            CHECK((t[i].addr & 0x00FFu) == 0);    /* the volume, not pan/mute */
            CHECK(t[i].addr >= 0x1E00u && t[i].addr <= 0x2900u);
        }
    }
    CHECK_EQ_U32(cross, 132u);
    CHECK_EQ_U32(input, 12u);
}

static void test_sysex(void)
{
    /* computed by hand: word, 5 septets LSB first */
    static const uint8_t PB1_AN1[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x02, 0x01, 0x0C, 0xF7 };
    static const uint8_t PB2_AN2[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x06, 0x03, 0x0C, 0xF7 };
    static const uint8_t PB9_PH9[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x20, 0x22, 0x11, 0x0C, 0xF7 };
    static const uint8_t VOL_PH9[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x00, 0x30, 0x02, 0xF7 };
    /* sent on the unit in D3 and D2/B.4 (switched the routings off) */
    static const uint8_t PB1_PH9_OFF[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x00, 0x11, 0x04, 0xF7 };
    static const uint8_t PB2_PH10_OFF[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x04, 0x13, 0x04, 0xF7 };
    uint8_t out[12];

    CHECK(ff802_mixer_init_message(0, out) == FF802_OK);
    CHECK_BYTES(out, PB1_PH9_OFF, 12);
    CHECK(ff802_mixer_init_message(1, out) == FF802_OK);
    CHECK_BYTES(out, PB2_PH10_OFF, 12);
    CHECK(ff802_mixer_init_message(2, out) == FF802_OK);
    CHECK_BYTES(out, PB1_AN1, 12);
    /* Input 9 -> Analog 3, sent on the unit in E1 */
    static const uint8_t IN9_AN3[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x66, 0x4C, 0x20, 0x04, 0x0C, 0xF7 };
    CHECK(ff802_mixer_init_message(28, out) == FF802_OK);
    CHECK_BYTES(out, IN9_AN3, 12);
    CHECK(ff802_mixer_init_message(37, out) == FF802_OK);
    CHECK(ff802_mixer_init_message(167, out) == FF802_OK);
    /* Playback 3 -> Phones 11 off (shell computation): word 0x42A20000, parity bit 0 */
    static const uint8_t PB3_PH11_OFF[12] =
        { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00, 0x00, 0x00, 0x08, 0x15, 0x04, 0xF7 };
    CHECK(ff802_rme_sysex_reg(0x42A2, 0x0000, out) == FF802_OK);
    CHECK_BYTES(out, PB3_PH11_OFF, 12);
    CHECK(ff802_mixer_init_message(168, out) == FF802_ERR_ADDR_RANGE);

    CHECK(ff802_rme_sysex_reg(0x4020, 0x9000, out) == FF802_OK);
    CHECK_BYTES(out, PB1_AN1, 12);
    CHECK(ff802_rme_sysex_reg(0x4061, 0x9000, out) == FF802_OK);
    CHECK_BYTES(out, PB2_AN2, 12);
    CHECK(ff802_rme_sysex_reg(0x4228, 0x9000, out) == FF802_OK);
    CHECK_BYTES(out, PB9_PH9, 12);
    CHECK(ff802_rme_sysex_reg(0x2600, 0x0000, out) == FF802_OK);
    CHECK_BYTES(out, VOL_PH9, 12);

    /* every message of the list: framed, 7-bit data, decodes back */
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        CHECK(ff802_rme_sysex_reg(t[i].addr, t[i].value, out) == FF802_OK);
        CHECK(out[0] == 0xF0 && out[11] == 0xF7);
        uint32_t w = 0;
        for (unsigned j = 1; j < 11u; j++)
            CHECK(out[j] < 0x80u);
        for (unsigned j = 0; j < 5u; j++)
            w |= (uint32_t)out[6u + j] << (7u * j);
        uint16_t a = 0, v = 0;
        CHECK(ff802_reg_decode(w, &a, &v) == FF802_OK);
        CHECK_EQ_U32(a, t[i].addr);
        CHECK_EQ_U32(v, t[i].value);
    }

    /* errors leave the output untouched */
    uint8_t keep[12] = { 0xAA };
    CHECK(ff802_rme_sysex_reg(0x8000, 0, keep) == FF802_ERR_ADDR_RANGE);
    CHECK(keep[0] == 0xAA);
    CHECK(ff802_rme_sysex_reg(0x4020, 0x9000, NULL) == FF802_ERR_NULL_ARG);
}

/* Same frame as the CC disable command tested on the unit. */
static void test_same_frame_as_cc_disable(void)
{
    uint8_t usb[FF802_CC_DISABLE_MIDI_SIZE];
    uint8_t payload[12];
    uint8_t out[12];

    CHECK(ff802_cc_disable_midi(usb) == FF802_OK);
    for (unsigned p = 0; p < 4u; p++)
        for (unsigned j = 0; j < 3u; j++)
            payload[3u * p + j] = usb[4u * p + 1u + j];
    CHECK(ff802_rme_sysex_reg(FF802_REG_CC_MODE, FF802_CC_VALUE_SYSEX_DISABLE, out) == FF802_OK);
    CHECK_BYTES(out, payload, 12);
}

static void test_labels(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    char buf[64];
    ff802_mixer_entry_label(&t[0], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 1 -> Phones 9, off") == 0);
    ff802_mixer_entry_label(&t[1], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 2 -> Phones 10, off") == 0);
    ff802_mixer_entry_label(&t[2], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 1 -> Analog 1, 0 dB") == 0);
    ff802_mixer_entry_label(&t[11], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 10 -> Phones 10, 0 dB") == 0);
    ff802_mixer_entry_label(&t[25], buf, sizeof buf);
    CHECK(strcmp(buf, "Output Phones 12 volume, 0 dB") == 0);
    ff802_mixer_entry_label(&t[26], buf, sizeof buf);
    CHECK(strcmp(buf, "Input 9 -> Analog 1, 30 %") == 0);
    ff802_mixer_entry_label(&t[28], buf, sizeof buf);
    CHECK(strcmp(buf, "Input 9 -> Analog 3, 30 %") == 0);
    ff802_mixer_entry_label(&t[37], buf, sizeof buf);
    CHECK(strcmp(buf, "Input 9 -> Phones 12, 30 %") == 0);
    ff802_mixer_entry_label(&t[38], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 2 -> Analog 1, off") == 0);
    ff802_mixer_entry_label(&t[167], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 11 -> Phones 12, off") == 0);
    CHECK(strcmp(ff802_mixer_output_name(0), "?") == 0);
    CHECK(strcmp(ff802_mixer_output_name(8), "Analog 8") == 0);
    ff802_mixer_entry_label(NULL, buf, sizeof buf);
    CHECK(buf[0] == '\0');
}

int main(void)
{
    test_table();
    test_v2_unchanged();
    test_v3_unchanged();
    test_playback_matrix();
    test_input9();
    test_former_24_unchanged();
    test_no_duplicates();
    test_measured_registers_in_table();
    test_formulas();
    test_nothing_else();
    test_sysex();
    test_same_frame_as_cc_disable();
    test_labels();
    TEST_EXIT();
}
