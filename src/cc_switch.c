#include "ff802/cc_switch.h"

#include <stdarg.h>
#include <string.h>

#include "ff802/cc_mode.h"
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

/* Choose the target device among the enumerated ones. */
static ff802_sw_status select_target(const ff802_usb_dev *devs, size_t n,
                                     const ff802_sw_options *opt,
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

        if (mode == FF802_MODE_CLASS_COMPLIANT)
            logf_(log, "Note: device on bus %03u address %03u is already in "
                       "class-compliant mode.\n",
                  (unsigned)d->bus, (unsigned)d->address);
        if (mode != FF802_MODE_PROPRIETARY)
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

/* Steps after the device has been opened. Never closes the handle. */
static ff802_sw_status run_opened(const ff802_usb_backend *be,
                                  const ff802_sw_options *opt,
                                  ff802_usb_handle h, FILE *log,
                                  ff802_sw_result *res)
{
    ff802_usb_err e;
    int active = 0;

    e = be->kernel_driver_active(be->self, h, FF802_USB_INTERFACE, &active);
    if (e != FF802_USB_OK)
        return fail(res, FF802_SW_DRIVER_CHECK_FAILED, FF802_STEP_DRIVER_CHECK, e);
    if (active)
        return fail(res, FF802_SW_KERNEL_DRIVER, FF802_STEP_DRIVER_CHECK,
                    FF802_USB_OK);
    logf_(log, "No kernel driver bound to interface %u.\n",
          (unsigned)FF802_USB_INTERFACE);

    if (opt->action == FF802_SW_CHECK) {
        logf_(log, "Check only: interface not claimed, nothing sent.\n");
        return FF802_SW_OK;
    }

    if (!be->write_enabled)
        return fail(res, FF802_SW_WRITE_DISABLED, FF802_STEP_WRITE_GATE,
                    FF802_USB_ERR_WRITE_DISABLED);

    uint8_t cmd[FF802_REG_WORD_SIZE];
    if (ff802_cc_command(true, cmd) != FF802_OK)
        return fail(res, FF802_SW_INTERNAL, FF802_STEP_WRITE, FF802_USB_OK);

    if (opt->confirm == NULL ||
        !opt->confirm(opt->confirm_user, &res->target, cmd))
        return fail(res, FF802_SW_ABORTED, FF802_STEP_CONFIRM, FF802_USB_OK);

    e = be->claim(be->self, h, FF802_USB_INTERFACE);
    if (e != FF802_USB_OK)
        return fail(res, map_claim_err(e), FF802_STEP_CLAIM, e);
    logf_(log, "Interface %u claimed.\n", (unsigned)FF802_USB_INTERFACE);

    /* Exactly one transfer attempt. No retry on any error. */
    size_t transferred = 0;
    res->write_attempted = 1;
    e = be->bulk_write(be->self, h, FF802_USB_EP_OUT, cmd, sizeof cmd,
                       FF802_USB_TIMEOUT_MS, &transferred);
    res->transferred = transferred;

    ff802_sw_status st = FF802_SW_OK;
    if (e != FF802_USB_OK) {
        /* A timeout may still have moved some bytes. */
        if (e == FF802_USB_ERR_TIMEOUT && transferred > 0 &&
            transferred < sizeof cmd)
            st = fail(res, FF802_SW_SHORT_WRITE, FF802_STEP_WRITE, e);
        else
            st = fail(res, map_write_err(e), FF802_STEP_WRITE, e);
    } else if (transferred != sizeof cmd) {
        st = fail(res, FF802_SW_SHORT_WRITE, FF802_STEP_WRITE, FF802_USB_OK);
    } else {
        logf_(log, "Transferred %zu bytes to endpoint 0x%02X.\n",
              transferred, (unsigned)FF802_USB_EP_OUT);
    }

    e = be->release(be->self, h, FF802_USB_INTERFACE);
    if (e != FF802_USB_OK) {
        res->release_failed = 1;
        res->release_err = e;
        logf_(log, "Warning: releasing interface %u failed: %s\n",
              (unsigned)FF802_USB_INTERFACE, ff802_usb_err_str(e));
    } else {
        logf_(log, "Interface %u released.\n", (unsigned)FF802_USB_INTERFACE);
    }
    return st;
}

ff802_sw_status ff802_switch_run(const ff802_usb_backend *be,
                                 const ff802_sw_options *opt,
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
    e = be->get_layout(be->self, &res->target, FF802_USB_INTERFACE,
                       FF802_USB_ALTSETTING, &layout);
    if (e != FF802_USB_OK)
        return fail(res, FF802_SW_LAYOUT_FAILED, FF802_STEP_LAYOUT, e);
    res->layout = ff802_check_layout(&layout);
    if (res->layout != FF802_LAYOUT_OK)
        return fail(res, FF802_SW_UNEXPECTED_LAYOUT, FF802_STEP_LAYOUT,
                    FF802_USB_OK);
    logf_(log, "Descriptor OK: configuration %u, interface %u, "
               "bulk OUT 0x%02X, %u bytes max packet.\n",
          (unsigned)FF802_USB_CONFIG, (unsigned)FF802_USB_INTERFACE,
          (unsigned)FF802_USB_EP_OUT, (unsigned)FF802_USB_MAX_PACKET);

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

const char *ff802_sw_status_str(ff802_sw_status st)
{
    switch (st) {
    case FF802_SW_OK:                  return "success";
    case FF802_SW_LIST_FAILED:         return "USB enumeration failed";
    case FF802_SW_NOT_FOUND:           return "no Fireface 802 in proprietary mode found";
    case FF802_SW_AMBIGUOUS:           return "more than one Fireface 802 found";
    case FF802_SW_LAYOUT_FAILED:       return "could not read USB descriptors";
    case FF802_SW_UNEXPECTED_LAYOUT:   return "unexpected USB descriptor layout";
    case FF802_SW_ACCESS_DENIED:       return "access to the USB device denied";
    case FF802_SW_DISCONNECTED:        return "device was disconnected";
    case FF802_SW_OPEN_FAILED:         return "could not open the USB device";
    case FF802_SW_KERNEL_DRIVER:       return "a kernel driver is bound to interface 1";
    case FF802_SW_DRIVER_CHECK_FAILED: return "could not query kernel driver state";
    case FF802_SW_WRITE_DISABLED:      return "USB writes are disabled in this build";
    case FF802_SW_ABORTED:             return "aborted, not confirmed by user";
    case FF802_SW_INTERFACE_BUSY:      return "interface 1 is in use by another program";
    case FF802_SW_CLAIM_FAILED:        return "could not claim interface 1";
    case FF802_SW_TIMEOUT:             return "bulk transfer timed out";
    case FF802_SW_SHORT_WRITE:         return "incomplete bulk transfer";
    case FF802_SW_STALL:               return "endpoint 0x0C stalled";
    case FF802_SW_WRITE_FAILED:        return "bulk transfer failed";
    case FF802_SW_INTERNAL:            break;
    }
    return "internal error";
}

const char *ff802_sw_status_hint(ff802_sw_status st)
{
    switch (st) {
    case FF802_SW_NOT_FOUND:
        return "Check with ff802-detect. A device already in class-compliant "
               "mode cannot be switched by this tool.";
    case FF802_SW_AMBIGUOUS:
        return "Select a device with --device BUS:ADDRESS (see ff802-detect).";
    case FF802_SW_ACCESS_DENIED:
        return "Your user has no write access to the USB device node. A udev "
               "rule is needed (see docs/usb_comm.md); it is not installed "
               "automatically. Do not run this tool as root.";
    case FF802_SW_KERNEL_DRIVER:
        return "This tool never detaches kernel drivers. Unbind the driver "
               "manually if you know what you are doing.";
    case FF802_SW_INTERFACE_BUSY:
        return "Close other programs using the device and try again.";
    case FF802_SW_WRITE_DISABLED:
        return "Rebuild with -DFF802_ENABLE_USB_WRITE=ON to allow the transfer.";
    case FF802_SW_TIMEOUT:
    case FF802_SW_SHORT_WRITE:
    case FF802_SW_STALL:
    case FF802_SW_WRITE_FAILED:
        return "It is unknown whether the device received the command. "
               "Nothing was retried. Power-cycle the device and check its "
               "mode with ff802-detect before trying again.";
    case FF802_SW_DISCONNECTED:
        return "Reconnect the device and check its mode with ff802-detect.";
    default:
        break;
    }
    return NULL;
}

int ff802_sw_exit_code(ff802_sw_status st)
{
    switch (st) {
    case FF802_SW_OK:
        return FF802_CC_EXIT_OK;
    case FF802_SW_NOT_FOUND:
    case FF802_SW_AMBIGUOUS:
        return FF802_CC_EXIT_NOT_FOUND;
    case FF802_SW_WRITE_DISABLED:
    case FF802_SW_ABORTED:
        return FF802_CC_EXIT_NOT_SENT;
    case FF802_SW_TIMEOUT:
    case FF802_SW_SHORT_WRITE:
    case FF802_SW_STALL:
    case FF802_SW_WRITE_FAILED:
        return FF802_CC_EXIT_WRITE_FAILED;
    case FF802_SW_DISCONNECTED:
    case FF802_SW_LIST_FAILED:
    case FF802_SW_LAYOUT_FAILED:
    case FF802_SW_UNEXPECTED_LAYOUT:
    case FF802_SW_ACCESS_DENIED:
    case FF802_SW_OPEN_FAILED:
    case FF802_SW_KERNEL_DRIVER:
    case FF802_SW_DRIVER_CHECK_FAILED:
    case FF802_SW_INTERFACE_BUSY:
    case FF802_SW_CLAIM_FAILED:
    case FF802_SW_INTERNAL:
        break;
    }
    return FF802_CC_EXIT_CHECK_FAILED;
}

int ff802_confirm_answer_ok(const char *line)
{
    if (line == NULL)
        return 0;
    size_t wlen = strlen(FF802_CONFIRM_WORD);
    if (strncmp(line, FF802_CONFIRM_WORD, wlen) != 0)
        return 0;
    const char *rest = line + wlen;
    return strcmp(rest, "") == 0 || strcmp(rest, "\n") == 0 ||
           strcmp(rest, "\r\n") == 0;
}

int ff802_parse_location(const char *s, uint8_t *bus, uint8_t *address)
{
    unsigned long vals[2] = { 0, 0 };
    const char *p = s;

    if (s == NULL || bus == NULL || address == NULL)
        return -1;

    for (int part = 0; part < 2; part++) {
        unsigned digits = 0;
        while (*p >= '0' && *p <= '9') {
            vals[part] = vals[part] * 10u + (unsigned long)(*p - '0');
            if (vals[part] > 999u)
                return -1;
            p++;
            digits++;
        }
        if (digits == 0)
            return -1;
        if (part == 0) {
            if (*p != ':')
                return -1;
            p++;
        }
    }
    if (*p != '\0')
        return -1;
    if (vals[0] < 1u || vals[0] > 255u || vals[1] < 1u || vals[1] > 127u)
        return -1;

    *bus = (uint8_t)vals[0];
    *address = (uint8_t)vals[1];
    return 0;
}
