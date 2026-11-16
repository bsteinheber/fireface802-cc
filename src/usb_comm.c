/*
 * libusb USB backend: the only module that opens devices, claims
 * interfaces and performs transfers (see usb_comm.h).
 */
#include "ff802/usb_comm.h"
#include "usb_internal.h"

#include <stdlib.h>
#include <string.h>

#include <libusb.h>

#ifndef FF802_ENABLE_USB_WRITE
#define FF802_ENABLE_USB_WRITE 0
#endif

typedef struct comm_state {
    libusb_context *ctx;
    libusb_device **list;   /* kept alive until exit for open/get_layout */
    ssize_t         count;
} comm_state;

static ff802_usb_err map_err(int r)
{
    switch (r) {
    case LIBUSB_SUCCESS:             return FF802_USB_OK;
    case LIBUSB_ERROR_ACCESS:        return FF802_USB_ERR_ACCESS;
    case LIBUSB_ERROR_NO_DEVICE:     return FF802_USB_ERR_NO_DEVICE;
    case LIBUSB_ERROR_NOT_FOUND:     return FF802_USB_ERR_NOT_FOUND;
    case LIBUSB_ERROR_BUSY:          return FF802_USB_ERR_BUSY;
    case LIBUSB_ERROR_TIMEOUT:       return FF802_USB_ERR_TIMEOUT;
    case LIBUSB_ERROR_PIPE:          return FF802_USB_ERR_PIPE;
    case LIBUSB_ERROR_OVERFLOW:      return FF802_USB_ERR_OVERFLOW;
    case LIBUSB_ERROR_IO:            return FF802_USB_ERR_IO;
    case LIBUSB_ERROR_NO_MEM:        return FF802_USB_ERR_NO_MEM;
    case LIBUSB_ERROR_NOT_SUPPORTED: return FF802_USB_ERR_NOT_SUPPORTED;
    default:                         return FF802_USB_ERR_OTHER;
    }
}

int ff802_usb_comm_write_enabled(void)
{
    return FF802_ENABLE_USB_WRITE ? 1 : 0;
}

static libusb_device *find_dev(comm_state *s, const ff802_usb_dev *rec)
{
    for (ssize_t i = 0; i < s->count; i++) {
        libusb_device *d = s->list[i];
        if (libusb_get_bus_number(d) == rec->bus &&
            libusb_get_device_address(d) == rec->address)
            return d;
    }
    return NULL;
}

static ff802_usb_err c_list(void *self, ff802_usb_dev **devs, size_t *n)
{
    comm_state *s = self;

    *devs = NULL;
    *n = 0;
    if (s->list != NULL) {
        libusb_free_device_list(s->list, 1);
        s->list = NULL;
        s->count = 0;
    }

    ssize_t cnt = libusb_get_device_list(s->ctx, &s->list);
    if (cnt < 0) {
        s->list = NULL;
        return map_err((int)cnt);
    }
    s->count = cnt;

    ff802_usb_dev *recs = calloc(cnt > 0 ? (size_t)cnt : 1u, sizeof *recs);
    if (recs == NULL)
        return FF802_USB_ERR_NO_MEM;

    size_t used = 0;
    for (ssize_t i = 0; i < cnt; i++) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(s->list[i], &desc) != LIBUSB_SUCCESS)
            continue;
        ff802_usb_fill_record(s->list[i], &desc, &recs[used++]);
    }
    *devs = recs;
    *n = used;
    return FF802_USB_OK;
}

static void c_free(void *self, ff802_usb_dev *devs)
{
    (void)self;
    free(devs);
}

/* Reads the cached active configuration descriptor; no device access. */
static ff802_usb_err c_layout(void *self, const ff802_usb_dev *rec,
                              uint8_t iface, uint8_t alt,
                              ff802_dev_layout *out)
{
    comm_state *s = self;
    libusb_device *dev = find_dev(s, rec);
    struct libusb_config_descriptor *cfg = NULL;

    memset(out, 0, sizeof *out);
    if (dev == NULL)
        return FF802_USB_ERR_NO_DEVICE;

    int r = libusb_get_active_config_descriptor(dev, &cfg);
    if (r == LIBUSB_ERROR_NOT_FOUND)
        return FF802_USB_OK;   /* unconfigured: config_value stays 0 */
    if (r != LIBUSB_SUCCESS)
        return map_err(r);

    out->config_value = cfg->bConfigurationValue;
    for (int i = 0; i < cfg->bNumInterfaces && !out->has_interface; i++) {
        const struct libusb_interface *itf = &cfg->interface[i];
        for (int a = 0; a < itf->num_altsetting; a++) {
            const struct libusb_interface_descriptor *d = &itf->altsetting[a];
            if (d->bInterfaceNumber != iface || d->bAlternateSetting != alt)
                continue;
            out->has_interface = 1;
            unsigned n = d->bNumEndpoints;
            if (n > FF802_MAX_ENDPOINTS)
                n = FF802_MAX_ENDPOINTS;
            for (unsigned e = 0; e < n; e++) {
                out->eps[e].address = d->endpoint[e].bEndpointAddress;
                out->eps[e].attributes = d->endpoint[e].bmAttributes;
                out->eps[e].max_packet = d->endpoint[e].wMaxPacketSize;
            }
            out->num_endpoints = (uint8_t)n;
            break;
        }
    }
    libusb_free_config_descriptor(cfg);
    return FF802_USB_OK;
}

static ff802_usb_err c_open(void *self, const ff802_usb_dev *rec,
                            ff802_usb_handle *handle)
{
    comm_state *s = self;
    libusb_device *dev = find_dev(s, rec);
    libusb_device_handle *h = NULL;

    *handle = NULL;
    if (dev == NULL)
        return FF802_USB_ERR_NO_DEVICE;

    int r = libusb_open(dev, &h);
    if (r != LIBUSB_SUCCESS)
        return map_err(r);
    *handle = h;
    return FF802_USB_OK;
}

static void c_close(void *self, ff802_usb_handle handle)
{
    (void)self;
    if (handle != NULL)
        libusb_close(handle);
}

static ff802_usb_err c_kdrv(void *self, ff802_usb_handle handle,
                            uint8_t iface, int *active)
{
    (void)self;
    int r = libusb_kernel_driver_active(handle, iface);
    if (r < 0)
        return map_err(r);
    *active = r ? 1 : 0;
    return FF802_USB_OK;
}

#if FF802_ENABLE_USB_WRITE

static ff802_usb_err c_claim(void *self, ff802_usb_handle handle,
                             uint8_t iface)
{
    (void)self;
    return map_err(libusb_claim_interface(handle, iface));
}

static ff802_usb_err c_release(void *self, ff802_usb_handle handle,
                               uint8_t iface)
{
    (void)self;
    return map_err(libusb_release_interface(handle, iface));
}

static ff802_usb_err c_write(void *self, ff802_usb_handle handle,
                             uint8_t endpoint, const uint8_t *data,
                             size_t len, unsigned timeout_ms,
                             size_t *transferred)
{
    (void)self;
    unsigned char buf[64];
    int done = 0;

    *transferred = 0;
    if (len > sizeof buf)
        return FF802_USB_ERR_OVERFLOW;
    memcpy(buf, data, len);   /* libusb takes a non-const buffer */

    /* Single synchronous transfer; the caller never retries. */
    int r = libusb_bulk_transfer(handle, endpoint, buf, (int)len, &done,
                                 timeout_ms);
    if (done > 0)
        *transferred = (size_t)done;
    return map_err(r);
}

#else /* !FF802_ENABLE_USB_WRITE */

static ff802_usb_err c_claim(void *self, ff802_usb_handle handle,
                             uint8_t iface)
{
    (void)self; (void)handle; (void)iface;
    return FF802_USB_ERR_WRITE_DISABLED;
}

static ff802_usb_err c_release(void *self, ff802_usb_handle handle,
                               uint8_t iface)
{
    (void)self; (void)handle; (void)iface;
    return FF802_USB_ERR_WRITE_DISABLED;
}

static ff802_usb_err c_write(void *self, ff802_usb_handle handle,
                             uint8_t endpoint, const uint8_t *data,
                             size_t len, unsigned timeout_ms,
                             size_t *transferred)
{
    (void)self; (void)handle; (void)endpoint; (void)data; (void)len;
    (void)timeout_ms;
    *transferred = 0;
    return FF802_USB_ERR_WRITE_DISABLED;
}

#endif /* FF802_ENABLE_USB_WRITE */

ff802_usb_err ff802_usb_comm_init(ff802_usb_backend *be)
{
    memset(be, 0, sizeof *be);

    comm_state *s = calloc(1, sizeof *s);
    if (s == NULL)
        return FF802_USB_ERR_NO_MEM;

    int r = libusb_init(&s->ctx);
    if (r != LIBUSB_SUCCESS) {
        free(s);
        return map_err(r);
    }

    be->self = s;
    be->write_enabled = ff802_usb_comm_write_enabled();
    be->list_devices = c_list;
    be->free_devices = c_free;
    be->get_layout = c_layout;
    be->open = c_open;
    be->close = c_close;
    be->kernel_driver_active = c_kdrv;
    be->claim = c_claim;
    be->release = c_release;
    be->bulk_write = c_write;
    return FF802_USB_OK;
}

void ff802_usb_comm_exit(ff802_usb_backend *be)
{
    comm_state *s = be->self;
    if (s == NULL)
        return;
    if (s->list != NULL)
        libusb_free_device_list(s->list, 1);
    libusb_exit(s->ctx);
    free(s);
    memset(be, 0, sizeof *be);
}
