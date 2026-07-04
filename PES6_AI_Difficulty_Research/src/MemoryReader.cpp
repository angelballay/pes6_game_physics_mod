#include "MemoryReader.h"
#include <Windows.h>

uint8_t MemoryReader::ReadU8(uintptr_t address, uint8_t fallback)
{
    __try { return *reinterpret_cast<volatile uint8_t*>(address); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

uint16_t MemoryReader::ReadU16(uintptr_t address, uint16_t fallback)
{
    __try { return *reinterpret_cast<volatile uint16_t*>(address); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

uint32_t MemoryReader::ReadU32(uintptr_t address, uint32_t fallback)
{
    __try { return *reinterpret_cast<volatile uint32_t*>(address); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}


float MemoryReader::ReadF32(uintptr_t address, float fallback)
{
    __try { return *reinterpret_cast<volatile float*>(address); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}
