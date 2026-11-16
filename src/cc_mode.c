#include "ff802/cc_mode.h"

ff802_status ff802_cc_command(bool enable, uint8_t out[FF802_REG_WORD_SIZE])
{
    uint16_t value = enable ? FF802_CC_VALUE_ENABLE : FF802_CC_VALUE_DISABLE;
    return ff802_reg_encode_bytes(FF802_REG_CC_MODE, value, out);
}
