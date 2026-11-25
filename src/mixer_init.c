#include "ff802/mixer_init.h"

#include <stdio.h>

#define X(p, o, a) { a, FF802_MIXER_ROUTE_OFF, o, FF802_MIXER_CROSS_OFF, p }
#define R(k, a) { a, FF802_MIXER_ROUTE_0DB, k, FF802_MIXER_ROUTE, k }
#define V(k, a) { a, FF802_MIXER_OUTVOL_0DB, k, FF802_MIXER_OUTPUT_VOLUME, 0 }
#define I(k, a) { a, FF802_MIXER_INPUT9_LEVEL, k, FF802_MIXER_INPUT_ROUTE, FF802_MIXER_INPUT9 }

/*
 * The complete list, written out: no address is computed at run time.
 * ff802_mixer_init_verify() compares it with the formulas and the
 * checksum, and the tests compare it with the measured registers.
 */
static const ff802_mixer_entry TABLE[FF802_MIXER_INIT_COUNT] = {
    /* v2 (unchanged): crossed routings off, routings, output volumes */
    X(1, 9, 0x4220), X(2, 10, 0x4261),
    R(1, 0x4020), R(2, 0x4061), R(3, 0x40A2), R(4, 0x40E3),
    R(5, 0x4124), R(6, 0x4165), R(7, 0x41A6), R(8, 0x41E7),
    R(9, 0x4228), R(10, 0x4269), R(11, 0x42AA), R(12, 0x42EB),
    V(1, 0x1E00), V(2, 0x1F00), V(3, 0x2000), V(4, 0x2100),
    V(5, 0x2200), V(6, 0x2300), V(7, 0x2400), V(8, 0x2500),
    V(9, 0x2600), V(10, 0x2700), V(11, 0x2800), V(12, 0x2900),
    /* v3: Input 9 (Mic/Inst 9) -> Output 1..12 at 30 % linear */
    I(1, 0x4008), I(2, 0x4048), I(3, 0x4088), I(4, 0x40C8),
    I(5, 0x4108), I(6, 0x4148), I(7, 0x4188), I(8, 0x41C8),
    I(9, 0x4208), I(10, 0x4248), I(11, 0x4288), I(12, 0x42C8),
    /* v4: the remaining crossed Playback p -> Output o (p, o = 1..12, p != o) off */
    X(2, 1, 0x4021), X(3, 1, 0x4022), X(4, 1, 0x4023), X(5, 1, 0x4024),
    X(6, 1, 0x4025), X(7, 1, 0x4026), X(8, 1, 0x4027), X(9, 1, 0x4028),
    X(10, 1, 0x4029), X(11, 1, 0x402A), X(12, 1, 0x402B),
    X(1, 2, 0x4060), X(3, 2, 0x4062), X(4, 2, 0x4063), X(5, 2, 0x4064),
    X(6, 2, 0x4065), X(7, 2, 0x4066), X(8, 2, 0x4067), X(9, 2, 0x4068),
    X(10, 2, 0x4069), X(11, 2, 0x406A), X(12, 2, 0x406B),
    X(1, 3, 0x40A0), X(2, 3, 0x40A1), X(4, 3, 0x40A3), X(5, 3, 0x40A4),
    X(6, 3, 0x40A5), X(7, 3, 0x40A6), X(8, 3, 0x40A7), X(9, 3, 0x40A8),
    X(10, 3, 0x40A9), X(11, 3, 0x40AA), X(12, 3, 0x40AB),
    X(1, 4, 0x40E0), X(2, 4, 0x40E1), X(3, 4, 0x40E2), X(5, 4, 0x40E4),
    X(6, 4, 0x40E5), X(7, 4, 0x40E6), X(8, 4, 0x40E7), X(9, 4, 0x40E8),
    X(10, 4, 0x40E9), X(11, 4, 0x40EA), X(12, 4, 0x40EB),
    X(1, 5, 0x4120), X(2, 5, 0x4121), X(3, 5, 0x4122), X(4, 5, 0x4123),
    X(6, 5, 0x4125), X(7, 5, 0x4126), X(8, 5, 0x4127), X(9, 5, 0x4128),
    X(10, 5, 0x4129), X(11, 5, 0x412A), X(12, 5, 0x412B),
    X(1, 6, 0x4160), X(2, 6, 0x4161), X(3, 6, 0x4162), X(4, 6, 0x4163),
    X(5, 6, 0x4164), X(7, 6, 0x4166), X(8, 6, 0x4167), X(9, 6, 0x4168),
    X(10, 6, 0x4169), X(11, 6, 0x416A), X(12, 6, 0x416B),
    X(1, 7, 0x41A0), X(2, 7, 0x41A1), X(3, 7, 0x41A2), X(4, 7, 0x41A3),
    X(5, 7, 0x41A4), X(6, 7, 0x41A5), X(8, 7, 0x41A7), X(9, 7, 0x41A8),
    X(10, 7, 0x41A9), X(11, 7, 0x41AA), X(12, 7, 0x41AB),
    X(1, 8, 0x41E0), X(2, 8, 0x41E1), X(3, 8, 0x41E2), X(4, 8, 0x41E3),
    X(5, 8, 0x41E4), X(6, 8, 0x41E5), X(7, 8, 0x41E6), X(9, 8, 0x41E8),
    X(10, 8, 0x41E9), X(11, 8, 0x41EA), X(12, 8, 0x41EB),
    X(2, 9, 0x4221), X(3, 9, 0x4222), X(4, 9, 0x4223), X(5, 9, 0x4224),
    X(6, 9, 0x4225), X(7, 9, 0x4226), X(8, 9, 0x4227), X(10, 9, 0x4229),
    X(11, 9, 0x422A), X(12, 9, 0x422B),
    X(1, 10, 0x4260), X(3, 10, 0x4262), X(4, 10, 0x4263), X(5, 10, 0x4264),
    X(6, 10, 0x4265), X(7, 10, 0x4266), X(8, 10, 0x4267), X(9, 10, 0x4268),
    X(11, 10, 0x426A), X(12, 10, 0x426B),
    X(1, 11, 0x42A0), X(2, 11, 0x42A1), X(3, 11, 0x42A2), X(4, 11, 0x42A3),
    X(5, 11, 0x42A4), X(6, 11, 0x42A5), X(7, 11, 0x42A6), X(8, 11, 0x42A7),
    X(9, 11, 0x42A8), X(10, 11, 0x42A9), X(12, 11, 0x42AB),
    X(1, 12, 0x42E0), X(2, 12, 0x42E1), X(3, 12, 0x42E2), X(4, 12, 0x42E3),
    X(5, 12, 0x42E4), X(6, 12, 0x42E5), X(7, 12, 0x42E6), X(8, 12, 0x42E7),
    X(9, 12, 0x42E8), X(10, 12, 0x42E9), X(11, 12, 0x42EA),
};

const ff802_mixer_entry *ff802_mixer_init_table(void)
{
    return TABLE;
}

static uint32_t fnv(size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
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

uint32_t ff802_mixer_init_checksum(void)
{
    return fnv(FF802_MIXER_INIT_COUNT);
}

uint32_t ff802_mixer_init_v3_checksum(void)
{
    return fnv(FF802_MIXER_INIT_V3_COUNT);
}

uint32_t ff802_mixer_init_v2_checksum(void)
{
    return fnv(FF802_MIXER_INIT_V2_COUNT);
}

uint16_t ff802_mixer_matrix_addr(unsigned playback, unsigned output)
{
    if (playback < 1u || playback > FF802_MIXER_CHANNELS ||
        output < 1u || output > FF802_MIXER_CHANNELS)
        return 0;
    return (uint16_t)(0x4000u | ((output - 1u) << 6) | (0x20u + playback - 1u));
}

/* Hardware input i (1..30) -> Output o: matrix source index i-1. */
uint16_t ff802_mixer_input_addr(unsigned input, unsigned output)
{
    if (input < 1u || input > 30u || output < 1u || output > FF802_MIXER_CHANNELS)
        return 0;
    return (uint16_t)(0x4000u | ((output - 1u) << 6) | (input - 1u));
}

uint16_t ff802_mixer_route_addr(unsigned k)
{
    return ff802_mixer_matrix_addr(k, k);
}

/* The two crossed routings that the CC mode sets and the list switches off. */
static const uint8_t CROSS[FF802_MIXER_CROSS_COUNT][2] = { { 1, 9 }, { 2, 10 } };

/* Is (p, o) one of the two crossed routings of entries 1..2? */
static int in_cross(unsigned p, unsigned o)
{
    for (unsigned c = 0; c < FF802_MIXER_CROSS_COUNT; c++)
        if (CROSS[c][0] == p && CROSS[c][1] == o)
            return 1;
    return 0;
}

/*
 * The order of entries 39..168: by output o = 1..12, then playback
 * p = 1..12, without p == o and without the two of entries 1..2.
 * Advances (*p, *o) to the next pair; *o is 13 after the last.
 */
static void next_cross(unsigned *p, unsigned *o)
{
    do {
        if (++*p > FF802_MIXER_CHANNELS) {
            *p = 1;
            ++*o;
        }
    } while (*o <= FF802_MIXER_CHANNELS && (*p == *o || in_cross(*p, *o)));
}

uint16_t ff802_mixer_outvol_addr(unsigned k)
{
    if (k < 1u || k > FF802_MIXER_CHANNELS)
        return 0;
    return (uint16_t)(0x1E00u + ((k - 1u) << 8));
}

int ff802_mixer_init_verify(void)
{
    unsigned np = 0, no = 1;
    next_cross(&np, &no);                     /* the first pair: (2, 1) */
    if (ff802_mixer_init_v2_checksum() != FF802_MIXER_INIT_V2_CHECKSUM ||
        ff802_mixer_init_v3_checksum() != FF802_MIXER_INIT_V3_CHECKSUM ||
        ff802_mixer_init_checksum() != FF802_MIXER_INIT_CHECKSUM)
        return -1;
    for (unsigned i = 0; i < FF802_MIXER_INIT_COUNT; i++) {
        const ff802_mixer_entry *e = &TABLE[i];
        if (i < FF802_MIXER_CROSS_COUNT) {
            unsigned p = CROSS[i][0], o = CROSS[i][1];
            if (e->kind != FF802_MIXER_CROSS_OFF || e->source != p || e->channel != o ||
                e->addr != ff802_mixer_matrix_addr(p, o) || e->value != FF802_MIXER_ROUTE_OFF)
                return -1;
            continue;
        }
        if (i >= FF802_MIXER_INIT_V3_COUNT) {          /* v4: the other crossed routings */
            if (e->kind != FF802_MIXER_CROSS_OFF || e->value != FF802_MIXER_ROUTE_OFF ||
                e->source != np || e->channel != no ||
                e->addr != ff802_mixer_matrix_addr(np, no))
                return -1;
            next_cross(&np, &no);
            continue;
        }
        if (i >= FF802_MIXER_INIT_V2_COUNT) {          /* v3: Input 9 -> Output k */
            unsigned k = i - FF802_MIXER_INIT_V2_COUNT + 1u;
            if (e->kind != FF802_MIXER_INPUT_ROUTE || e->source != FF802_MIXER_INPUT9 ||
                e->channel != k || e->addr != ff802_mixer_input_addr(FF802_MIXER_INPUT9, k) ||
                e->value != FF802_MIXER_INPUT9_LEVEL)
                return -1;
            continue;
        }
        unsigned j = i - FF802_MIXER_CROSS_COUNT;
        unsigned k = (j % FF802_MIXER_CHANNELS) + 1u;
        int route = j < FF802_MIXER_CHANNELS;
        if (e->channel != k)
            return -1;
        if (route && (e->kind != FF802_MIXER_ROUTE || e->source != k ||
                      e->addr != ff802_mixer_route_addr(k) ||
                      e->value != FF802_MIXER_ROUTE_0DB))
            return -1;
        if (!route && (e->kind != FF802_MIXER_OUTPUT_VOLUME || e->source != 0 ||
                       e->addr != ff802_mixer_outvol_addr(k) ||
                       e->value != FF802_MIXER_OUTVOL_0DB))
            return -1;
    }
    return no == FF802_MIXER_CHANNELS + 1u ? 0 : -1;   /* all 130 seen */
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
    else if (e->kind == FF802_MIXER_INPUT_ROUTE)
        snprintf(buf, size, "Input %u -> %s, 30 %%", (unsigned)e->source,
                 ff802_mixer_output_name(e->channel));
    else if (e->kind == FF802_MIXER_CROSS_OFF)
        snprintf(buf, size, "Playback %u -> %s, off", (unsigned)e->source,
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
