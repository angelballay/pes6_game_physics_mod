#include <Windows.h>
#include "AiDifficultyLogger.h"
#include "HookManager.h"

namespace
{
    DWORD WINAPI InitThread(LPVOID)
    {
        // Give Kitserver/PES a small moment to finish module initialization.
        Sleep(1000);

        AiDifficultyLogger::Instance().Initialize();
        const bool ok = HookManager::Instance().InstallAll();

        // If hooks fail, the header will still be written. We avoid calling LogFromHook
        // without CPU registers because this logger is intended only for hook probes.
        (void)ok;

        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        HookManager::Instance().RemoveAll();
        AiDifficultyLogger::Instance().Shutdown();
        break;
    }

    return TRUE;
}
