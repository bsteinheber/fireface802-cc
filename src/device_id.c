#include "ff802/device_id.h"

ff802_mode ff802_classify(uint16_t vid, uint16_t pid)
{
    if (vid == FF802_VID_RME && pid == FF802_PID_PROPRIETARY)
        return FF802_MODE_PROPRIETARY;
    if (vid == FF802_VID_CLASS_COMPLIANT && pid == FF802_PID_CLASS_COMPLIANT)
        return FF802_MODE_CLASS_COMPLIANT;
    /* Other RME products share the vendor id. Report them separately so
     * that a user with e.g. a Fireface 802 FS gets a helpful hint. The CC
     * vendor id 0x0424 belongs to Microchip and is used by many unrelated
     * devices, so only the exact PID is matched there. */
    if (vid == FF802_VID_RME)
        return FF802_MODE_RME_OTHER;
    return FF802_MODE_NONE;
}

int ff802_mode_is_ff802(ff802_mode mode)
{
    return mode == FF802_MODE_PROPRIETARY || mode == FF802_MODE_CLASS_COMPLIANT;
}

const char *ff802_mode_name(ff802_mode mode)
{
    switch (mode) {
    case FF802_MODE_PROPRIETARY:     return "proprietary";
    case FF802_MODE_CLASS_COMPLIANT: return "class-compliant";
    case FF802_MODE_RME_OTHER:       return "rme-other";
    case FF802_MODE_NONE:            break;
    }
    return "none";
}

const char *ff802_mode_description(ff802_mode mode)
{
    switch (mode) {
    case FF802_MODE_PROPRIETARY:
        return "Fireface 802, proprietary USB mode";
    case FF802_MODE_CLASS_COMPLIANT:
        return "Fireface 802, class-compliant mode (ID not yet confirmed on Linux)";
    case FF802_MODE_RME_OTHER:
        return "other RME device (not supported)";
    case FF802_MODE_NONE:
        break;
    }
    return "not an RME device";
}
