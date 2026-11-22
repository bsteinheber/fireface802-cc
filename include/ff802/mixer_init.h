/*
 * Mixer initialisation of the original RME Fireface 802 in class-compliant
 * mode: the fixed list of register writes and their RME SysEx form.
 *
 * Pure protocol layer: no I/O, no allocation, no MIDI or USB access.
 *
 * The list has exactly 24 entries, in the order they are sent:
 *
 *   1..12   Playback k -> Hardware Output k at 0 dB (value 0x9000)
 *           register 0x4000 | (k-1) << 6 | (0x20 + k-1)
 *   13..24  Hardware Output k volume at 0 dB (value 0x0000)
 *           register 0x1E00 + ((k-1) << 8)
 *
 * k = 1..8 are Analog 1..8, k = 9..12 Phones 9..12. The formulas were
 * confirmed with OSCMix on the unit for Playback 1, 2, 9, 10, 11, 12 and
 * Hardware Output 1, 9, 10, 11 (written and observed) and Output 12
 * (observed in register reports); see docs/mixer_routing.md.
 *
 * Routings first, output volumes last: an interrupted run leaves the
 * output volumes as they were. Nothing else is in the list: no crossed
 * routings (e.g. Playback 9 -> Output 10), no inputs, no mute, no pan,
 * no outputs 13..30, no special registers (0x3D44, 0x3F00, 0x3F97,
 * 0x3F99). The list is a constant; no address is computed at run time.
 *
 * SysEx of one register write (12 bytes):
 *
 *   F0 00 20 0D 10 00  <encoded word in 5 x 7 bits, LSB first>  F7
 *
 * with the word of ff802_reg_encode(); the same frame as the tested CC
 * disable command (ff802_cc_disable_midi()).
 */
#ifndef FF802_MIXER_INIT_H
#define FF802_MIXER_INIT_H

#include <stddef.h>
#include <stdint.h>

#include "ff802/reg_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FF802_MIXER_CHANNELS     12u
#define FF802_MIXER_INIT_COUNT   24u
#define FF802_MIXER_SYSEX_SIZE   12u

#define FF802_MIXER_ROUTE_0DB    0x9000u   /* mixer gain 1.0 */
#define FF802_MIXER_OUTVOL_0DB   0x0000u   /* output volume in 0.1 dB */

/* FNV-1a (32 bit) over address and value (little-endian) of all entries. */
#define FF802_MIXER_INIT_CHECKSUM 0x61C13611u

typedef enum ff802_mixer_kind {
    FF802_MIXER_ROUTE = 0,        /* Playback k -> Hardware Output k */
    FF802_MIXER_OUTPUT_VOLUME     /* Hardware Output k volume */
} ff802_mixer_kind;

typedef struct ff802_mixer_entry {
    uint16_t         addr;
    uint16_t         value;
    uint8_t          channel;   /* k, 1..12 */
    ff802_mixer_kind kind;
} ff802_mixer_entry;

/* The FF802_MIXER_INIT_COUNT entries, in send order. */
const ff802_mixer_entry *ff802_mixer_init_table(void);

/* Checksum of the table as compiled in (see FF802_MIXER_INIT_CHECKSUM). */
uint32_t ff802_mixer_init_checksum(void);

/*
 * 0 if the table is exactly the expected one: the checksum matches and
 * every entry equals the formula for its position. Checked before
 * anything is sent.
 */
int ff802_mixer_init_verify(void);

/* The formulas, k = 1..12 (0 for any other k). */
uint16_t ff802_mixer_route_addr(unsigned k);
uint16_t ff802_mixer_outvol_addr(unsigned k);

/* "Analog 3", "Phones 9"; "?" for k outside 1..12. */
const char *ff802_mixer_output_name(unsigned k);

/* "Playback 9 -> Phones 9, 0 dB" or "Output Phones 9 volume, 0 dB". */
void ff802_mixer_entry_label(const ff802_mixer_entry *e, char *buf, size_t size);

/*
 * The 12-byte RME SysEx message that writes value to register addr.
 * *out is left untouched on error (address out of range).
 */
ff802_status ff802_rme_sysex_reg(uint16_t addr, uint16_t value,
                                 uint8_t out[FF802_MIXER_SYSEX_SIZE]);

/* The SysEx message of entry index (0..23) of the table, e.g. for display. */
ff802_status ff802_mixer_init_message(unsigned index, uint8_t out[FF802_MIXER_SYSEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIXER_INIT_H */
