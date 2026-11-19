/*
 * Claim with automatic kernel driver detach, for ff802-cc disable only.
 *
 * In class-compliant mode snd-usb-audio is bound to the USB-MIDI
 * interface 3 that carries the disable command. As in the tested
 * transfer (2026-11-19), libusb auto-detach makes the claim detach the
 * driver of exactly that interface, and the release re-attach it. No
 * explicit detach call, no reset, no configuration change.
 *
 * Separate from usb_comm.c so that only programs which call
 * ff802_usb_comm_allow_detach_claim() contain this code. Without
 * FF802_ENABLE_USB_WRITE nothing here references libusb.
 */
#include "ff802/usb_comm.h"

#ifndef FF802_ENABLE_USB_WRITE
#define FF802_ENABLE_USB_WRITE 0
#endif

#if FF802_ENABLE_USB_WRITE

#include <libusb.h>

static ff802_usb_err map_err(int r)
{
    switch (r) {
    case LIBUSB_SUCCESS:             return FF802_USB_OK;
    case LIBUSB_ERROR_ACCESS:        return FF802_USB_ERR_ACCESS;
    case LIBUSB_ERROR_NO_DEVICE:     return FF802_USB_ERR_NO_DEVICE;
    case LIBUSB_ERROR_NOT_FOUND:     return FF802_USB_ERR_NOT_FOUND;
    case LIBUSB_ERROR_BUSY:          return FF802_USB_ERR_BUSY;
    case LIBUSB_ERROR_NOT_SUPPORTED: return FF802_USB_ERR_NOT_SUPPORTED;
    case LIBUSB_ERROR_IO:            return FF802_USB_ERR_IO;
    default:                         return FF802_USB_ERR_OTHER;
    }
}

static ff802_usb_err c_claim_detach(void *self, ff802_usb_handle handle,
                                    uint8_t iface)
{
    (void)self;
    int r = libusb_set_auto_detach_kernel_driver(handle, 1);
    if (r != LIBUSB_SUCCESS)
        return map_err(r);
    return map_err(libusb_claim_interface(handle, iface));
}

#else /* !FF802_ENABLE_USB_WRITE */

static ff802_usb_err c_claim_detach(void *self, ff802_usb_handle handle,
                                    uint8_t iface)
{
    (void)self; (void)handle; (void)iface;
    return FF802_USB_ERR_WRITE_DISABLED;
}

#endif

void ff802_usb_comm_allow_detach_claim(ff802_usb_backend *be)
{
    be->claim_detach = c_claim_detach;
}
