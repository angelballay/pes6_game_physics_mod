#include "pch.h"
#include "MemoryPatch.h"
#include "Logger.h"

bool CheckBytes(uintptr_t address, const unsigned char* expected, size_t count)
{
    __try
    {
        unsigned char* actual = (unsigned char*)address;

        for (size_t i = 0; i < count; i++)
        {
            if (actual[i] != expected[i])
            {
                LogFormat(
                    "[ERROR] Byte mismatch en 0x%08X + %u: esperado %02X, leido %02X",
                    (unsigned int)address,
                    (unsigned int)i,
                    expected[i],
                    actual[i]
                );
                return false;
            }
        }

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        LogFormat("[ERROR] Excepcion leyendo bytes en 0x%08X", (unsigned int)address);
        return false;
    }
}

bool WriteJump(uintptr_t source, void* destination, size_t length)
{
    if (length < 5)
    {
        return false;
    }

    DWORD oldProtect = 0;

    if (!VirtualProtect((void*)source, length, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogFormat("[ERROR] VirtualProtect fallo en 0x%08X", (unsigned int)source);
        return false;
    }

    uintptr_t src = source;
    uintptr_t dst = (uintptr_t)destination;
    int32_t relative = (int32_t)(dst - src - 5);

    unsigned char* p = (unsigned char*)source;

    p[0] = 0xE9; // JMP rel32
    *(int32_t*)(p + 1) = relative;

    for (size_t i = 5; i < length; i++)
    {
        p[i] = 0x90; // NOP
    }

    FlushInstructionCache(GetCurrentProcess(), (void*)source, length);

    DWORD temp = 0;
    VirtualProtect((void*)source, length, oldProtect, &temp);

    return true;
}

bool WriteFloat(uintptr_t address, float value)
{
    DWORD oldProtect = 0;
    if (!VirtualProtect((void*)address, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        LogFormat("[ERROR] VirtualProtect fallo en WriteFloat 0x%08X", (unsigned int)address);
        return false;
    }

    *(float*)address = value;

    DWORD temp = 0;
    VirtualProtect((void*)address, sizeof(float), oldProtect, &temp);

    return true;
}