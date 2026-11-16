/*
 * Register command encoding for the RME Fireface 802 (proprietary USB mode).
 *
 * This module is a pure protocol layer: it performs no I/O, no allocation
 * and knows nothing about USB. It turns (address, value) pairs into the
 * 4-byte wire representation and back.
 *
 * Word layout (32 bit, transmitted little-endian):
 *
 *   bit 31      : parity bit, chosen so that the whole word has an odd
 *                 number of set bits
 *   bits 30..16 : register address (15 bit)
 *   bits 15..0  : register value (16 bit)
 *
 * See docs/protocol.md for a detailed description.
 */
#ifndef FF802_REG_CODEC_H
#define FF802_REG_CODEC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size of one encoded register command on the wire. */
#define FF802_REG_WORD_SIZE 4u

/* Highest valid register address (15 bit). */
#define FF802_REG_ADDR_MAX 0x7FFFu

#define FF802_REG_PARITY_BIT  (UINT32_C(1) << 31)
#define FF802_REG_ADDR_SHIFT  16u
#define FF802_REG_ADDR_MASK   (UINT32_C(0x7FFF) << FF802_REG_ADDR_SHIFT)
#define FF802_REG_VALUE_MASK  UINT32_C(0x0000FFFF)

/* Result codes returned by the codec functions. */
typedef enum ff802_status {
    FF802_OK = 0,
    FF802_ERR_NULL_ARG = -1,      /* a required pointer argument was NULL */
    FF802_ERR_ADDR_RANGE = -2,    /* address does not fit into 15 bit */
    FF802_ERR_PARITY = -3         /* word does not have odd parity */
} ff802_status;

/*
 * Encode a register write into a 32-bit command word.
 * Returns FF802_ERR_ADDR_RANGE if addr > FF802_REG_ADDR_MAX; *word is left
 * untouched on error.
 */
ff802_status ff802_reg_encode(uint16_t addr, uint16_t value, uint32_t *word);

/*
 * Decode a 32-bit command word into address and value.
 * Returns FF802_ERR_PARITY if the word does not have odd parity; the
 * outputs are left untouched on error.
 */
ff802_status ff802_reg_decode(uint32_t word, uint16_t *addr, uint16_t *value);

/* True if the word has an odd number of set bits (i.e. valid parity). */
bool ff802_reg_parity_ok(uint32_t word);

/* Serialize a word into 4 bytes, least significant byte first. */
void ff802_word_to_le(uint32_t word, uint8_t out[FF802_REG_WORD_SIZE]);

/* Deserialize 4 little-endian bytes into a word. */
uint32_t ff802_word_from_le(const uint8_t in[FF802_REG_WORD_SIZE]);

/*
 * Convenience: encode a register write directly into its 4-byte wire form.
 * *out is left untouched on error.
 */
ff802_status ff802_reg_encode_bytes(uint16_t addr, uint16_t value,
                                    uint8_t out[FF802_REG_WORD_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* FF802_REG_CODEC_H */
