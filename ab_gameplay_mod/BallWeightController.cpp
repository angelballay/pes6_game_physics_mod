#include "pch.h"
#include "BallWeightController.h"

#include "GameplayConfig.h"
#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"
#include "BallActionGuards.h"
#include "BallActorTracker.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;
    constexpr DWORD BALL_STATE_PASS_OR_LOOSE = 1;
    constexpr float VANILLA_BALL_WEIGHT = 188.0f;

    static volatile LONG g_controllerRunning = 0;
    static HANDLE g_controllerThread = nullptr;
    static uintptr_t g_pesBase = 0;

    bool g_wasR2Held = false;
    ULONGLONG g_r2ChargeUntilMs = 0;

    static DWORD FloatToBits(float value)
    {
        DWORD bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }


    template <typename T>
    bool SafeRead(uintptr_t address, T& out)
    {
        __try
        {
            out = *reinterpret_cast<T*>(address);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    template <typename T>
    T ReadOr(uintptr_t address, T fallback = T{})
    {
        T value{};
        return SafeRead<T>(address, value) ? value : fallback;
    }

    static bool ReadBallState(DWORD* outState)
    {
        if (!outState) return false;

        __try
        {
            DWORD ballBase = *(DWORD*)(g_pesBase + PesAddresses::BALL_GLOBAL_PTR);
            if (!ballBase)
                return false;

            *outState = *(DWORD*)(ballBase + PesOffsets::BALL_STATE);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ApplyBallWeightIfChanged(float value, DWORD* lastBits)
    {
        DWORD bits = FloatToBits(value);
        if (lastBits && *lastBits == bits)
            return;

        uintptr_t address = g_pesBase + PesAddresses::BALL_WEIGHT_STATIC;
        if (WriteFloat(address, value))
        {
            if (lastBits)
                *lastBits = bits;
        }
    }


    static bool KeyDown(int vk)
    {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    static uint16_t GetAnim30(uintptr_t player)
    {
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);
        if (!animPtr)
            return 0;

        return ReadOr<uint16_t>(animPtr + 0x30, 0);
    }

    static bool LooksLikeValidPlayer(uintptr_t player)
    {
        if (player < 0x01000000 || player > 0x08000000)
            return false;

        const uint8_t id = ReadOr<uint8_t>(player + 0x00, 0xFF);
        if (id == 0xFF || id > 31)
            return false;

        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);
        if (!animPtr)
            return false;

        return true;
    }

    static bool IsInternalR1Sprint(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint8_t p16 = ReadOr<uint8_t>(player + 0x16, 0);
        const uint8_t p4F = ReadOr<uint8_t>(player + 0x4F, 0);
        const uint16_t anim30 = GetAnim30(player);

        // R1 observado:
        // B0 0x01000820 / 0x01000880 / variantes con direccion.
        // p16 2, p4F 4/8, anim30 0x001C/0x001D/0x001E/0x02E6/0x02E9.
        const bool b0R1 = (b0 & 0x01000800) == 0x01000800;
        const bool animR1 =
            p16 == 2 &&
            (p4F == 4 || p4F == 8) &&
            (anim30 == 0x001C ||
             anim30 == 0x001D ||
             anim30 == 0x001E ||
             anim30 == 0x02E6 ||
             anim30 == 0x02E9);

        return b0R1 || animR1;
    }

    static bool IsInternalR2Control(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);

        // R2 observado:
        // B0 0x02000220 / 0x02000240 / 0x02000280 / variantes con direccion.
        return (b0 & 0x02000200) == 0x02000200;
    }

    static uintptr_t GetRecentActorPlayer()
    {
        if (!HasRecentBallActor(1000))
            return 0;

        const uintptr_t player = GetBallActorPlayer();
        return LooksLikeValidPlayer(player) ? player : 0;
    }

    static float ResolveTargetBallWeight(DWORD ballState)
    {
        const float overall = GetOverallBallWeight();

        // Reglas globales por estado de pelota.
        if (ballState == BALL_STATE_PASS_OR_LOOSE)
            return GetBallWeightState1();

        if (ballState != BALL_STATE_POSSESSION)
            return overall;

        const uintptr_t player = GetRecentActorPlayer();

        // Guardas visuales: centros, tiros, Q+W/pase alto y saques de arquero
        // deben usar overall para no contaminar predictores de caida/trayectoria.
        if (ShouldForceOverallForProtectedAction(player, ballState))
            return overall;

        // Input real del usuario. Mapeo confirmado:
        // R1 = E, R2 = C.
        const bool inputR1 = KeyDown('E');
        const bool inputR2 = KeyDown('C');

        // Fallback interno desde la estructura del jugador activo.
        const bool internalR1 = IsInternalR1Sprint(player);
        const bool internalR2 = IsInternalR2Control(player);

        const bool r1Held = inputR1 || internalR1;
        const bool r2Held = inputR2 || internalR2;

        // No hay caso especial de doble R1: si se detecta como R1, se comporta
        // como sprint R1 normal.
        if (r1Held && r2Held)
            return GetBallWeightR1R2();

        if (r1Held)
            return GetBallWeightR1();

        if (r2Held)
            return GetBallWeightR2();

        return GetBallWeightNormalDribble();
    }

    static DWORD WINAPI BallWeightThread(LPVOID)
    {
        DWORD lastAppliedBits = 0xFFFFFFFF;

        LogFormat(
            "[BALL_WEIGHT] Controller iniciado. overall=%.3f state1=%.3f normal=%.3f r1=%.3f r2=%.3f r1r2=%.3f",
            GetOverallBallWeight(),
            GetBallWeightState1(),
            GetBallWeightNormalDribble(),
            GetBallWeightR1(),
            GetBallWeightR2(),
            GetBallWeightR1R2()
        );

        while (InterlockedCompareExchange(&g_controllerRunning, 0, 0) != 0)
        {
            Sleep(10);

            DWORD state = 0xFFFFFFFF;
            if (!ReadBallState(&state))
                state = 0xFFFFFFFF;

            const float target = ResolveTargetBallWeight(state);
            ApplyBallWeightIfChanged(target, &lastAppliedBits);
        }

        ApplyBallWeightIfChanged(VANILLA_BALL_WEIGHT, &lastAppliedBits);
        WriteLog("[BALL_WEIGHT] Controller finalizado. Peso restaurado a vanilla.");
        return 0;
    }
}

bool StartBallWeightController(uintptr_t pesBase)
{
    if (InterlockedCompareExchange(&g_controllerRunning, 0, 0) != 0)
        return true;

    if (!pesBase)
    {
        WriteLog("[BALL_WEIGHT] No se puede iniciar: pesBase=0.");
        return false;
    }

    g_pesBase = pesBase;
    InterlockedExchange(&g_controllerRunning, 1);

    g_controllerThread = CreateThread(
        nullptr,
        0,
        BallWeightThread,
        nullptr,
        0,
        nullptr
    );

    if (!g_controllerThread)
    {
        InterlockedExchange(&g_controllerRunning, 0);
        WriteLog("[BALL_WEIGHT] ERROR creando thread.");
        return false;
    }

    return true;
}

void StopBallWeightController()
{
    if (InterlockedCompareExchange(&g_controllerRunning, 0, 0) == 0)
        return;

    InterlockedExchange(&g_controllerRunning, 0);

    if (g_controllerThread)
    {
        WaitForSingleObject(g_controllerThread, 500);
        CloseHandle(g_controllerThread);
        g_controllerThread = nullptr;
    }
}
