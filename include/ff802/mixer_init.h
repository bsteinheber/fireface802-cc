/*
 * Mixer initialisation of the original RME Fireface 802 in class-compliant
 * mode: the fixed list of register writes and their RME SysEx form.
 *
 * Pure protocol layer: no I/O, no allocation, no MIDI or USB access.
 *
 * The list (configuration ff802-mixer-init-v4) has exactly 168 entries, in
 * the order they are sent:
 *
 *   1..2    two unwanted crossed routings switched off (value 0x0000):
 *           Playback 1 -> Phones 9 (0x4220), Playback 2 -> Phones 10 (0x4261)
 *   3..14   Playback k -> Hardware Output k at 0 dB (value 0x9000)
 *           register 0x4000 | (k-1) << 6 | (0x20 + k-1)
 *   15..26  Hardware Output k volume at 0 dB (value 0x0000)
 *           register 0x1E00 + ((k-1) << 8)
 *   27..38  Input 9 (Mic/Inst 9) -> Hardware Output k at 30 % linear
 *           (value 0x2666), register 0x4000 | (k-1) << 6 | 8
 *   39..168 the other 130 Playback p -> Output o routings within the
 *           12 x 12 playback matrix (p, o = 1..12, p != o, without the two
 *           of 1..2) switched off (value 0x0000), by output, then playback
 *
 * Entries 1..38 are the previous configuration ff802-mixer-init-v3 and
 * entries 1..26 the configuration ff802-mixer-init-v2, unchanged and in the
 * same order; their own checksums (0xEC858A44, 0xC97A359C) are checked as
 * well. Together, the 12 x 12 matrix of Playback 1..12 -> Output 1..12 is
 * completely defined: 12 routings at 0 dB, 132 off. Playback 13..30 and
 * Outputs 13..30 (AES, ADAT) are not part of the list. The same mono
 * signal of Input 9 goes to both channels of each output pair (the unit
 * has no pan register: one value per output).
 *
 * k = 1..8 are Analog 1..8, k = 9..12 Phones 9..12. The matrix register of
 * Playback p -> Output o is 0x4000 | (o-1) << 6 | (0x20 + p-1), of
 * hardware input i -> Output o 0x4000 | (o-1) << 6 | (i-1) (OSCMix
 * device_ff802.c, inputs[8] = "Mic/Inst 9"; FireWire protocol: mixer step
 * 0x40, 8 line then 4 mic inputs). 0x2666 = lround(0.3 * 0x8000) (OSCMix
 * setmixlevel), confirmed on the unit for Input 9 -> Analog 3 (0x4088) in
 * the hardware test E1 (docs/mixer_e1.md); the other eleven addresses come
 * from the same formula.
 *
 * Confirmed on the unit: the formulas with OSCMix for Playback 1, 2, 9-12
 * and Output 1, 9-11 (written and observed) and Output 12 (reports); the
 * project's send path with 0x2600, 0x4228, 0x4269 and the full former list
 * of 24 entries; 0x0000 = off with 0x4269 (D1), 0x4261 (D2) and 0x4220
 * (D3). The CC mode routes Playback 1/2 also to Phones 9/10; entries 1..2
 * remove exactly these two crossed routings (docs/mixer_routing.md).
 *
 * Order: the tested v3 part first (crossed routings 1..2 off, routings,
 * output volumes, Input 9), then the 130 further crossed routings off:
 * a run interrupted after message 38 has set exactly the v3 list, and the
 * v3 part is sent exactly as before. Nothing else is in the list: no
 * playback 13..30, no other inputs, no input gain, no mute, no pan, no
 * outputs 13..30, no control room, no special registers (0x3D44, 0x3F00,
 * 0x3F97, 0x3F99). The list is a constant; no address is computed at run
 * time.
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
#define FF802_MIXER_CROSS_COUNT   2u   /* crossed routings switched off */
#define FF802_MIXER_INPUT9_COUNT 12u   /* Input 9 -> Output 1..12 */
#define FF802_MIXER_INIT_V2_COUNT 26u  /* 2 + 12 routings + 12 volumes */
#define FF802_MIXER_INIT_V3_COUNT 38u  /* v2 + 12 Input 9 routings */
#define FF802_MIXER_CROSS_EXTRA_COUNT 130u /* v4: the other crossed playback routings */
#define FF802_MIXER_PLAYBACK_MATRIX 144u   /* 12 x 12: 12 routings + 132 off */
#define FF802_MIXER_INIT_COUNT  168u   /* v3 (38) + 130 crossed routings off */
#define FF802_MIXER_SYSEX_SIZE   12u

#define FF802_MIXER_ROUTE_0DB    0x9000u   /* mixer gain 1.0 */
#define FF802_MIXER_ROUTE_OFF    0x0000u   /* mixer gain 0 (D1-D3) */
#define FF802_MIXER_OUTVOL_0DB   0x0000u   /* output volume in 0.1 dB */
#define FF802_MIXER_INPUT9       9u        /* hardware input Mic/Inst 9 */
#define FF802_MIXER_INPUT9_LEVEL 0x2666u   /* 30 % linear (E1) */

/*
 * FNV-1a (32 bit) over address and value (little-endian) of the entries:
 * all 168 (v4), the first 38 alone (v3, tested 0.2.0~dev6) and the first
 * 26 alone (v2, unchanged since 0.2.0~dev3).
 */
#define FF802_MIXER_INIT_CHECKSUM    0xC43D2879u
#define FF802_MIXER_INIT_V3_CHECKSUM 0xEC858A44u
#define FF802_MIXER_INIT_V2_CHECKSUM 0xC97A359Cu

/*
 * Name of this register list for logs and mixer-result. Changes whenever
 * an entry is added, removed, reordered or changes its value:
 *   v1  24 entries (12 routings, 12 volumes)
 *   v2  26 entries (v1 + Playback 1/2 -> Phones 9/10 off, sent first)
 *   v3  38 entries (v2 unchanged + Input 9 -> Output 1..12 at 0x2666)
 *   v4  168 entries (v3 unchanged + the other 130 crossed playback
 *       routings of the 12 x 12 matrix off)
 */
#define FF802_MIXER_INIT_ID    "ff802-mixer-init-v4"
#define FF802_MIXER_INIT_V3_ID "ff802-mixer-init-v3"
#define FF802_MIXER_INIT_V2_ID "ff802-mixer-init-v2"

typedef enum ff802_mixer_kind {
    FF802_MIXER_ROUTE = 0,        /* Playback k -> Hardware Output k */
    FF802_MIXER_OUTPUT_VOLUME,    /* Hardware Output k volume */
    FF802_MIXER_CROSS_OFF,        /* Playback source -> Output channel, off */
    FF802_MIXER_INPUT_ROUTE       /* hardware input source -> Output channel */
} ff802_mixer_kind;

typedef struct ff802_mixer_entry {
    uint16_t         addr;
    uint16_t         value;
    uint8_t          channel;   /* output k, 1..12 */
    ff802_mixer_kind kind;
    uint8_t          source;    /* playback 1..12 or hardware input (9), 0 for volumes */
} ff802_mixer_entry;

/* The FF802_MIXER_INIT_COUNT entries, in send order. */
const ff802_mixer_entry *ff802_mixer_init_table(void);

/* Checksum of the table as compiled in (see FF802_MIXER_INIT_CHECKSUM). */
uint32_t ff802_mixer_init_checksum(void);

/* Checksum of its first FF802_MIXER_INIT_V3_COUNT entries (the v3 list). */
uint32_t ff802_mixer_init_v3_checksum(void);

/* Checksum of its first FF802_MIXER_INIT_V2_COUNT entries (the v2 list). */
uint32_t ff802_mixer_init_v2_checksum(void);

/*
 * 0 if the table is exactly the expected one: both checksums match and
 * every entry equals the formula for its position. Checked before
 * anything is sent.
 */
int ff802_mixer_init_verify(void);

/* The formulas, 1-based (0 for anything outside 1..12, inputs 1..30). */
uint16_t ff802_mixer_matrix_addr(unsigned playback, unsigned output);
uint16_t ff802_mixer_input_addr(unsigned input, unsigned output);
uint16_t ff802_mixer_route_addr(unsigned k);     /* Playback k -> Output k */
uint16_t ff802_mixer_outvol_addr(unsigned k);

/* "Analog 3", "Phones 9"; "?" for k outside 1..12. */
const char *ff802_mixer_output_name(unsigned k);

/* "Playback 9 -> Phones 9, 0 dB", "Playback 1 -> Phones 9, off",
 * "Output Phones 9 volume, 0 dB" or "Input 9 -> Analog 3, 30 %". */
void ff802_mixer_entry_label(const ff802_mixer_entry *e, char *buf, size_t size);

/*
 * The 12-byte RME SysEx message that writes value to register addr.
 * *out is left untouched on error (address out of range).
 */
ff802_status ff802_rme_sysex_reg(uint16_t addr, uint16_t value,
                                 uint8_t out[FF802_MIXER_SYSEX_SIZE]);

/* The SysEx message of entry index (0..167) of the table, e.g. for display. */
ff802_status ff802_mixer_init_message(unsigned index, uint8_t out[FF802_MIXER_SYSEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FF802_MIXER_INIT_H */
