/*
 * Class-compliant (CC) mode switch command for the original RME Fireface 802.
 *
 * Builds the register command that enables or disables CC mode. Like the
 * register codec, this module is protocol-only and performs no USB I/O.
 *
 * Note: according to Windows USB captures, the mode change takes effect
 * only after the device is disconnected or re-initialized.
 */
#ifndef FF802_CC_MODE_H
#define FF802_CC_MODE_H

#include <stdbool.h>
#include <stdint.h>

#include "ff802/reg_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register controlling class-compliant mode. */
#define FF802_REG_CC_MODE 0x3D44u

#define FF802_CC_VALUE_DISABLE 0x0000u
#define FF802_CC_VALUE_ENABLE  0x0001u

/*
 * Write the 4-byte wire command that enables (enable = true) or disables
 * (enable = false) class-compliant mode into out.
 */
ff802_status ff802_cc_command(bool enable, uint8_t out[FF802_REG_WORD_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FF802_CC_MODE_H */
