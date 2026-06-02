#include "pch.h"
#include "BallActionGuards.h"

#include "GameplayConfig.h"

#include <windows.h>
#include <cstdint>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;

    // Firmas vistas en player+B0:
    // D / centro por arriba: 0x00122000, a veces con bits extra de direccion.
    // A / tiro:             0x00488000.
    constexpr uint32_t CROSS_MASK = 0x00122000;
    constexpr uint32_t SHOT_MASK  = 0x00488000;

    static ULONGLONG g_lastProtectedActionTick = 0;

    bool IsRecentTick(ULONGLONG tick, ULONGLONG windowMs)
    {
        if (tick == 0)
            return false;

        return GetTickCount64() - tick <= windowMs;
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

    uint16_t GetAnim30(uintptr_t player)
    {
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);
        if (!animPtr)
            return 0;

        return ReadOr<uint16_t>(animPtr + 0x30, 0);
    }

    bool LooksLikeValidPlayer(uintptr_t player)
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

    bool IsFieldKickOrCrossPreparing(uintptr_t player)
    {
        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint8_t p16 = ReadOr<uint8_t>(player + 0x16, 0);
        const uint16_t p18 = ReadOr<uint16_t>(player + 0x18, 0);
        const uint8_t p4D = ReadOr<uint8_t>(player + 0x4D, 0);
        const uint8_t p4F = ReadOr<uint8_t>(player + 0x4F, 0);
        const uint16_t anim30 = GetAnim30(player);

        const bool hasCrossMask = (b0 & CROSS_MASK) == CROSS_MASK;
        const bool hasShotMask = (b0 & SHOT_MASK) == SHOT_MASK;

        const bool knownKick =
            p4D == 16 ||
            p4D == 24 ||
            hasCrossMask ||
            hasShotMask;

        // Centro D / centro corriendo. Importante:
        // en algunas fases b0 pierde CROSS_MASK, pero p16=9 + anim30 sigue marcando centro.
        const bool crossByP16 =
            p16 == 9 &&
            (
                anim30 == 0x0011 ||
                anim30 == 0x0018 ||
                anim30 == 0x0224 ||
                anim30 == 0x0226 ||
                anim30 == 0x0227 ||
                anim30 == 0x00EC ||
                anim30 == 0x00EB
                );

        // Q + W / pase alto especial.
        const bool lobPassQW =
            p16 == 5 &&
            p18 == 88 &&
            anim30 == 0x00E2 &&
            p4F >= 20 &&
            p4F <= 60;

        // Variante vieja que ya habíamos visto.
        const bool possessionCrossVariant =
            p16 == 32 &&
            anim30 == 0x00EB;

        return
            knownKick ||
            crossByP16 ||
            possessionCrossVariant ||
            lobPassQW;
    }

    bool IsGoalkeeperKickPreparing(uintptr_t player, DWORD ballState)
    {
        if (!player || ballState != BALL_STATE_POSSESSION)
            return false;

        const uint8_t p16 = ReadOr<uint8_t>(player + 0x16, 0);
        const uint16_t anim30 = GetAnim30(player);

        const bool keeperP16 =
            p16 == 129 ||
            p16 == 130 ||
            p16 == 148 ||
            p16 == 149;

        const bool keeperAnim =
            anim30 == 0x0158 ||
            anim30 == 0x0275 ||
            anim30 == 0x0391 ||
            anim30 == 0x03E7;

        return keeperP16 || keeperAnim;
    }
}

bool ShouldForceOverallForProtectedAction(uintptr_t player, DWORD ballState)
{
    const ULONGLONG now = GetTickCount64();
    const ULONGLONG latchMs = static_cast<ULONGLONG>(GetProtectedActionLatchMs());

    bool protectedNow = false;

    if (LooksLikeValidPlayer(player))
    {
        protectedNow =
            IsFieldKickOrCrossPreparing(player) ||
            IsGoalkeeperKickPreparing(player, ballState);
    }

    if (protectedNow)
        g_lastProtectedActionTick = now;

    return protectedNow || IsRecentTick(g_lastProtectedActionTick, latchMs);
}
