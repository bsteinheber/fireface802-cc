#include "ff802/cc_mode.h"

#include <stddef.h>

ff802_status ff802_cc_command(bool enable, uint8_t out[FF802_REG_WORD_SIZE])
{
    uint16_t value = enable ? FF802_CC_VALUE_ENABLE : FF802_CC_VALUE_DISABLE;
    return ff802_reg_encode_bytes(FF802_REG_CC_MODE, value, out);
}

/* RME SysEx header: start, manufacturer 00 20 0D, then 10 00. */
static const uint8_t SYSEX_HEAD[6] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };

ff802_status ff802_cc_disable_midi(uint8_t out[FF802_CC_DISABLE_MIDI_SIZE])
{
    uint8_t sysex[12];
    uint32_t word;

    if (out == NULL)
        return FF802_ERR_NULL_ARG;
    ff802_status st = ff802_reg_encode(FF802_REG_CC_MODE,
                                       FF802_CC_VALUE_SYSEX_DISABLE, &word);
    if (st != FF802_OK)
        return st;

    for (unsigned i = 0; i < sizeof SYSEX_HEAD; i++)
        sysex[i] = SYSEX_HEAD[i];
    for (unsigned i = 0; i < 5u; i++)          /* 32 bits in 7-bit groups */
        sysex[6u + i] = (uint8_t)((word >> (7u * i)) & 0x7Fu);
    sysex[11] = 0xF7;

    for (unsigned p = 0; p < 4u; p++) {        /* 3 data bytes per packet */
        out[4u * p] = 0x13;                    /* cable 1, CIN 3 (as tested) */
        out[4u * p + 1u] = sysex[3u * p];
        out[4u * p + 2u] = sysex[3u * p + 1u];
        out[4u * p + 3u] = sysex[3u * p + 2u];
    }
    return FF802_OK;
}
