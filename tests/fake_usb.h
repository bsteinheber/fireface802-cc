/*
 * Simulated USB backend for tests. Records every call in a trace string
 * and lets tests inject errors per operation. Never touches real USB.
 */
#ifndef FF802_FAKE_USB_H
#define FF802_FAKE_USB_H

#include "ff802/usb_backend.h"

#define FAKE_MAX_DEVS 8

typedef struct fake_usb {
    /* Configuration set by the test. */
    ff802_usb_dev    devs[FAKE_MAX_DEVS];
    size_t           ndevs;
    ff802_dev_layout layout;
    int              kernel_driver_active;
    int              write_enabled;
    size_t           write_transferred;   /* bytes reported by bulk_write */

    ff802_usb_err err_list;
    ff802_usb_err err_layout;
    ff802_usb_err err_open;
    ff802_usb_err err_kdrv;
    ff802_usb_err err_claim;
    ff802_usb_err err_write;
    ff802_usb_err err_release;
    ff802_usb_err err_claim_detach;

    /* Observations. */
    char     trace[256];
    int      n_open, n_close, n_claim, n_release, n_write, n_list, n_free;
    int      n_claim_detach;        /* claims with kernel driver auto-detach */
    int      open_handles;          /* opened minus closed */
    int      claimed;               /* claimed minus released */
    uint8_t  last_iface;
    uint8_t  layout_iface, layout_alt;
    uint8_t  write_ep;
    uint8_t  write_data[16];
    size_t   write_len;
    unsigned write_timeout;
    ff802_usb_dev opened_dev;
} fake_usb;

/* Initialise with one healthy Fireface 802 in proprietary mode. */
void fake_usb_init(fake_usb *f);

/* Build a backend table bound to f. */
ff802_usb_backend fake_usb_backend(fake_usb *f);

/* Fill layout with the expected Fireface 802 interface 1 layout. */
void fake_usb_good_layout(ff802_dev_layout *l);

#endif /* FF802_FAKE_USB_H */
