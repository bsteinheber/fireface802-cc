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

int main(void)
{
    test_capture_enable();
    test_capture_disable();
    test_captures_decode();
    test_parity_bit_differs();
    test_null_output();
    TEST_EXIT();
}
