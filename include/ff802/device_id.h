/*
 * USB identifiers of the original RME Fireface 802 and classification of
 * a (vendor id, product id) pair into an operating mode.
 *
 * Pure logic: no USB access, no I/O.
 */
#ifndef FF802_DEVICE_ID_H
#define FF802_DEVICE_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Proprietary USB mode (confirmed). */
#define FF802_VID_RME              0x2A39u
#define FF802_PID_PROPRIETARY      0x3FCDu

/* Class-compliant mode (assumed, not yet confirmed under Linux). */
#define FF802_VID_CLASS_COMPLIANT  0x0424u
#define FF802_PID_CLASS_COMPLIANT  0x3FDDu

typedef enum ff802_mode {
    FF802_MODE_NONE = 0,          /* not an RME device */
    FF802_MODE_PROPRIETARY,       /* Fireface 802, proprietary USB mode */
    FF802_MODE_CLASS_COMPLIANT,   /* Fireface 802, class-compliant mode */
    FF802_MODE_RME_OTHER          /* RME vendor id, but not a known 802 PID */
} ff802_mode;

/* Classify a USB device by its vendor and product id. */
ff802_mode ff802_classify(uint16_t vid, uint16_t pid);

/* True for modes that denote an original Fireface 802. */
int ff802_mode_is_ff802(ff802_mode mode);

/* Short, stable identifier for the mode (e.g. "proprietary"). */
const char *ff802_mode_name(ff802_mode mode);

/* Human-readable description of the mode. */
const char *ff802_mode_description(ff802_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* FF802_DEVICE_ID_H */
