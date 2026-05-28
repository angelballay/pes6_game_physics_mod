#include "pch.h"

#include "KitserverOverlay.h"
#include "Logger.h"

#include <windows.h>
#include <tchar.h>

// ------------------------------------------------------------
// Kitserver dynamic imports
// ------------------------------------------------------------
//
// No incluimos headers de Kitserver y no linkeamos kload.lib.
// Resolvemos todo en runtime con GetModuleHandle/GetProcAddress.
//
// IMPORTANTE:
// Esto compila aunque kload.dll no este presente.
// Si kload.dll no esta cargada, el overlay se desactiva y el mod
// sigue funcionando sin mensaje visual.
// ------------------------------------------------------------

enum KitserverHookId
{
    hk_D3D_Create = 0,
    hk_D3D_GetDeviceCaps = 1,
    hk_D3D_CreateDevice = 2,
    hk_D3D_Present = 3
};

typedef void(__cdecl* HookFunctionFn)(int hookId, DWORD functionAddress);
typedef void(__cdecl* UnhookFunctionFn)(int hookId, DWORD functionAddress);
typedef void(__cdecl* KDrawTextFn)(float x, float y, DWORD color, DWORD fontSize, char* text, bool absolute);
typedef void(__cdecl* KGetTextExtentFn)(char* text, DWORD fontSize, SIZE* size);

static HookFunctionFn g_hookFunction = nullptr;
static UnhookFunctionFn g_unhookFunction = nullptr;
static KDrawTextFn g_kDrawText = nullptr;
static KGetTextExtentFn g_kGetTextExtent = nullptr;

static bool g_exportsLoaded = false;
static bool g_overlayInstalled = false;

static volatile LONG g_drawMessage = 0;
static DWORD g_messageUntilTick = 0;
static bool g_lastEnabled = true;

static CRITICAL_SECTION g_overlayLock;
static bool g_overlayLockInitialized = false;

static volatile LONG g_presentCallCount = 0;
static volatile LONG g_drawCallCount = 0;

// ------------------------------------------------------------
// Dynamic loading
// ------------------------------------------------------------

static FARPROC GetProcAddressAny(HMODULE module, const char* name)
{
    FARPROC proc = GetProcAddress(module, name);

    if (proc)
        return proc;

    return nullptr;
}

static bool LoadKitserverExports()
{
    if (g_exportsLoaded)
        return true;

    HMODULE kload = GetModuleHandleA("kload.dll");

    if (!kload)
    {
        WriteLog("[KSO][WARN] kload.dll no esta cargada. Overlay no disponible.");
        return false;
    }

    g_hookFunction = (HookFunctionFn)GetProcAddressAny(kload, "HookFunction");
    g_unhookFunction = (UnhookFunctionFn)GetProcAddressAny(kload, "UnhookFunction");
    g_kDrawText = (KDrawTextFn)GetProcAddressAny(kload, "KDrawText");
    g_kGetTextExtent = (KGetTextExtentFn)GetProcAddressAny(kload, "KGetTextExtent");

    if (!g_hookFunction || !g_unhookFunction || !g_kDrawText || !g_kGetTextExtent)
    {
        LogFormat(
            "[KSO][ERROR] Exports faltantes: HookFunction=%u UnhookFunction=%u KDrawText=%u KGetTextExtent=%u",
            g_hookFunction ? 1u : 0u,
            g_unhookFunction ? 1u : 0u,
            g_kDrawText ? 1u : 0u,
            g_kGetTextExtent ? 1u : 0u
        );

        g_hookFunction = nullptr;
        g_unhookFunction = nullptr;
        g_kDrawText = nullptr;
        g_kGetTextExtent = nullptr;

        return false;
    }

    g_exportsLoaded = true;

    WriteLog("[KSO][OK] Exports de kload.dll cargados dinamicamente.");

    return true;
}

// ------------------------------------------------------------
// Drawing
// ------------------------------------------------------------

static void DrawPhysicsMessage()
{
    if (InterlockedCompareExchange(&g_drawMessage, 0, 0) == 0)
        return;

    DWORD now = GetTickCount();

    if (now > g_messageUntilTick)
    {
        InterlockedExchange(&g_drawMessage, 0);
        return;
    }

    if (!g_kDrawText || !g_kGetTextExtent)
        return;

    bool enabled = true;

    if (g_overlayLockInitialized)
    {
        EnterCriticalSection(&g_overlayLock);
        enabled = g_lastEnabled;
        LeaveCriticalSection(&g_overlayLock);
    }
    else
    {
        enabled = g_lastEnabled;
    }

    char text[128] = { 0 };

    strcpy_s(
        text,
        sizeof(text),
        enabled
        ? "FISICAS ACTIVADAS"
        : "FISICAS DESACTIVADAS"
    );

    SIZE size = {};
    g_kGetTextExtent(text, 16, &size);

    // Sin GetPESInfo por ahora: usamos coordenadas absolutas seguras.
    // 640x480 es la base historica de PES6/Kitserver; con absolute=true
    // Kitserver se encarga de escalar internamente segun su sistema.
    float baseWidth = 640.0f;
    float x = (baseWidth - (float)size.cx) / 2.0f;
    float y = 26.0f;

    DWORD color = enabled ? 0xff40ff40 : 0xffff4040;

    // Sombra.
    g_kDrawText(x + 2.0f, y + 2.0f, 0xff000000, 12, text, true);

    // Texto.
    g_kDrawText(x, y, color, 12, text, true);

    InterlockedIncrement(&g_drawCallCount);
}

// Firma compatible con hk_D3D_Present de Kitserver.
// No incluimos d3d8.h: nos alcanza con recibir el puntero como void*.
void STDMETHODCALLTYPE ABPhysicsPresent(
    void* self,
    const RECT* src,
    const RECT* dest,
    HWND hWnd,
    void* unused
)
{
    UNREFERENCED_PARAMETER(self);
    UNREFERENCED_PARAMETER(src);
    UNREFERENCED_PARAMETER(dest);
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(unused);

    InterlockedIncrement(&g_presentCallCount);

    DrawPhysicsMessage();
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------

bool InstallKitserverOverlay()
{
    if (g_overlayInstalled)
        return true;

    if (!g_overlayLockInitialized)
    {
        InitializeCriticalSection(&g_overlayLock);
        g_overlayLockInitialized = true;
    }

    if (!LoadKitserverExports())
    {
        WriteLog("[KSO][WARN] Overlay no instalado.");
        return false;
    }

    g_hookFunction(hk_D3D_Present, (DWORD)ABPhysicsPresent);

    g_overlayInstalled = true;

    WriteLog("[KSO][OK] Overlay instalado en hk_D3D_Present.");

    return true;
}

void UninstallKitserverOverlay()
{
    if (g_overlayInstalled && g_unhookFunction)
    {
        g_unhookFunction(hk_D3D_Present, (DWORD)ABPhysicsPresent);
        g_overlayInstalled = false;
    }

    if (g_overlayLockInitialized)
    {
        DeleteCriticalSection(&g_overlayLock);
        g_overlayLockInitialized = false;
    }

    WriteLog("[KSO] Overlay desinstalado.");
}

void ShowPhysicsModOverlayMessage(bool enabled)
{
    if (g_overlayLockInitialized)
    {
        EnterCriticalSection(&g_overlayLock);
        g_lastEnabled = enabled;
        LeaveCriticalSection(&g_overlayLock);
    }
    else
    {
        g_lastEnabled = enabled;
    }

    g_messageUntilTick = GetTickCount() + 2200;
    InterlockedExchange(&g_drawMessage, 1);

    LogFormat(
        "[KSO] Show message: %s",
        enabled ? "ACTIVADO" : "DESACTIVADO"
    );
}

bool IsKitserverOverlayAvailable()
{
    return g_overlayInstalled;
}