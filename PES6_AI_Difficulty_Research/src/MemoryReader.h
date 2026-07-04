#pragma once

#include <cstdint>

class MemoryReader final
{
public:
    static uint8_t  ReadU8(uintptr_t address, uint8_t fallback = 0);
    static uint16_t ReadU16(uintptr_t address, uint16_t fallback = 0);
    static uint32_t ReadU32(uintptr_t address, uint32_t fallback = 0);
    static float    ReadF32(uintptr_t address, float fallback = 0.0f);
};
