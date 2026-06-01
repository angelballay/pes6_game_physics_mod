#include "pch.h"
#include "BallActionGuards.h"

#include "PesAddresses.h"

#include <windows.h>
#include <cstdint>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;

    // Tabla de jugadores reconstruida por RE:
    // 03BDE240 era Ronaldinho con id actor 11.
    // Por lo tanto id 0 ~= 03BDC980 = pes6.exe+37DC980.
    constexpr uintptr_t PLAYER_TABLE_BASE = 0x37DC980;
    constexpr uintptr_t PLAYER_STRIDE = 0x240;

    // No conocemos el total real de estructuras activas.
    // Escaneamos una ventana amplia y descartamos candidatos invalidos.
    constexpr int PLAYER_SCAN_COUNT = 48;

    // Global confirmado por RE:
    // pes6.exe+37E09CC guarda el id del actor que tiene/controla la pelota.
    constexpr uintptr_t G_BALL_ACTOR_ID = 0x37E09CC;

    // Firmas vistas en player+B0:
    // D / centro por arriba: 0x00122000, a veces con bits extra de direccion.
    // A / tiro:             0x00488000.
    constexpr uint32_t CROSS_MASK = 0x00122000;
    constexpr uint32_t SHOT_MASK  = 0x00488000;

    constexpr DWORD ACTION_GUARD_LATCH_MS = 700;

    static DWORD g_lastFieldActionGuardTick = 0;
    static DWORD g_lastKeeperActionGuardTick = 0;

    bool IsRecentTick(DWORD tick, DWORD windowMs)
    {
        if (tick == 0)
            return false;

        return GetTickCount() - tick <= windowMs;
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

    uintptr_t GetBall(uintptr_t pesBase)
    {
        if (!pesBase)
            return 0;

        return ReadOr<uintptr_t>(pesBase + PesAddresses::BALL_GLOBAL_PTR, 0);
    }

    DWORD GetBallState(uintptr_t ball)
    {
        if (!ball)
            return 0xFFFFFFFF;

        return ReadOr<DWORD>(ball + PesOffsets::BALL_STATE, 0xFFFFFFFF);
    }

    uint8_t GetPlayerActorId(uintptr_t player)
    {
        return ReadOr<uint8_t>(player + 0x00, 0xFF);
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
        const uint8_t id = GetPlayerActorId(player);
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);

        // En pruebas vimos ids reales como 1, 10, 11, 12, etc.
        // A partir de cierto punto de la ventana aparecen estructuras basura;
        // si el id es demasiado alto, se descarta.
        if (id == 0xFF || id > 31)
            return false;

        // Las estructuras validas observadas tienen puntero de animacion/estado en +04.
        if (!animPtr)
            return false;

        return true;
    }

    uintptr_t GetPlayerBySlot(uintptr_t pesBase, int slot)
    {
        if (!pesBase || slot < 0 || slot >= PLAYER_SCAN_COUNT)
            return 0;

        return pesBase + PLAYER_TABLE_BASE + (PLAYER_STRIDE * static_cast<uintptr_t>(slot));
    }

    uintptr_t FindPlayerByActorId(uintptr_t pesBase, uint8_t wantedId)
    {
        if (!pesBase || wantedId == 0xFF)
            return 0;

        for (int i = 0; i < PLAYER_SCAN_COUNT; ++i)
        {
            const uintptr_t player = GetPlayerBySlot(pesBase, i);

            if (!LooksLikeValidPlayer(player))
                continue;

            if (GetPlayerActorId(player) == wantedId)
                return player;
        }

        return 0;
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

        // Firmas ya conocidas:
        // - Centro D / acción aérea: B0 0x00122000
        // - Tiro A: B0 0x00488000
        // - Acciones de golpeo: p4D 16 / 24
        const bool knownKick =
            p4D == 16 ||
            p4D == 24 ||
            hasCrossMask ||
            hasShotMask;

        // Nuevo caso: centro D desde derecha / banda.
        // Log observado:
        // p4F = 6
        // anim30 = 0x0226
        // ball84 todavía 0.
        const bool rightSideCrossVariant =
            p4F == 6 &&
            anim30 == 0x0226;

        // Nuevo caso: centro D en estado ball84 = 5.
        // Log observado:
        // p16 = 32
        // anim30 = 0x00EB
        // p4D = 0
        // B0 = 0
        const bool possessionCrossVariant =
            p16 == 32 &&
            anim30 == 0x00EB;

        // Nuevo caso: Q + W pase alto.
        // Log observado:
        // p16 = 5
        // p18 = 88
        // anim30 = 0x00E2
        // p4F en rango alto/decreciente.
        const bool lobPassQW =
            p16 == 5 &&
            p18 == 88 &&
            anim30 == 0x00E2 &&
            p4F >= 20 &&
            p4F <= 60;

        return knownKick ||
            rightSideCrossVariant ||
            possessionCrossVariant ||
            lobPassQW;
    }

    bool AnyFieldKickOrCrossPreparing(uintptr_t pesBase)
    {
        if (!pesBase)
            return false;

        const uintptr_t tableBase = pesBase + PLAYER_TABLE_BASE;

        for (int i = 0; i < PLAYER_SCAN_COUNT; ++i)
        {
            const uintptr_t player = tableBase + PLAYER_STRIDE * i;

            if (!LooksLikeValidPlayer(player))
                continue;

            if (IsFieldKickOrCrossPreparing(player))
                return true;
        }

        return false;
    }

    bool IsGoalkeeperKickPreparing(uintptr_t player, DWORD ballState)
    {
        if (!player || ballState != BALL_STATE_POSSESSION)
            return false;

        const uint8_t p16 = ReadOr<uint8_t>(player + 0x16, 0);
        const uint8_t p4F = ReadOr<uint8_t>(player + 0x4F, 0);
        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint16_t anim30 = GetAnim30(player);

        const bool hasCrossMask = (b0 & CROSS_MASK) == CROSS_MASK;

        // Saques de arquero real observados:
        // - p16 129 / 130 / 148 / 149 durante preparacion.
        // - anim30 0x0158 / 0x0275 / 0x0391 / 0x03E7.
        // - p4F puede valer 46, 24, 1, etc. Lo usamos como apoyo.
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

        const bool keeperP4F =
            p4F == 46 ||
            p4F == 24 ||
            p4F == 1;

        return keeperP16 ||
               keeperAnim ||
               hasCrossMask ||
               keeperP4F;
    }
}

bool ShouldUseOverallBallWeightForActionGuard(uintptr_t pesBase)
{
    const uintptr_t ball = GetBall(pesBase);
    if (ball == 0)
        return true;

    const uint32_t ballState = ReadOr<uint32_t>(ball + 0x84, 0xFFFFFFFF);
    const DWORD now = GetTickCount();

    // Jugador de campo: no dependemos de g37E09CC,
    // porque en algunas acciones la señal aparece antes o dura muy poco.
    const bool fieldGuardNow = AnyFieldKickOrCrossPreparing(pesBase);

    if (fieldGuardNow)
        g_lastFieldActionGuardTick = now;

    if (fieldGuardNow || IsRecentTick(g_lastFieldActionGuardTick, ACTION_GUARD_LATCH_MS))
        return true;

    // Arquero: usamos actorId porque acá sí nos funcionó bien.
    const uint8_t actorId = ReadOr<uint8_t>(pesBase + G_BALL_ACTOR_ID, 0xFF);
    const uintptr_t actor = FindPlayerByActorId(pesBase, actorId);

    const bool keeperGuardNow =
        actor != 0 && IsGoalkeeperKickPreparing(actor, ballState);

    if (keeperGuardNow)
        g_lastKeeperActionGuardTick = now;

    if (keeperGuardNow || IsRecentTick(g_lastKeeperActionGuardTick, ACTION_GUARD_LATCH_MS))
        return true;

    return false;
}
