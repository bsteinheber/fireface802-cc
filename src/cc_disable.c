#include "ff802/cc_disable.h"

#include <stdarg.h>
#include <string.h>

#include "ff802/device_id.h"

static void logf_(FILE *log, const char *fmt, ...)
{
    if (log == NULL)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(log, fmt, ap);
    va_end(ap);
}

static ff802_sw_status fail(ff802_sw_result *res, ff802_sw_status st,
                            ff802_sw_step step, ff802_usb_err err)
{
    res->status = st;
    res->failed_step = step;
    res->usb_err = err;
    return st;
}

ff802_layout_status ff802_check_layout_disable(const ff802_dev_layout *layout)
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
        if (ep->address != FF802_DIS_EP_OUT)
            continue;
        if ((ep->attributes & FF802_USB_EP_TYPE_MASK) != FF802_USB_EP_TYPE_BULK)
            return FF802_LAYOUT_NOT_BULK;
        if ((ep->max_packet & 0x07FFu) != FF802_DIS_MAX_PACKET)
            return FF802_LAYOUT_MAX_PACKET;
        return FF802_LAYOUT_OK;
    }
    return FF802_LAYOUT_NO_ENDPOINT;
}

const char *ff802_disable_layout_str(ff802_layout_status st)
{
    switch (st) {
    case FF802_LAYOUT_OK:           return "layout as expected";
    case FF802_LAYOUT_WRONG_CONFIG: return "active USB configuration is not 1";
    case FF802_LAYOUT_NO_INTERFACE: return "interface 3 / alternate setting 0 not found";
    case FF802_LAYOUT_NO_ENDPOINT:  return "bulk OUT endpoint 0x07 not found";
    case FF802_LAYOUT_NOT_BULK:     return "endpoint 0x07 is not a bulk endpoint";
    case FF802_LAYOUT_MAX_PACKET:   return "endpoint 0x07 max packet size is not 512 "
                                           "(device not running at high speed?)";
    }
    return "unknown layout status";
}

static ff802_sw_status select_target(const ff802_usb_dev *devs, size_t n,
                                     const ff802_dis_options *opt,
                                     ff802_sw_result *res, FILE *log)
{
    size_t candidates = 0;
    const ff802_usb_dev *pick = NULL;

    for (size_t i = 0; i < n; i++) {
        const ff802_usb_dev *d = &devs[i];
        ff802_mode mode = ff802_classify(d->vid, d->pid);
        if (opt->select_location &&
            (d->bus != opt->bus || d->address != opt->address))
            continue;
        if (mode == FF802_MODE_PROPRIETARY)
            logf_(log, "Note: device on bus %03u address %03u is already in "
                       "proprietary mode.\n", (unsigned)d->bus, (unsigned)d->address);
        if (mode != FF802_MODE_CLASS_COMPLIANT)
            continue;
        candidates++;
        if (pick == NULL)
            pick = d;
    }
    if (candidates == 0)
        return fail(res, FF802_SW_NOT_FOUND, FF802_STEP_SELECT, FF802_USB_OK);
    if (candidates > 1)
        return fail(res, FF802_SW_AMBIGUOUS, FF802_STEP_SELECT, FF802_USB_OK);
    res->target = *pick;
    res->have_target = 1;
    return FF802_SW_OK;
}

static ff802_sw_status map_open_err(ff802_usb_err e)
{
    switch (e) {
    case FF802_USB_ERR_ACCESS:    return FF802_SW_ACCESS_DENIED;
    case FF802_USB_ERR_NO_DEVICE: return FF802_SW_DISCONNECTED;
    default:                      return FF802_SW_OPEN_FAILED;
    }
}

static ff802_sw_status map_claim_err(ff802_usb_err e)
{
    switch (e) {
    case FF802_USB_ERR_BUSY:           return FF802_SW_INTERFACE_BUSY;
    case FF802_USB_ERR_ACCESS:         return FF802_SW_ACCESS_DENIED;
    case FF802_USB_ERR_NO_DEVICE:      return FF802_SW_DISCONNECTED;
    case FF802_USB_ERR_WRITE_DISABLED: return FF802_SW_WRITE_DISABLED;
    default:                           return FF802_SW_CLAIM_FAILED;
    }
}

static ff802_sw_status map_write_err(ff802_usb_err e)
{
    switch (e) {
    case FF802_USB_ERR_TIMEOUT:        return FF802_SW_TIMEOUT;
    case FF802_USB_ERR_PIPE:           return FF802_SW_STALL;
    case FF802_USB_ERR_NO_DEVICE:      return FF802_SW_DISCONNECTED;
    case FF802_USB_ERR_WRITE_DISABLED: return FF802_SW_WRITE_DISABLED;
    default:                           return FF802_SW_WRITE_FAILED;
    }
}

static ff802_sw_status run_opened(const ff802_usb_backend *be,
                                  const ff802_dis_options *opt,
                                  ff802_usb_handle h, FILE *log,
                                  ff802_sw_result *res)
{
    int active = 0;
    ff802_usb_err e = be->kernel_driver_active(be->self, h, FF802_DIS_INTERFACE, &active);
    if (e != FF802_USB_OK)
        return fail(res, FF802_SW_DRIVER_CHECK_FAILED, FF802_STEP_DRIVER_CHECK, e);
    logf_(log, active ? "Kernel driver bound to interface %u (snd-usb-audio); "
                        "libusb detaches it on claim.\n"
                      : "No kernel driver bound to interface %u.\n",
          (unsigned)FF802_DIS_INTERFACE);

    if (!be->write_enabled)
        return fail(res, FF802_SW_WRITE_DISABLED, FF802_STEP_WRITE_GATE,
                    FF802_USB_ERR_WRITE_DISABLED);

    uint8_t cmd[FF802_CC_DISABLE_MIDI_SIZE];
    if (ff802_cc_disable_midi(cmd) != FF802_OK)
        return fail(res, FF802_SW_INTERNAL, FF802_STEP_WRITE, FF802_USB_OK);

    if (opt->confirm == NULL ||
        !opt->confirm(opt->confirm_user, &res->target, cmd, sizeof cmd))
        return fail(res, FF802_SW_ABORTED, FF802_STEP_CONFIRM, FF802_USB_OK);

    if (be->claim_detach == NULL)
        return fail(res, FF802_SW_INTERNAL, FF802_STEP_CLAIM, FF802_USB_OK);
    e = be->claim_detach(be->self, h, FF802_DIS_INTERFACE);
    if (e != FF802_USB_OK)
        return fail(res, map_claim_err(e), FF802_STEP_CLAIM, e);
    logf_(log, "Interface %u claimed.\n", (unsigned)FF802_DIS_INTERFACE);

    /* Exactly one transfer attempt. No retry on any error. */
    size_t transferred = 0;
    res->write_attempted = 1;
    e = be->bulk_write(be->self, h, FF802_DIS_EP_OUT, cmd, sizeof cmd,
                       FF802_DIS_TIMEOUT_MS, &transferred);
    res->transferred = transferred;

    ff802_sw_status st = FF802_SW_OK;
    if (e != FF802_USB_OK) {
        if (e == FF802_USB_ERR_TIMEOUT && transferred > 0 && transferred < sizeof cmd)
            st = fail(res, FF802_SW_SHORT_WRITE, FF802_STEP_WRITE, e);
        else
            st = fail(res, map_write_err(e), FF802_STEP_WRITE, e);
    } else if (transferred != sizeof cmd) {
        st = fail(res, FF802_SW_SHORT_WRITE, FF802_STEP_WRITE, FF802_USB_OK);
    } else {
        logf_(log, "Transferred %zu bytes to endpoint 0x%02X.\n",
              transferred, (unsigned)FF802_DIS_EP_OUT);
    }

    /* The unit may already have left CC mode: a failing release is expected. */
    e = be->release(be->self, h, FF802_DIS_INTERFACE);
    if (e != FF802_USB_OK) {
        res->release_failed = 1;
        res->release_err = e;
        logf_(log, "Releasing interface %u: %s%s\n", (unsigned)FF802_DIS_INTERFACE,
              ff802_usb_err_str(e),
              st == FF802_SW_OK ? " (expected once the unit has switched)" : "");
    } else {
        logf_(log, "Interface %u released.\n", (unsigned)FF802_DIS_INTERFACE);
    }
    return st;
}

ff802_sw_status ff802_disable_run(const ff802_usb_backend *be,
                                  const ff802_dis_options *opt,
                                  FILE *log, ff802_sw_result *res)
{
    if (res == NULL)
        return FF802_SW_INTERNAL;
    memset(res, 0, sizeof *res);
    res->status = FF802_SW_OK;
    if (be == NULL || opt == NULL)
        return fail(res, FF802_SW_INTERNAL, FF802_STEP_NONE, FF802_USB_OK);

    ff802_usb_dev *devs = NULL;
    size_t n = 0;
    ff802_usb_err e = be->list_devices(be->self, &devs, &n);
    if (e != FF802_USB_OK)
        return fail(res, FF802_SW_LIST_FAILED, FF802_STEP_LIST, e);
    ff802_sw_status st = select_target(devs, n, opt, res, log);
    be->free_devices(be->self, devs);
    if (st != FF802_SW_OK)
        return st;
    logf_(log, "Target: Bus %03u Device %03u (ID %04x:%04x)\n",
          (unsigned)res->target.bus, (unsigned)res->target.address,
          (unsigned)res->target.vid, (unsigned)res->target.pid);

    ff802_dev_layout layout;
    memset(&layout, 0, sizeof layout);
    e = be->get_layout(be->self, &res->target, FF802_DIS_INTERFACE,
                       FF802_DIS_ALTSETTING, &layout);
    if (e != FF802_USB_OK)
        return fail(res, FF802_SW_LAYOUT_FAILED, FF802_STEP_LAYOUT, e);
    res->layout = ff802_check_layout_disable(&layout);
    if (res->layout != FF802_LAYOUT_OK)
        return fail(res, FF802_SW_UNEXPECTED_LAYOUT, FF802_STEP_LAYOUT, FF802_USB_OK);
    logf_(log, "Descriptor OK: configuration %u, interface %u, bulk OUT 0x%02X, "
               "%u bytes max packet.\n", (unsigned)FF802_USB_CONFIG,
          (unsigned)FF802_DIS_INTERFACE, (unsigned)FF802_DIS_EP_OUT,
          (unsigned)FF802_DIS_MAX_PACKET);

    ff802_usb_handle h = NULL;
    e = be->open(be->self, &res->target, &h);
    if (e != FF802_USB_OK)
        return fail(res, map_open_err(e), FF802_STEP_OPEN, e);
    logf_(log, "Device opened.\n");
    st = run_opened(be, opt, h, log, res);
    be->close(be->self, h);
    logf_(log, "Device closed.\n");
    return st;
}

const char *ff802_disable_status_str(ff802_sw_status st)
{
    switch (st) {
    case FF802_SW_NOT_FOUND:      return "no Fireface 802 in class-compliant mode found";
    case FF802_SW_KERNEL_DRIVER:  return "unexpected kernel driver on interface 3";
    case FF802_SW_INTERFACE_BUSY: return "interface 3 is in use by another program";
    case FF802_SW_CLAIM_FAILED:   return "could not claim interface 3";
    case FF802_SW_STALL:          return "endpoint 0x07 stalled";
    case FF802_SW_INTERNAL:       return "internal error (detach claim not available?)";
    default:                      break;
    }
    return ff802_sw_status_str(st);
}

int ff802_disable_confirm_answer_ok(const char *line)
{
    if (line == NULL)
        return 0;
    size_t wlen = strlen(FF802_DISABLE_CONFIRM_WORD);
    if (strncmp(line, FF802_DISABLE_CONFIRM_WORD, wlen) != 0)
        return 0;
    const char *rest = line + wlen;
    return strcmp(rest, "") == 0 || strcmp(rest, "\n") == 0 ||
           strcmp(rest, "\r\n") == 0;
}
