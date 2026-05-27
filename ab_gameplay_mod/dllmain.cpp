#include "pch.h"

#include <windows.h>
#include <stdint.h>

#include "Logger.h"
#include "PassContext.h"
#include "PassPower.h"
#include <string.h>
#include "ModState.h"
#include "HotkeyToggle.h"
#include "D3DOverlay.h"

// ------------------------------------------------------------
// Globals
// ------------------------------------------------------------

static volatile bool g_running = true;
static uintptr_t g_base = 0;

// ------------------------------------------------------------
// Log context info
// ------------------------------------------------------------

static float BitsToFloat(DWORD bits)
{
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(float));
    return value;
}



static void LogCurrentContext(DWORD ctxCount)
{
    DWORD passer = GetSavedPasser();
    DWORD receiver = GetSavedReceiver();

    BYTE passerX = 0;
    BYTE passerY = 0;
    BYTE receiverX = 0;
    BYTE receiverY = 0;
    int dist = 0;

    if (ReadPassContextDistance(
        passer,
        receiver,
        &passerX,
        &passerY,
        &receiverX,
        &receiverY,
        &dist
    ))
    {
        LogFormat(
            "[CTX] count=%u passer=0x%08X receiver=0x%08X p=(%u,%u) r=(%u,%u) dist=%d",
            (unsigned int)ctxCount,
            (unsigned int)passer,
            (unsigned int)receiver,
            (unsigned int)passerX,
            (unsigned int)passerY,
            (unsigned int)receiverX,
            (unsigned int)receiverY,
            dist
        );
    }
    else
    {
        LogFormat(
            "[CTX] count=%u passer=0x%08X receiver=0x%08X ERROR leyendo +204/+205",
            (unsigned int)ctxCount,
            (unsigned int)passer,
            (unsigned int)receiver
        );
    }
}

// ------------------------------------------------------------
// Monitor thread
// ------------------------------------------------------------

static DWORD WINAPI MainThread(LPVOID)
{
    Sleep(1000);

    WriteLog("========================================");
    WriteLog("pes6_passspeed.dll cargada.");
    WriteLog("Version modular: PassSpeed v0.3.4 50% con logs.");
    WriteLog("IMPORTANTE: newCtx solo se loguea, todavia no bloquea boosts.");
    WriteLog("========================================");

    HMODULE exeModule = GetModuleHandleA(nullptr);

    if (!exeModule)
    {
        WriteLog("[ERROR] GetModuleHandleA(nullptr) fallo.");
        return 0;
    }

    g_base = (uintptr_t)exeModule;

    LogFormat("Base del EXE: 0x%08X", (unsigned int)g_base);

    if (!InstallContextHook(g_base))
    {
        WriteLog("[ERROR] Fallo instalando hook de contexto.");
        return 0;
    }

    if (!InstallPowerHook(g_base))
    {
        WriteLog("[ERROR] Fallo instalando hook de potencia.");
        return 0;
    }

    SetPhysicsModEnabled(true);

    if (!InstallD3DOverlayHook())
    {
        WriteLog("[WARN] No se pudo instalar overlay D3D. El mod funciona, pero no habra mensaje en pantalla.");
    }

    StartHotkeyToggle();

    WriteLog("[OK] Hotkey activo: Ctrl + Shift + P");
    ShowPhysicsModOverlayMessage(true);

    DWORD lastCtxCount = 0;
    DWORD lastPowerCount = 0;

    while (g_running)
    {
        Sleep(1000);

        DWORD ctxCount = GetContextCount();
        DWORD powerCount = GetPowerCount();

        if (ctxCount != lastCtxCount)
        {
            lastCtxCount = ctxCount;
            LogCurrentContext(ctxCount);
        }

        if (powerCount != lastPowerCount)
        {
            lastPowerCount = powerCount;

            LogFormat(
                "[PWR] count=%u edi=0x%08X->0x%08X ball50Before=%u dist=%u "
                "boostMode=0x%X gate=0x%X ctxAtPower=%u newCtx=%u "
                "ballD0=(%.2f,%.2f,%.2f) ball1454=(%.2f,%.2f,%.2f)",
                (unsigned int)powerCount,
                (unsigned int)GetLastEDIOriginal(),
                (unsigned int)GetLastEDIModified(),
                (unsigned int)GetLastBall50Before(),
                (unsigned int)GetLastDistSimple(),
                (unsigned int)GetLastBoostMode(),
                (unsigned int)GetLastBallGateMode(),
                (unsigned int)GetLastPowerCtxCount(),
                (unsigned int)GetLastPowerHadNewCtx(),
                BitsToFloat(GetLastBallD0XBits()),
                BitsToFloat(GetLastBallD0YBits()),
                BitsToFloat(GetLastBallD0ZBits()),
                BitsToFloat(GetLastBall1454XBits()),
                BitsToFloat(GetLastBall1458YBits()),
                BitsToFloat(GetLastBall145CZBits())
            );
        }
    }

    WriteLog("MainThread finalizado.");
    return 0;
}

// ------------------------------------------------------------
// DllMain
// ------------------------------------------------------------

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(hModule);

        HANDLE hThread = CreateThread(
            nullptr,
            0,
            MainThread,
            nullptr,
            0,
            nullptr
        );

        if (hThread)
        {
            CloseHandle(hThread);
        }

        break;
    }

    case DLL_PROCESS_DETACH:
    {
        g_running = false;
        StopHotkeyToggle();
        ShutdownD3DOverlay();
        WriteLog("pes6_passspeed.dll descargada.");
        break;
    }
    }

    return TRUE;
}