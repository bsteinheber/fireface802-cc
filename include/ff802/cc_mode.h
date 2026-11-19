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

/*
 * Leaving class-compliant mode ("ff802-cc disable").
 *
 * In CC mode (0424:3fdd) the unit has no endpoint 0x0C. The command that
 * switched a real unit to proprietary mode (hardware test 2026-11-19) is
 * the same register written as an RME SysEx message on the USB-MIDI
 * interface 3, bulk OUT endpoint 0x07, cable 1:
 *
 *   SysEx  F0 00 20 0D 10 00  <word in 5 x 7 bits, LSB first>  F7
 *   word   ff802_reg_encode(0x3D44, 0xFFFE) = 0xBD44FFFE
 *   USB    4-byte USB-MIDI packets, header 0x13 (cable 1, CIN 3) on each
 *          packet, exactly as in the tested transfer
 *
 * The meaning of the value 0xFFFE is not documented; the bytes reproduce
 * the tested transfer and must not be changed without a new test.
 */
#define FF802_CC_VALUE_SYSEX_DISABLE 0xFFFEu
#define FF802_CC_DISABLE_MIDI_SIZE   16u

/* Write the 16 bytes of the USB-MIDI transfer that leaves CC mode. */
ff802_status ff802_cc_disable_midi(uint8_t out[FF802_CC_DISABLE_MIDI_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FF802_CC_MODE_H */
