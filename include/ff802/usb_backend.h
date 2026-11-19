/*
 * Abstract USB backend used by the CC switch logic.
 *
 * The switch logic (cc_switch.h) only talks to USB through this table of
 * function pointers. The real implementation is in usb_comm.c (libusb);
 * the tests use a simulated backend. This header has no libusb dependency.
 */
#ifndef FF802_USB_BACKEND_H
#define FF802_USB_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "ff802/detect.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Expected USB layout of the Fireface 802 in proprietary mode. */
#define FF802_USB_CONFIG       1u
#define FF802_USB_INTERFACE    1u
#define FF802_USB_ALTSETTING   0u
#define FF802_USB_EP_OUT       0x0Cu
#define FF802_USB_MAX_PACKET   512u

/* Timeout for the single bulk OUT transfer. */
#define FF802_USB_TIMEOUT_MS   1000u

/* bmAttributes transfer type values (USB 2.0, table 9-13). */
#define FF802_USB_EP_TYPE_MASK 0x03u
#define FF802_USB_EP_TYPE_BULK 0x02u

/* Backend-independent USB error codes. */
typedef enum ff802_usb_err {
    FF802_USB_OK = 0,
    FF802_USB_ERR_ACCESS,          /* insufficient permissions */
    FF802_USB_ERR_NO_DEVICE,       /* device disconnected */
    FF802_USB_ERR_NOT_FOUND,       /* entity not found */
    FF802_USB_ERR_BUSY,            /* resource busy (e.g. interface claimed) */
    FF802_USB_ERR_TIMEOUT,         /* transfer timed out */
    FF802_USB_ERR_PIPE,            /* endpoint stalled */
    FF802_USB_ERR_OVERFLOW,
    FF802_USB_ERR_IO,
    FF802_USB_ERR_NO_MEM,
    FF802_USB_ERR_NOT_SUPPORTED,
    FF802_USB_ERR_WRITE_DISABLED,  /* writes disabled at build time */
    FF802_USB_ERR_OTHER
} ff802_usb_err;

const char *ff802_usb_err_str(ff802_usb_err err);

/* Maximum number of endpoints recorded per interface. */
#define FF802_MAX_ENDPOINTS 16u

typedef struct ff802_ep_info {
    uint8_t  address;       /* bEndpointAddress */
    uint8_t  attributes;    /* bmAttributes */
    uint16_t max_packet;    /* wMaxPacketSize */
} ff802_ep_info;

/* Descriptor data of the target interface, read from cached descriptors. */
typedef struct ff802_dev_layout {
    uint8_t config_value;        /* bConfigurationValue of the active config,
                                    0 if unconfigured */
    uint8_t has_interface;       /* interface/altsetting found in it */
    uint8_t num_endpoints;       /* valid entries in eps[] */
    ff802_ep_info eps[FF802_MAX_ENDPOINTS];
} ff802_dev_layout;

typedef enum ff802_layout_status {
    FF802_LAYOUT_OK = 0,
    FF802_LAYOUT_WRONG_CONFIG,      /* active configuration is not 1 */
    FF802_LAYOUT_NO_INTERFACE,      /* interface 1 / altsetting 0 missing */
    FF802_LAYOUT_NO_ENDPOINT,       /* endpoint 0x0C missing */
    FF802_LAYOUT_NOT_BULK,          /* endpoint 0x0C is not bulk */
    FF802_LAYOUT_MAX_PACKET         /* wMaxPacketSize is not 512 */
} ff802_layout_status;

/* Validate a layout against the expected Fireface 802 layout. */
ff802_layout_status ff802_check_layout(const ff802_dev_layout *layout);

const char *ff802_layout_status_str(ff802_layout_status st);

/* Opaque device handle owned by the backend. */
typedef void *ff802_usb_handle;

typedef struct ff802_usb_backend {
    void *self;

    /* Non-zero if this backend is able to claim interfaces and write. */
    int write_enabled;

    /* Enumerate devices. The array stays valid until free_devices(). */
    ff802_usb_err (*list_devices)(void *self, ff802_usb_dev **devs, size_t *n);
    void (*free_devices)(void *self, ff802_usb_dev *devs);

    /* Read cached descriptor data; must not open the device. */
    ff802_usb_err (*get_layout)(void *self, const ff802_usb_dev *dev,
                                uint8_t iface, uint8_t altsetting,
                                ff802_dev_layout *out);

    ff802_usb_err (*open)(void *self, const ff802_usb_dev *dev,
                          ff802_usb_handle *handle);
    void (*close)(void *self, ff802_usb_handle handle);

    /* *active is set to 1 if a kernel driver is bound to the interface. */
    ff802_usb_err (*kernel_driver_active)(void *self, ff802_usb_handle handle,
                                          uint8_t iface, int *active);

    ff802_usb_err (*claim)(void *self, ff802_usb_handle handle, uint8_t iface);
    ff802_usb_err (*release)(void *self, ff802_usb_handle handle, uint8_t iface);

    /* One bulk OUT transfer, no retries. *transferred is always set. */
    ff802_usb_err (*bulk_write)(void *self, ff802_usb_handle handle,
                                uint8_t endpoint, const uint8_t *data,
                                size_t len, unsigned timeout_ms,
                                size_t *transferred);

    /*
     * Disable path only (ff802-cc disable, interface 3): claim the
     * interface and let libusb detach the kernel driver bound to it (libusb
     * auto-detach); release() then lets libusb re-attach it. NULL unless explicitly enabled with
     * ff802_usb_comm_allow_detach_claim(); the enable path never uses it.
     */
    ff802_usb_err (*claim_detach)(void *self, ff802_usb_handle handle,
                                  uint8_t iface);
} ff802_usb_backend;

#ifdef __cplusplus
}
#endif

#endif /* FF802_USB_BACKEND_H */
