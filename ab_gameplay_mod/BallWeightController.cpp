#include "pch.h"
#include "BallWeightController.h"

#include "GameplayConfig.h"
#include "Logger.h"
#include "MemoryPatch.h"
#include "ModState.h"
#include "PesAddresses.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;
    constexpr float VANILLA_BALL_WEIGHT = 188.0f;

    static volatile LONG g_controllerRunning = 0;
    static HANDLE g_controllerThread = nullptr;
    static uintptr_t g_pesBase = 0;

    static DWORD FloatToBits(float value)
    {
        DWORD bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
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

    static DWORD WINAPI BallWeightThread(LPVOID)
    {
        DWORD lastAppliedBits = 0xFFFFFFFF;

        LogFormat(
            "[BALL_WEIGHT] Controller iniciado. overall=%.3f possession=%.3f",
            GetOverallBallWeight(),
            GetPossessionBallWeight()
        );

        while (InterlockedCompareExchange(&g_controllerRunning, 0, 0) != 0)
        {
            Sleep(10);

            if (!IsPhysicsModEnabled())
            {
                ApplyBallWeightIfChanged(VANILLA_BALL_WEIGHT, &lastAppliedBits);
                continue;
            }

            DWORD state = 0xFFFFFFFF;
            float target = GetOverallBallWeight();

            if (ReadBallState(&state) && state == BALL_STATE_POSSESSION)
            {
                target = GetPossessionBallWeight();
            }

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
