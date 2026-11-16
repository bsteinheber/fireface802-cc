/*
 * Verifies that the real libusb backend reports the write capability the
 * build was configured with. Does not initialise libusb or touch USB.
 */
#include "ff802/usb_comm.h"
#include "test_common.h"

#ifndef FF802_EXPECT_WRITE_ENABLED
#error "FF802_EXPECT_WRITE_ENABLED must be defined by the build"
#endif

int main(void)
{
    CHECK(ff802_usb_comm_write_enabled() == FF802_EXPECT_WRITE_ENABLED);
    TEST_EXIT();
}
