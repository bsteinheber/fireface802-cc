/*
 * Tests for USB id classification. Expected ids are written as literals,
 * independent of the constants in device_id.h.
 */
#include "ff802/device_id.h"
#include "test_common.h"

static void test_known_ids(void)
{
    CHECK(ff802_classify(0x2A39, 0x3FCD) == FF802_MODE_PROPRIETARY);
    CHECK(ff802_classify(0x0424, 0x3FDD) == FF802_MODE_CLASS_COMPLIANT);
}

static void test_near_misses(void)
{
    /* Right vendor, wrong product: other RME device. */
    CHECK(ff802_classify(0x2A39, 0x3FDD) == FF802_MODE_RME_OTHER);
    CHECK(ff802_classify(0x2A39, 0x3FCC) == FF802_MODE_RME_OTHER);
    CHECK(ff802_classify(0x2A39, 0x0000) == FF802_MODE_RME_OTHER);

    /* Microchip vendor id with another PID is an unrelated device. */
    CHECK(ff802_classify(0x0424, 0x3FCD) == FF802_MODE_NONE);
    CHECK(ff802_classify(0x0424, 0x2514) == FF802_MODE_NONE);

    /* Swapped / byte-swapped ids must not match. */
    CHECK(ff802_classify(0x3FCD, 0x2A39) == FF802_MODE_NONE);
    CHECK(ff802_classify(0x392A, 0xCD3F) == FF802_MODE_NONE);

    /* Arbitrary devices. */
    CHECK(ff802_classify(0x0000, 0x0000) == FF802_MODE_NONE);
    CHECK(ff802_classify(0x1D6B, 0x0002) == FF802_MODE_NONE);
    CHECK(ff802_classify(0xFFFF, 0xFFFF) == FF802_MODE_NONE);
}

static void test_is_ff802(void)
{
    CHECK(ff802_mode_is_ff802(FF802_MODE_PROPRIETARY));
    CHECK(ff802_mode_is_ff802(FF802_MODE_CLASS_COMPLIANT));
    CHECK(!ff802_mode_is_ff802(FF802_MODE_RME_OTHER));
    CHECK(!ff802_mode_is_ff802(FF802_MODE_NONE));
}

static void test_names(void)
{
    CHECK(strcmp(ff802_mode_name(FF802_MODE_PROPRIETARY), "proprietary") == 0);
    CHECK(strcmp(ff802_mode_name(FF802_MODE_CLASS_COMPLIANT), "class-compliant") == 0);
    CHECK(strcmp(ff802_mode_name(FF802_MODE_RME_OTHER), "rme-other") == 0);
    CHECK(strcmp(ff802_mode_name(FF802_MODE_NONE), "none") == 0);
    /* Out-of-range values must not crash and fall back to "none". */
    CHECK(strcmp(ff802_mode_name((ff802_mode)42), "none") == 0);
    CHECK(ff802_mode_description((ff802_mode)42) != NULL);
    CHECK(strstr(ff802_mode_description(FF802_MODE_CLASS_COMPLIANT),
                 "not yet confirmed") != NULL);
}

int main(void)
{
    test_known_ids();
    test_near_misses();
    test_is_ff802();
    test_names();
    TEST_EXIT();
}
