/*
 * Read-only libusb enumeration.
 *
 * Only the following libusb calls are used: context init/exit, device
 * list get/free, and the descriptor/topology getters below. Since libusb
 * 1.0.16 the device descriptor is cached at enumeration time, so reading
 * it causes no bus traffic. The test check_forbidden_usb_calls enforces
 * that no device-opening or transfer API is used in this file.
 */
#include "ff802/usb_enum.h"
#include "usb_internal.h"

#include <stdlib.h>

#include <libusb.h>

void ff802_usb_fill_record(libusb_device *dev,
                           const struct libusb_device_descriptor *desc,
                           ff802_usb_dev *rec)
{
    uint8_t ports[FF802_MAX_PORT_DEPTH];

    rec->bus = libusb_get_bus_number(dev);
    rec->address = libusb_get_device_address(dev);
    rec->vid = desc->idVendor;
    rec->pid = desc->idProduct;
    rec->bcd_device = desc->bcdDevice;

    int depth = libusb_get_port_numbers(dev, ports, (int)sizeof ports);
    rec->port_count = 0;
    if (depth > 0) {
        rec->port_count = (uint8_t)depth;
        for (int i = 0; i < depth; i++)
            rec->ports[i] = ports[i];
    }
}

int ff802_usb_enumerate(ff802_usb_dev **devs, size_t *count)
{
    libusb_context *ctx = NULL;
    libusb_device **list = NULL;
    ff802_usb_dev *recs = NULL;
    size_t used = 0;
    int err;

    if (devs == NULL || count == NULL)
        return LIBUSB_ERROR_INVALID_PARAM;
    *devs = NULL;
    *count = 0;

    err = libusb_init(&ctx);
    if (err < 0)
        return err;

    ssize_t n = libusb_get_device_list(ctx, &list);
    if (n < 0) {
        err = (int)n;
        goto out;
    }

    /* Allocate at least one element so that n == 0 still yields a
     * valid, freeable pointer. */
    recs = calloc(n > 0 ? (size_t)n : 1u, sizeof *recs);
    if (recs == NULL) {
        err = LIBUSB_ERROR_NO_MEM;
        goto out;
    }

    for (ssize_t i = 0; i < n; i++) {
        struct libusb_device_descriptor desc;
        /* A device whose descriptor cannot be read is skipped. */
        if (libusb_get_device_descriptor(list[i], &desc) != LIBUSB_SUCCESS)
            continue;
        ff802_usb_fill_record(list[i], &desc, &recs[used++]);
    }

    *devs = recs;
    *count = used;
    recs = NULL;
    err = 0;

out:
    free(recs);
    if (list != NULL)
        libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return err;
}

void ff802_usb_free(ff802_usb_dev *devs)
{
    free(devs);
}

const char *ff802_usb_strerror(int err)
{
    return libusb_strerror(err);
}
