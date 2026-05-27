#include "pch.h"

#include "D3DOverlay.h"
#include "Logger.h"

#include <windows.h>
#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

typedef HRESULT(WINAPI* EndSceneFn)(IDirect3DDevice9* device);

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device);

static EndSceneFn g_originalEndScene = nullptr;
static BYTE g_originalEndSceneBytes[5] = {};
static void* g_endSceneAddress = nullptr;
static bool g_overlayHookInstalled = false;

static volatile LONG g_drawMessage = 0;
static DWORD g_messageUntilTick = 0;
static bool g_lastEnabled = true;

static CRITICAL_SECTION g_messageLock;
static bool g_messageLockInitialized = false;

struct OverlayVertex
{
    float x;
    float y;
    float z;
    float rhw;
    DWORD color;
};

#define OVERLAY_FVF (D3DFVF_XYZRHW | D3DFVF_DIFFUSE)

static bool WriteMemory(void* address, const void* data, size_t size)
{
    DWORD oldProtect = 0;

    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(address, data, size);

    DWORD temp = 0;
    VirtualProtect(address, size, oldProtect, &temp);

    FlushInstructionCache(GetCurrentProcess(), address, size);

    return true;
}

static bool WriteJump5(void* source, void* destination)
{
    BYTE patch[5] = {};
    patch[0] = 0xE9;

    DWORD relative = (DWORD)((uintptr_t)destination - (uintptr_t)source - 5);
    memcpy(&patch[1], &relative, sizeof(DWORD));

    return WriteMemory(source, patch, sizeof(patch));
}

static void RestoreEndSceneBytes()
{
    if (!g_endSceneAddress)
        return;

    WriteMemory(g_endSceneAddress, g_originalEndSceneBytes, sizeof(g_originalEndSceneBytes));
}

static void ReinstallEndSceneHook()
{
    if (!g_endSceneAddress)
        return;

    WriteJump5(g_endSceneAddress, (void*)HookedEndScene);
}

static LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static HWND CreateDummyWindow()
{
    WNDCLASSA wc = {};
    wc.lpfnWndProc = DummyWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "ABGameplayModDummyD3DWindow";

    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        wc.lpszClassName,
        "AB Gameplay Mod Dummy D3D Window",
        WS_OVERLAPPEDWINDOW,
        0,
        0,
        100,
        100,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    return hwnd;
}

static void* GetEndSceneAddress()
{
    HWND hwnd = CreateDummyWindow();

    if (!hwnd)
    {
        WriteLog("[D3D][ERROR] No se pudo crear dummy window.");
        return nullptr;
    }

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);

    if (!d3d)
    {
        DestroyWindow(hwnd);
        WriteLog("[D3D][ERROR] Direct3DCreate9 fallo.");
        return nullptr;
    }

    D3DPRESENT_PARAMETERS params = {};
    params.Windowed = TRUE;
    params.SwapEffect = D3DSWAPEFFECT_DISCARD;
    params.hDeviceWindow = hwnd;
    params.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9* device = nullptr;

    HRESULT hr = d3d->CreateDevice(
        D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL,
        hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,
        &params,
        &device
    );

    if (FAILED(hr) || !device)
    {
        WriteLog("[D3D][WARN] HAL dummy device fallo. Probando REF.");

        hr = d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_REF,
            hwnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING,
            &params,
            &device
        );
    }

    if (FAILED(hr) || !device)
    {
        d3d->Release();
        DestroyWindow(hwnd);

        WriteLog("[D3D][ERROR] No se pudo crear dummy device.");
        return nullptr;
    }

    void** vtable = *(void***)device;

    // IDirect3DDevice9::EndScene = vtable[42]
    void* endScene = vtable[42];

    device->Release();
    d3d->Release();
    DestroyWindow(hwnd);

    return endScene;
}

static void DrawFilledRect(
    IDirect3DDevice9* device,
    float x,
    float y,
    float width,
    float height,
    DWORD color
)
{
    OverlayVertex vertices[4] =
    {
        { x,         y,          0.0f, 1.0f, color },
        { x + width, y,          0.0f, 1.0f, color },
        { x,         y + height, 0.0f, 1.0f, color },
        { x + width, y + height, 0.0f, 1.0f, color },
    };

    DWORD oldFvf = 0;
    device->GetFVF(&oldFvf);

    device->SetTexture(0, nullptr);
    device->SetFVF(OVERLAY_FVF);

    device->DrawPrimitiveUP(
        D3DPT_TRIANGLESTRIP,
        2,
        vertices,
        sizeof(OverlayVertex)
    );

    device->SetFVF(oldFvf);
}

static void DrawOverlayBanner(IDirect3DDevice9* device)
{
    if (InterlockedCompareExchange(&g_drawMessage, 0, 0) == 0)
        return;

    DWORD now = GetTickCount();

    if (now > g_messageUntilTick)
    {
        InterlockedExchange(&g_drawMessage, 0);
        return;
    }

    bool enabled = true;

    if (g_messageLockInitialized)
    {
        EnterCriticalSection(&g_messageLock);
        enabled = g_lastEnabled;
        LeaveCriticalSection(&g_messageLock);
    }
    else
    {
        enabled = g_lastEnabled;
    }

    D3DVIEWPORT9 viewport = {};

    if (FAILED(device->GetViewport(&viewport)))
        return;

    float bannerWidth = 420.0f;
    float bannerHeight = 34.0f;

    float x = ((float)viewport.Width - bannerWidth) / 2.0f;
    float y = 24.0f;

    DWORD bgColor = D3DCOLOR_ARGB(185, 0, 0, 0);

    DWORD stateColor = enabled
        ? D3DCOLOR_ARGB(230, 40, 220, 70)
        : D3DCOLOR_ARGB(230, 230, 60, 60);

    // Fondo negro.
    DrawFilledRect(device, x, y, bannerWidth, bannerHeight, bgColor);

    // Barra de estado arriba.
    DrawFilledRect(device, x, y, bannerWidth, 5.0f, stateColor);

    // Indicador central: verde = activado, rojo = desactivado.
    DrawFilledRect(
        device,
        x + (bannerWidth / 2.0f) - 45.0f,
        y + 12.0f,
        90.0f,
        10.0f,
        stateColor
    );
}

HRESULT WINAPI HookedEndScene(IDirect3DDevice9* device)
{
    RestoreEndSceneBytes();

    DrawOverlayBanner(device);

    HRESULT result = g_originalEndScene(device);

    ReinstallEndSceneHook();

    return result;
}

bool InstallD3DOverlayHook()
{
    if (g_overlayHookInstalled)
        return true;

    if (!g_messageLockInitialized)
    {
        InitializeCriticalSection(&g_messageLock);
        g_messageLockInitialized = true;
    }

    void* endScene = GetEndSceneAddress();

    if (!endScene)
    {
        WriteLog("[D3D][ERROR] No se encontro EndScene.");
        return false;
    }

    g_endSceneAddress = endScene;
    g_originalEndScene = (EndSceneFn)endScene;

    memcpy(g_originalEndSceneBytes, g_endSceneAddress, sizeof(g_originalEndSceneBytes));

    if (!WriteJump5(g_endSceneAddress, (void*)HookedEndScene))
    {
        WriteLog("[D3D][ERROR] No se pudo instalar hook EndScene.");
        return false;
    }

    g_overlayHookInstalled = true;

    LogFormat(
        "[D3D][OK] Hook EndScene instalado en 0x%08X",
        (unsigned int)(uintptr_t)g_endSceneAddress
    );

    return true;
}

void ShutdownD3DOverlay()
{
    if (g_overlayHookInstalled && g_endSceneAddress)
    {
        RestoreEndSceneBytes();
        g_overlayHookInstalled = false;
    }

    if (g_messageLockInitialized)
    {
        DeleteCriticalSection(&g_messageLock);
        g_messageLockInitialized = false;
    }

    WriteLog("[D3D] Overlay apagado.");
}

void ShowPhysicsModOverlayMessage(bool enabled)
{
    if (g_messageLockInitialized)
    {
        EnterCriticalSection(&g_messageLock);
        g_lastEnabled = enabled;
        LeaveCriticalSection(&g_messageLock);
    }
    else
    {
        g_lastEnabled = enabled;
    }

    g_messageUntilTick = GetTickCount() + 2200;
    InterlockedExchange(&g_drawMessage, 1);
}