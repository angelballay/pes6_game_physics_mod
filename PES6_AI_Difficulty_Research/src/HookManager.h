#pragma once

#include <Windows.h>
#include <cstdint>

class HookManager
{
public:
    static HookManager& Instance();

    bool InstallAll();
    void RemoveAll();

private:
    HookManager() = default;
    HookManager(const HookManager&) = delete;
    HookManager& operator=(const HookManager&) = delete;

    bool InstallJump(uintptr_t target, void* detour, size_t length);
};
