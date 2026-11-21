/*
 * Tests for the fixed register list of the mixer initialisation and its
 * RME SysEx encoding.
 *
 * The expected values are literals: the registers measured with OSCMix on
 * the unit (docs/mixer_routing.md) and SysEx bytes computed independently
 * of the module under test. They are deliberately not derived from the
 * code under test.
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
};

/* The complete expected list in send order. */
static const uint16_t EXPECTED[FF802_MIXER_INIT_COUNT][2] = {
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
    CHECK_EQ_U32(ff802_mixer_init_checksum(), FF802_MIXER_INIT_CHECKSUM);
    CHECK_EQ_U32(FF802_MIXER_INIT_COUNT, 24u);
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        CHECK_EQ_U32(t[i].addr, EXPECTED[i][0]);
        CHECK_EQ_U32(t[i].value, EXPECTED[i][1]);
        CHECK_EQ_U32(t[i].channel, (i % 12u) + 1u);
        CHECK(t[i].kind == (i < 12u ? FF802_MIXER_ROUTE : FF802_MIXER_OUTPUT_VOLUME));
    }
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
        CHECK_EQ_U32(ff802_mixer_route_addr(k), EXPECTED[k - 1u][0]);
        CHECK_EQ_U32(ff802_mixer_outvol_addr(k), EXPECTED[12u + k - 1u][0]);
    }
    CHECK_EQ_U32(ff802_mixer_route_addr(0), 0);
    CHECK_EQ_U32(ff802_mixer_route_addr(13), 0);
    CHECK_EQ_U32(ff802_mixer_outvol_addr(0), 0);
    CHECK_EQ_U32(ff802_mixer_outvol_addr(13), 0);
}

/* Nothing outside the 24 approved writes can be in the list. */
static void test_nothing_else(void)
{
    const ff802_mixer_entry *t = ff802_mixer_init_table();
    static const uint16_t FORBIDDEN[] = {
        0x3D44, 0x3F00, 0x3F97, 0x3F99,           /* CC mode, serial, store, refresh */
        0x4229, 0x4268, 0x4021, 0x4060,           /* crossed routings */
        0x4000, 0x4041,                           /* Input 1/2 -> Output 1/2 */
        0x1E01, 0x1E02, 0x2601, 0x2602,           /* pan, mute */
        0x2A00, 0x2B00,                           /* outputs 13, 14 */
    };
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        for (size_t f = 0; f < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; f++)
            CHECK(t[i].addr != FORBIDDEN[f]);
        if (t[i].kind == FF802_MIXER_ROUTE) {
            unsigned out = (t[i].addr >> 6) & 0x1Fu;
            unsigned src = t[i].addr & 0x3Fu;
            CHECK((t[i].addr & 0xF800u) == 0x4000u);
            CHECK(src >= 0x20u);                  /* a playback, never an input */
            CHECK(src == 0x20u + out);            /* the direct routing only */
            CHECK(out < 12u);
        } else {
            CHECK((t[i].addr & 0x00FFu) == 0);    /* the volume, not pan/mute */
            CHECK(t[i].addr >= 0x1E00u && t[i].addr <= 0x2900u);
        }
    }
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
    uint8_t out[12];

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
    CHECK(strcmp(buf, "Playback 1 -> Analog 1, 0 dB") == 0);
    ff802_mixer_entry_label(&t[9], buf, sizeof buf);
    CHECK(strcmp(buf, "Playback 10 -> Phones 10, 0 dB") == 0);
    ff802_mixer_entry_label(&t[23], buf, sizeof buf);
    CHECK(strcmp(buf, "Output Phones 12 volume, 0 dB") == 0);
    CHECK(strcmp(ff802_mixer_output_name(0), "?") == 0);
    CHECK(strcmp(ff802_mixer_output_name(8), "Analog 8") == 0);
    ff802_mixer_entry_label(NULL, buf, sizeof buf);
    CHECK(buf[0] == '\0');
}

int main(void)
{
    test_table();
    test_measured_registers_in_table();
    test_formulas();
    test_nothing_else();
    test_sysex();
    test_same_frame_as_cc_disable();
    test_labels();
    TEST_EXIT();
}
