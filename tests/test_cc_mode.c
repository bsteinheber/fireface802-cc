/*
 * Tests for the class-compliant mode command.
 *
 * The expected bytes are fixed reference values taken verbatim from
 * Windows USB captures of the vendor software. They are deliberately
 * written as literals and not derived from the codec under test.
 */
#include "ff802/cc_mode.h"
#include "test_common.h"

/* Captured from Windows: enable class-compliant mode. */
static const uint8_t CAPTURE_CC_ENABLE[4]  = { 0x01, 0x00, 0x44, 0xBD };
/* Captured from Windows: disable class-compliant mode. */
static const uint8_t CAPTURE_CC_DISABLE[4] = { 0x00, 0x00, 0x44, 0x3D };

static void test_capture_enable(void)
{
    uint8_t out[4] = { 0 };
    CHECK(ff802_cc_command(true, out) == FF802_OK);
    CHECK_BYTES(out, CAPTURE_CC_ENABLE, 4);
}

static void test_capture_disable(void)
{
    uint8_t out[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(ff802_cc_command(false, out) == FF802_OK);
    CHECK_BYTES(out, CAPTURE_CC_DISABLE, 4);
}

static void test_captures_decode(void)
{
    /* The captured commands must decode to register 0x3D44 with the
     * expected values and must carry valid parity. */
    uint16_t addr = 0, value = 0;

    uint32_t w_on = ff802_word_from_le(CAPTURE_CC_ENABLE);
    CHECK_EQ_U32(w_on, 0xBD440001u);
    CHECK(ff802_reg_parity_ok(w_on));
    CHECK(ff802_reg_decode(w_on, &addr, &value) == FF802_OK);
    CHECK_EQ_U32(addr, 0x3D44u);
    CHECK_EQ_U32(value, 0x0001u);

    uint32_t w_off = ff802_word_from_le(CAPTURE_CC_DISABLE);
    CHECK_EQ_U32(w_off, 0x3D440000u);
    CHECK(ff802_reg_parity_ok(w_off));
    CHECK(ff802_reg_decode(w_off, &addr, &value) == FF802_OK);
    CHECK_EQ_U32(addr, 0x3D44u);
    CHECK_EQ_U32(value, 0x0000u);
}

static void test_parity_bit_differs(void)
{
    /* The two commands differ in exactly the value LSB and the parity
     * bit (bit 31) -- a direct consequence of odd parity. */
    uint32_t diff = ff802_word_from_le(CAPTURE_CC_ENABLE)
                  ^ ff802_word_from_le(CAPTURE_CC_DISABLE);
    CHECK_EQ_U32(diff, 0x80000001u);
}

static void test_null_output(void)
{
    CHECK(ff802_cc_command(true, NULL) == FF802_ERR_NULL_ARG);
}

/* Transfer that switched a real unit from CC to proprietary mode
 * (hardware test 2026-11-19, interface 3, bulk OUT 0x07). Literal. */
static const uint8_t TESTED_CC_DISABLE_MIDI[16] = {
    0x13, 0xF0, 0x00, 0x20, 0x13, 0x0D, 0x10, 0x00,
    0x13, 0x7E, 0x7F, 0x13, 0x13, 0x6A, 0x0B, 0xF7
};

static void test_disable_midi(void)
{
    uint8_t out[16];
    memset(out, 0xAA, sizeof out);
    CHECK(ff802_cc_disable_midi(out) == FF802_OK);
    CHECK_BYTES(out, TESTED_CC_DISABLE_MIDI, 16);
    CHECK(ff802_cc_disable_midi(NULL) == FF802_ERR_NULL_ARG);
}

/* The tested bytes carry register 0x3D44 = 0xFFFE with valid parity. */
static void test_disable_midi_decodes(void)
{
    const uint8_t *b = TESTED_CC_DISABLE_MIDI;
    uint8_t sysex[12];
    for (int p = 0; p < 4; p++) {
        CHECK(b[4 * p] == 0x13);
        for (int i = 0; i < 3; i++)
            sysex[3 * p + i] = b[4 * p + 1 + i];
    }
    static const uint8_t head[6] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };
    CHECK_BYTES(sysex, head, 6);
    CHECK(sysex[11] == 0xF7);
    uint32_t w = 0;
    for (int i = 0; i < 5; i++) {
        CHECK(sysex[6 + i] <= 0x7F);
        w |= (uint32_t)sysex[6 + i] << (7 * i);
    }
    CHECK_EQ_U32(w, 0xBD44FFFEu);
    uint16_t addr = 0, value = 0;
    CHECK(ff802_reg_decode(w, &addr, &value) == FF802_OK);
    CHECK_EQ_U32(addr, 0x3D44u);
    CHECK_EQ_U32(value, 0xFFFEu);
}

int main(void)
{
    test_capture_enable();
    test_capture_disable();
    test_captures_decode();
    test_parity_bit_differs();
    test_null_output();
    test_disable_midi();
    test_disable_midi_decodes();
    TEST_EXIT();
}
