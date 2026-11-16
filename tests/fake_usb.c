#include "fake_usb.h"

#include <string.h>

static void trace(fake_usb *f, const char *what)
{
    if (f->trace[0] != '\0')
        strncat(f->trace, ",", sizeof f->trace - strlen(f->trace) - 1);
    strncat(f->trace, what, sizeof f->trace - strlen(f->trace) - 1);
}

void fake_usb_good_layout(ff802_dev_layout *l)
{
    memset(l, 0, sizeof *l);
    l->config_value = 1;
    l->has_interface = 1;
    l->num_endpoints = 2;
    l->eps[0] = (ff802_ep_info){ .address = 0x8C, .attributes = 0x02,
                                 .max_packet = 512 };
    l->eps[1] = (ff802_ep_info){ .address = 0x0C, .attributes = 0x02,
                                 .max_packet = 512 };
}

void fake_usb_init(fake_usb *f)
{
    memset(f, 0, sizeof *f);
    f->devs[0] = (ff802_usb_dev){ .bus = 3, .address = 18, .vid = 0x2A39,
                                  .pid = 0x3FCD, .bcd_device = 0x0001,
                                  .port_count = 1, .ports = { 2 } };
    f->devs[1] = (ff802_usb_dev){ .bus = 1, .address = 2, .vid = 0x046D,
                                  .pid = 0xC31C, .port_count = 1,
                                  .ports = { 1 } };
    f->ndevs = 2;
    fake_usb_good_layout(&f->layout);
    f->write_enabled = 1;
    f->write_transferred = 4;
}

static ff802_usb_err f_list(void *self, ff802_usb_dev **devs, size_t *n)
{
    fake_usb *f = self;
    trace(f, "list");
    f->n_list++;
    if (f->err_list != FF802_USB_OK)
        return f->err_list;
    *devs = f->devs;
    *n = f->ndevs;
    return FF802_USB_OK;
}

static void f_free(void *self, ff802_usb_dev *devs)
{
    fake_usb *f = self;
    (void)devs;
    trace(f, "free");
    f->n_free++;
}

static ff802_usb_err f_layout(void *self, const ff802_usb_dev *dev,
                              uint8_t iface, uint8_t alt,
                              ff802_dev_layout *out)
{
    fake_usb *f = self;
    (void)dev;
    trace(f, "layout");
    f->layout_iface = iface;
    f->layout_alt = alt;
    if (f->err_layout != FF802_USB_OK)
        return f->err_layout;
    *out = f->layout;
    return FF802_USB_OK;
}

static ff802_usb_err f_open(void *self, const ff802_usb_dev *dev,
                            ff802_usb_handle *h)
{
    fake_usb *f = self;
    trace(f, "open");
    f->n_open++;
    if (f->err_open != FF802_USB_OK)
        return f->err_open;
    f->opened_dev = *dev;
    f->open_handles++;
    *h = f;
    return FF802_USB_OK;
}

static void f_close(void *self, ff802_usb_handle h)
{
    fake_usb *f = self;
    (void)h;
    trace(f, "close");
    f->n_close++;
    f->open_handles--;
}

static ff802_usb_err f_kdrv(void *self, ff802_usb_handle h, uint8_t iface,
                            int *active)
{
    fake_usb *f = self;
    (void)h;
    trace(f, "kdrv");
    f->last_iface = iface;
    if (f->err_kdrv != FF802_USB_OK)
        return f->err_kdrv;
    *active = f->kernel_driver_active;
    return FF802_USB_OK;
}

static ff802_usb_err f_claim(void *self, ff802_usb_handle h, uint8_t iface)
{
    fake_usb *f = self;
    (void)h;
    trace(f, "claim");
    f->n_claim++;
    f->last_iface = iface;
    if (f->err_claim != FF802_USB_OK)
        return f->err_claim;
    f->claimed++;
    return FF802_USB_OK;
}

static ff802_usb_err f_release(void *self, ff802_usb_handle h, uint8_t iface)
{
    fake_usb *f = self;
    (void)h;
    trace(f, "release");
    f->n_release++;
    f->last_iface = iface;
    f->claimed--;
    return f->err_release;
}

static ff802_usb_err f_write(void *self, ff802_usb_handle h, uint8_t ep,
                             const uint8_t *data, size_t len,
                             unsigned timeout_ms, size_t *transferred)
{
    fake_usb *f = self;
    (void)h;
    trace(f, "write");
    f->n_write++;
    f->write_ep = ep;
    f->write_len = len;
    f->write_timeout = timeout_ms;
    if (len <= sizeof f->write_data)
        memcpy(f->write_data, data, len);
    *transferred = f->write_transferred;
    return f->err_write;
}

ff802_usb_backend fake_usb_backend(fake_usb *f)
{
    ff802_usb_backend be = {
        .self = f,
        .write_enabled = f->write_enabled,
        .list_devices = f_list,
        .free_devices = f_free,
        .get_layout = f_layout,
        .open = f_open,
        .close = f_close,
        .kernel_driver_active = f_kdrv,
        .claim = f_claim,
        .release = f_release,
        .bulk_write = f_write,
    };
    return be;
}
