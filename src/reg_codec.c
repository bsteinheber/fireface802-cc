#include "ff802/reg_codec.h"

#include <stddef.h>

/* Returns 1 if x has an odd number of set bits, 0 otherwise. */
static uint32_t odd_bit_count(uint32_t x)
{
    /* Fold the word onto itself; the XOR of all bits ends up in bit 0. */
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return x & 1u;
}

bool ff802_reg_parity_ok(uint32_t word)
{
    return odd_bit_count(word) == 1u;
}

ff802_status ff802_reg_encode(uint16_t addr, uint16_t value, uint32_t *word)
{
    if (word == NULL)
        return FF802_ERR_NULL_ARG;
    if (addr > FF802_REG_ADDR_MAX)
        return FF802_ERR_ADDR_RANGE;

    uint32_t w = ((uint32_t)addr << FF802_REG_ADDR_SHIFT) | (uint32_t)value;

    /* Set the parity bit if needed to make the total bit count odd. */
    if (odd_bit_count(w) == 0u)
        w |= FF802_REG_PARITY_BIT;

    *word = w;
    return FF802_OK;
}

ff802_status ff802_reg_decode(uint32_t word, uint16_t *addr, uint16_t *value)
{
    if (addr == NULL || value == NULL)
        return FF802_ERR_NULL_ARG;
    if (!ff802_reg_parity_ok(word))
        return FF802_ERR_PARITY;

    *addr = (uint16_t)((word & FF802_REG_ADDR_MASK) >> FF802_REG_ADDR_SHIFT);
    *value = (uint16_t)(word & FF802_REG_VALUE_MASK);
    return FF802_OK;
}

void ff802_word_to_le(uint32_t word, uint8_t out[FF802_REG_WORD_SIZE])
{
    out[0] = (uint8_t)(word & 0xFFu);
    out[1] = (uint8_t)((word >> 8) & 0xFFu);
    out[2] = (uint8_t)((word >> 16) & 0xFFu);
    out[3] = (uint8_t)((word >> 24) & 0xFFu);
}

uint32_t ff802_word_from_le(const uint8_t in[FF802_REG_WORD_SIZE])
{
    return (uint32_t)in[0]
         | ((uint32_t)in[1] << 8)
         | ((uint32_t)in[2] << 16)
         | ((uint32_t)in[3] << 24);
}

ff802_status ff802_reg_encode_bytes(uint16_t addr, uint16_t value,
                                    uint8_t out[FF802_REG_WORD_SIZE])
{
    if (out == NULL)
        return FF802_ERR_NULL_ARG;

    uint32_t word;
    ff802_status st = ff802_reg_encode(addr, value, &word);
    if (st != FF802_OK)
        return st;

    ff802_word_to_le(word, out);
    return FF802_OK;
}
