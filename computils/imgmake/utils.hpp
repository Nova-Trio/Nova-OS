#pragma once
#include <stdint.h>
namespace utils {

uint32_t crc32(const void* data, size_t length)
{
    const uint8_t* buffer = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= buffer[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320; // Standard IEEE 802.3 polynomial
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFF; // Final bit inversion
}

}
