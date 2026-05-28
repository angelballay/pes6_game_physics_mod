#include "pch.h"

#include "HotkeyToggle.h"

#include "Logger.h"
#include "ModState.h"
#include "KitserverOverlay.h"

#include <windows.h>

static volatile bool g_hotkeyRunning = false;
static HANDLE g_hotkeyThread = nullptr;

static bool IsKeyDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool WasKeyPressedNow(int vk)
{
    return (GetAsyncKeyState(vk) & 0x0001) != 0;
}

static DWORD WINAPI HotkeyThreadProc(LPVOID)
{
    WriteLog("[HOTKEY] Thread iniciado. Toggle: Ctrl + Shift + P");

    while (g_hotkeyRunning)
    {
        bool ctrl = IsKeyDown(VK_CONTROL);
        bool shift = IsKeyDown(VK_SHIFT);
        bool pPressed = WasKeyPressedNow('P');

        if (ctrl && shift && pPressed)
        {
            bool enabled = TogglePhysicsModEnabled();

            LogFormat(
                "[HOTKEY] Mod de fisicas %s",
                enabled ? "ACTIVADO" : "DESACTIVADO"
            );

            ShowPhysicsModOverlayMessage(enabled);
        }

        Sleep(25);
    }

    WriteLog("[HOTKEY] Thread finalizado.");

    return 0;
}

void StartHotkeyToggle()
{
    if (g_hotkeyRunning)
        return;

    g_hotkeyRunning = true;

    g_hotkeyThread = CreateThread(
        nullptr,
        0,
        HotkeyThreadProc,
        nullptr,
        0,
        nullptr
    );

    if (g_hotkeyThread)
    {
        CloseHandle(g_hotkeyThread);
        g_hotkeyThread = nullptr;
    }
    else
    {
        g_hotkeyRunning = false;
        WriteLog("[HOTKEY][ERROR] No se pudo crear thread.");
    }
}

void StopHotkeyToggle()
{
    g_hotkeyRunning = false;
}