#include "ff802/mixer_init.h"

#include <stdio.h>

#define R(k, a) { a, FF802_MIXER_ROUTE_0DB, k, FF802_MIXER_ROUTE }
#define V(k, a) { a, FF802_MIXER_OUTVOL_0DB, k, FF802_MIXER_OUTPUT_VOLUME }

/*
 * The complete list, written out: no address is computed at run time.
 * ff802_mixer_init_verify() compares it with the formulas and the
 * checksum, and the tests compare it with the measured registers.
 */
static const ff802_mixer_entry TABLE[FF802_MIXER_INIT_COUNT] = {
    R(1, 0x4020), R(2, 0x4061), R(3, 0x40A2), R(4, 0x40E3),
    R(5, 0x4124), R(6, 0x4165), R(7, 0x41A6), R(8, 0x41E7),
    R(9, 0x4228), R(10, 0x4269), R(11, 0x42AA), R(12, 0x42EB),
    V(1, 0x1E00), V(2, 0x1F00), V(3, 0x2000), V(4, 0x2100),
    V(5, 0x2200), V(6, 0x2300), V(7, 0x2400), V(8, 0x2500),
    V(9, 0x2600), V(10, 0x2700), V(11, 0x2800), V(12, 0x2900),
};

const ff802_mixer_entry *ff802_mixer_init_table(void)
{
    return TABLE;
}

uint32_t ff802_mixer_init_checksum(void)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        const uint8_t b[4] = {
            (uint8_t)(TABLE[i].addr & 0xFFu), (uint8_t)(TABLE[i].addr >> 8),
            (uint8_t)(TABLE[i].value & 0xFFu), (uint8_t)(TABLE[i].value >> 8)
        };
        for (unsigned j = 0; j < 4u; j++) {
            h ^= b[j];
            h *= 16777619u;
        }
    }
    return h;
}

uint16_t ff802_mixer_route_addr(unsigned k)
{
    if (k < 1u || k > FF802_MIXER_CHANNELS)
        return 0;
    return (uint16_t)(0x4000u | ((k - 1u) << 6) | (0x20u + k - 1u));
}

uint16_t ff802_mixer_outvol_addr(unsigned k)
{
    if (k < 1u || k > FF802_MIXER_CHANNELS)
        return 0;
    return (uint16_t)(0x1E00u + ((k - 1u) << 8));
}

int ff802_mixer_init_verify(void)
{
    if (ff802_mixer_init_checksum() != FF802_MIXER_INIT_CHECKSUM)
        return -1;
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        const ff802_mixer_entry *e = &TABLE[i];
        unsigned k = (i % FF802_MIXER_CHANNELS) + 1u;
        int route = i < FF802_MIXER_CHANNELS;
        if (e->channel != k)
            return -1;
        if (route && (e->kind != FF802_MIXER_ROUTE ||
                      e->addr != ff802_mixer_route_addr(k) ||
                      e->value != FF802_MIXER_ROUTE_0DB))
            return -1;
        if (!route && (e->kind != FF802_MIXER_OUTPUT_VOLUME ||
                       e->addr != ff802_mixer_outvol_addr(k) ||
                       e->value != FF802_MIXER_OUTVOL_0DB))
            return -1;
    }
    return 0;
}

const char *ff802_mixer_output_name(unsigned k)
{
    static const char *const NAMES[FF802_MIXER_CHANNELS] = {
        "Analog 1", "Analog 2", "Analog 3", "Analog 4", "Analog 5", "Analog 6",
        "Analog 7", "Analog 8", "Phones 9", "Phones 10", "Phones 11", "Phones 12"
    };
    if (k < 1u || k > FF802_MIXER_CHANNELS)
        return "?";
    return NAMES[k - 1u];
}

void ff802_mixer_entry_label(const ff802_mixer_entry *e, char *buf, size_t size)
{
    if (buf == NULL || size == 0)
        return;
    if (e == NULL) {
        buf[0] = '\0';
        return;
    }
    if (e->kind == FF802_MIXER_ROUTE)
        snprintf(buf, size, "Playback %u -> %s, 0 dB", (unsigned)e->channel,
                 ff802_mixer_output_name(e->channel));
    else
        snprintf(buf, size, "Output %s volume, 0 dB",
                 ff802_mixer_output_name(e->channel));
}

/* RME SysEx header: start, manufacturer 00 20 0D, then 10 00. */
static const uint8_t SYSEX_HEAD[6] = { 0xF0, 0x00, 0x20, 0x0D, 0x10, 0x00 };

ff802_status ff802_rme_sysex_reg(uint16_t addr, uint16_t value,
                                 uint8_t out[FF802_MIXER_SYSEX_SIZE])
{
    uint32_t word;

    if (out == NULL)
        return FF802_ERR_NULL_ARG;
    ff802_status st = ff802_reg_encode(addr, value, &word);
    if (st != FF802_OK)
        return st;
    for (unsigned i = 0; i < sizeof SYSEX_HEAD; i++)
        out[i] = SYSEX_HEAD[i];
    for (unsigned i = 0; i < 5u; i++)          /* 32 bits in 7-bit groups */
        out[6u + i] = (uint8_t)((word >> (7u * i)) & 0x7Fu);
    out[11] = 0xF7;
    return FF802_OK;
}

ff802_status ff802_mixer_init_message(unsigned index, uint8_t out[FF802_MIXER_SYSEX_SIZE])
{
    if (index >= FF802_MIXER_INIT_COUNT)
        return FF802_ERR_ADDR_RANGE;
    return ff802_rme_sysex_reg(TABLE[index].addr, TABLE[index].value, out);
}
