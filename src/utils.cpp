//
// Created by shalomc on 20/07/23.
//


// =====================================================

#include <cstdint>

uint8_t nybbleToHex(uint8_t nybble) {
    return nybble > 9 ? nybble + 0x37 : nybble + 0x30;
}

void hexData(uint8_t *data, uint8_t *output, int length)  {
    int x = 0;
    for (int y = 0; y < length; ++y, ++x)  {
        output[x] = nybbleToHex(data[y] >> 4 & 0xF);
        output[++x] = nybbleToHex(data[y] & 0xF);
        output[++x] = 0x20;
    }

    output[x] = 0;
}
