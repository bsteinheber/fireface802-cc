/*
 * Unit tests for the generic register codec: parity, bit layout,
 * byte order, error handling and bit-flip detection.
 *
 * Reference values are written as literals or computed with a naive,
 * independent bit count so that the tests do not reuse the code under test.
 */
#include "ff802/reg_codec.h"
#include "test_common.h"

/* Naive reference bit count, intentionally different from the codec. */
static unsigned ref_popcount(uint32_t x)
{
    unsigned n = 0;
    for (unsigned i = 0; i < 32; i++)
        n += (x >> i) & 1u;
    return n;
}

static void test_byte_order(void)
{
    static const uint8_t expected[4] = { 0x78, 0x56, 0x34, 0x12 };
    uint8_t out[4] = { 0 };

    ff802_word_to_le(UINT32_C(0x12345678), out);
    CHECK_BYTES(out, expected, 4);
    CHECK_EQ_U32(ff802_word_from_le(expected), 0x12345678u);

    /* Round trip on a few patterns, including all-zero and all-one. */
    static const uint32_t patterns[] = {
        0x00000000u, 0xFFFFFFFFu, 0x80000001u, 0x01020304u, 0xA5A55A5Au,
    };
    for (size_t i = 0; i < sizeof patterns / sizeof patterns[0]; i++) {
        ff802_word_to_le(patterns[i], out);
        CHECK_EQ_U32(ff802_word_from_le(out), patterns[i]);
    }

    /* The least significant byte must be transmitted first. */
    ff802_word_to_le(UINT32_C(0x000000FF), out);
    CHECK(out[0] == 0xFF && out[1] == 0 && out[2] == 0 && out[3] == 0);
    ff802_word_to_le(UINT32_C(0xFF000000), out);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0xFF);
}

static void test_known_words(void)
{
    uint32_t w = 0;

    /* addr 0, value 0: zero set bits -> parity bit must be set. */
    CHECK(ff802_reg_encode(0x0000, 0x0000, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x80000000u);

    /* Max addr and value: 31 set bits (odd) -> parity bit clear. */
    CHECK(ff802_reg_encode(0x7FFF, 0xFFFF, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x7FFFFFFFu);

    /* Value occupies bits 15..0, address bits 30..16. */
    CHECK(ff802_reg_encode(0x0001, 0x0000, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x00010000u);
    CHECK(ff802_reg_encode(0x0000, 0x0001, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x00000001u);
    CHECK(ff802_reg_encode(0x4000, 0x0000, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x40000000u);
    CHECK(ff802_reg_encode(0x0003, 0x0000, &w) == FF802_OK);
    CHECK_EQ_U32(w, 0x80030000u);
}

static void test_single_bits(void)
{
    /* Exactly one payload bit set -> odd already -> no parity bit. */
    for (unsigned i = 0; i < 16; i++) {
        uint32_t w = 0;
        CHECK(ff802_reg_encode(0, (uint16_t)(1u << i), &w) == FF802_OK);
        CHECK_EQ_U32(w, UINT32_C(1) << i);
    }
    for (unsigned i = 0; i < 15; i++) {
        uint32_t w = 0;
        CHECK(ff802_reg_encode((uint16_t)(1u << i), 0, &w) == FF802_OK);
        CHECK_EQ_U32(w, UINT32_C(1) << (16 + i));
    }
}

static void test_parity_sweep(void)
{
    /* Coarse sweep over the address/value space, checked against the
     * naive reference and the decoder. */
    unsigned bad_parity = 0, bad_layout = 0, bad_roundtrip = 0;

    for (uint32_t addr = 0; addr <= FF802_REG_ADDR_MAX; addr += 97) {
        for (uint32_t value = 0; value <= 0xFFFFu; value += 251) {
            uint32_t w = 0;
            if (ff802_reg_encode((uint16_t)addr, (uint16_t)value, &w) != FF802_OK) {
                bad_layout++;
                continue;
            }
            if (ref_popcount(w) % 2u != 1u)
                bad_parity++;
            if ((w & 0x7FFFFFFFu) != ((addr << 16) | value))
                bad_layout++;

            uint16_t a = 0, v = 0;
            if (ff802_reg_decode(w, &a, &v) != FF802_OK || a != addr || v != value)
                bad_roundtrip++;
        }
    }
    CHECK(bad_parity == 0);
    CHECK(bad_layout == 0);
    CHECK(bad_roundtrip == 0);
}

static void test_parity_ok_matches_reference(void)
{
    static const uint32_t words[] = {
        0x00000000u, 0x00000001u, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFFFFu,
        0xBD440001u, 0x3D440000u, 0x12345678u, 0xDEADBEEFu,
    };
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        CHECK(ff802_reg_parity_ok(words[i]) == (ref_popcount(words[i]) % 2u == 1u));
}

static void test_bit_flip_detection(void)
{
    /* Flipping any single bit of a valid word must break parity. */
    static const struct { uint16_t addr, value; } samples[] = {
        { 0x0000, 0x0000 }, { 0x7FFF, 0xFFFF }, { 0x3D44, 0x0001 },
        { 0x3D44, 0x0000 }, { 0x1234, 0xABCD }, { 0x0001, 0x8000 },
    };

    for (size_t s = 0; s < sizeof samples / sizeof samples[0]; s++) {
        uint32_t w = 0;
        CHECK(ff802_reg_encode(samples[s].addr, samples[s].value, &w) == FF802_OK);
        CHECK(ff802_reg_parity_ok(w));

        for (unsigned bit = 0; bit < 32; bit++) {
            uint32_t corrupted = w ^ (UINT32_C(1) << bit);
            uint16_t a = 0xAAAA, v = 0x5555;

            CHECK(!ff802_reg_parity_ok(corrupted));
            CHECK(ff802_reg_decode(corrupted, &a, &v) == FF802_ERR_PARITY);
            /* Outputs must stay untouched on error. */
            CHECK(a == 0xAAAA && v == 0x5555);
        }
    }
}

static void test_errors(void)
{
    uint32_t w = 0xCAFEBABEu;
    uint8_t out[4] = { 1, 2, 3, 4 };
    static const uint8_t untouched[4] = { 1, 2, 3, 4 };
    uint16_t a = 0, v = 0;

    CHECK(ff802_reg_encode(0x8000, 0, &w) == FF802_ERR_ADDR_RANGE);
    CHECK(ff802_reg_encode(0xFFFF, 0, &w) == FF802_ERR_ADDR_RANGE);
    CHECK_EQ_U32(w, 0xCAFEBABEu);

    CHECK(ff802_reg_encode_bytes(0x8000, 0, out) == FF802_ERR_ADDR_RANGE);
    CHECK_BYTES(out, untouched, 4);

    CHECK(ff802_reg_encode(0, 0, NULL) == FF802_ERR_NULL_ARG);
    CHECK(ff802_reg_encode_bytes(0, 0, NULL) == FF802_ERR_NULL_ARG);
    CHECK(ff802_reg_decode(0x80000000u, NULL, &v) == FF802_ERR_NULL_ARG);
    CHECK(ff802_reg_decode(0x80000000u, &a, NULL) == FF802_ERR_NULL_ARG);
}

static void test_encode_bytes(void)
{
    uint8_t out[4] = { 0 };

    /* 0x1234ABCD has 15 set bits (odd) -> parity bit clear. */
    static const uint8_t expected_odd[4] = { 0xCD, 0xAB, 0x34, 0x12 };
    CHECK(ff802_reg_encode_bytes(0x1234, 0xABCD, out) == FF802_OK);
    CHECK_BYTES(out, expected_odd, 4);

    /* 0x1234ABCC has 14 set bits (even) -> parity bit set -> 0x9234ABCC. */
    static const uint8_t expected_even[4] = { 0xCC, 0xAB, 0x34, 0x92 };
    CHECK(ff802_reg_encode_bytes(0x1234, 0xABCC, out) == FF802_OK);
    CHECK_BYTES(out, expected_even, 4);
}

int main(void)
{
    test_byte_order();
    test_known_words();
    test_single_bits();
    test_parity_sweep();
    test_parity_ok_matches_reference();
    test_bit_flip_detection();
    test_errors();
    test_encode_bytes();
    TEST_EXIT();
}
