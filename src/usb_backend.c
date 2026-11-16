#include "ff802/usb_backend.h"

const char *ff802_usb_err_str(ff802_usb_err err)
{
    switch (err) {
    case FF802_USB_OK:                 return "success";
    case FF802_USB_ERR_ACCESS:         return "access denied (insufficient permissions)";
    case FF802_USB_ERR_NO_DEVICE:      return "device disconnected";
    case FF802_USB_ERR_NOT_FOUND:      return "not found";
    case FF802_USB_ERR_BUSY:           return "resource busy";
    case FF802_USB_ERR_TIMEOUT:        return "timeout";
    case FF802_USB_ERR_PIPE:           return "endpoint stalled";
    case FF802_USB_ERR_OVERFLOW:       return "overflow";
    case FF802_USB_ERR_IO:             return "I/O error";
    case FF802_USB_ERR_NO_MEM:         return "out of memory";
    case FF802_USB_ERR_NOT_SUPPORTED:  return "operation not supported";
    case FF802_USB_ERR_WRITE_DISABLED: return "USB writes disabled in this build";
    case FF802_USB_ERR_OTHER:          break;
    }
    return "unknown USB error";
}

ff802_layout_status ff802_check_layout(const ff802_dev_layout *layout)
{
    if (layout->config_value != FF802_USB_CONFIG)
        return FF802_LAYOUT_WRONG_CONFIG;
    if (!layout->has_interface)
        return FF802_LAYOUT_NO_INTERFACE;

    unsigned n = layout->num_endpoints;
    if (n > FF802_MAX_ENDPOINTS)
        n = FF802_MAX_ENDPOINTS;

    for (unsigned i = 0; i < n; i++) {
        const ff802_ep_info *ep = &layout->eps[i];
        if (ep->address != FF802_USB_EP_OUT)
            continue;
        if ((ep->attributes & FF802_USB_EP_TYPE_MASK) != FF802_USB_EP_TYPE_BULK)
            return FF802_LAYOUT_NOT_BULK;
        /* Bits 10..0 carry the packet size. */
        if ((ep->max_packet & 0x07FFu) != FF802_USB_MAX_PACKET)
            return FF802_LAYOUT_MAX_PACKET;
        return FF802_LAYOUT_OK;
    }
    return FF802_LAYOUT_NO_ENDPOINT;
}

const char *ff802_layout_status_str(ff802_layout_status st)
{
    switch (st) {
    case FF802_LAYOUT_OK:           return "layout as expected";
    case FF802_LAYOUT_WRONG_CONFIG: return "active USB configuration is not 1";
    case FF802_LAYOUT_NO_INTERFACE: return "interface 1 / alternate setting 0 not found";
    case FF802_LAYOUT_NO_ENDPOINT:  return "bulk OUT endpoint 0x0C not found";
    case FF802_LAYOUT_NOT_BULK:     return "endpoint 0x0C is not a bulk endpoint";
    case FF802_LAYOUT_MAX_PACKET:   return "endpoint 0x0C max packet size is not 512 "
                                           "(device not running at high speed?)";
    }
    return "unknown layout status";
}
