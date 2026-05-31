#include "pch.h"
#include "BallActionGuards.h"

#include <windows.h>
#include <cstdint>

namespace
{
    // -------------------------------------------------------------------------
    // Direcciones PES6
    // -------------------------------------------------------------------------

    // [pes6.exe+7CCE94] = puntero dinámico a la pelota
    constexpr uintptr_t BALL_PTR_ADDR = 0x007CCE94;

    // pes6.exe+37E09CC = id del actor/jugador que tiene o controla la pelota.
    // Confirmado por RE.
    constexpr uintptr_t G_BALL_ACTOR_ID = 0x0037E09CC;

    // Tabla de jugadores reconstruida por RE:
    // 0x03BDE240 era Ronaldinho con id 11.
    // Entonces id 0 aproximado:
    // 0x03BDE240 - 11 * 0x240 = 0x03BDC980
    //
    // Como trabajamos con base relativa al módulo:
    // 0x03BDC980 - pes6.exe base 0x00400000 = 0x37DC980
    constexpr uintptr_t PLAYER_TABLE_BASE = 0x0037DC980;
    constexpr uintptr_t PLAYER_STRIDE = 0x240;

    // No conocemos el total real de estructuras activas.
    // Escaneamos una ventana amplia y validamos candidatos.
    constexpr int PLAYER_SCAN_COUNT = 48;

    // -------------------------------------------------------------------------
    // Firmas observadas por RE
    // -------------------------------------------------------------------------

    // B0 bitmask observado para centro con D / acción aérea.
    constexpr uint32_t CROSS_MASK = 0x00122000;

    // B0 bitmask observado para tiro con A.
    constexpr uint32_t SHOT_MASK = 0x00488000;

    // -------------------------------------------------------------------------
    // Safe reads
    // -------------------------------------------------------------------------

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

        return ReadOr<uintptr_t>(pesBase + BALL_PTR_ADDR, 0);
    }

    uint8_t GetPlayerActorId(uintptr_t player)
    {
        return ReadOr<uint8_t>(player + 0x00, 0xFF);
    }

    uint16_t GetAnim30(uintptr_t player)
    {
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);
        if (animPtr == 0)
            return 0;

        return ReadOr<uint16_t>(animPtr + 0x30, 0);
    }

    bool LooksLikeValidPlayer(uintptr_t player)
    {
        const uint8_t id = ReadOr<uint8_t>(player + 0x00, 0xFF);
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);

        // En nuestras pruebas vimos ids reales tipo 1, 10, 11, 12...
        // Si es basura alta, descartamos.
        if (id == 0xFF || id > 31)
            return false;

        // La mayoría de estructuras válidas tienen puntero de animación/estado en +04.
        if (animPtr == 0)
            return false;

        return true;
    }

    uintptr_t FindPlayerByActorId(uintptr_t pesBase, uint8_t wantedId)
    {
        if (!pesBase || wantedId == 0xFF)
            return 0;

        const uintptr_t tableBase = pesBase + PLAYER_TABLE_BASE;

        for (int i = 0; i < PLAYER_SCAN_COUNT; ++i)
        {
            const uintptr_t player = tableBase + PLAYER_STRIDE * i;

            if (!LooksLikeValidPlayer(player))
                continue;

            const uint8_t id = GetPlayerActorId(player);

            if (id == wantedId)
                return player;
        }

        return 0;
    }

    // -------------------------------------------------------------------------
    // Guardas de jugador de campo
    // -------------------------------------------------------------------------

    bool IsFieldKickOrCrossPreparing(uintptr_t player)
    {
        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint8_t p4D = ReadOr<uint8_t>(player + 0x4D, 0);

        const bool hasCrossMask = (b0 & CROSS_MASK) == CROSS_MASK;
        const bool hasShotMask = (b0 & SHOT_MASK) == SHOT_MASK;

        // Observado:
        // - D centro puede marcar B0 = 0x00122000, pero no siempre.
        // - A tiro puede marcar B0 = 0x00488000.
        // - p4D = 16 o 24 aparece temprano en varias acciones de golpeo.
        //
        // Para corregir el visor, preferimos ser algo amplios:
        // si el jugador está preparando golpeo/centro/tiro, volvemos a overall.
        return p4D == 16 ||
            p4D == 24 ||
            hasCrossMask ||
            hasShotMask;
    }

    // -------------------------------------------------------------------------
    // Guardas de arquero
    // -------------------------------------------------------------------------

    bool IsGoalkeeperKickPreparing(uintptr_t player, uint32_t ballState)
    {
        // La preparación de saque de arquero que nos interesa ocurre antes
        // de que la pelota salga. En logs, cuando sale pasa a ball+84 = 7.
        if (ballState != 0)
            return false;

        const uint8_t p16 = ReadOr<uint8_t>(player + 0x16, 0);
        const uint8_t p4F = ReadOr<uint8_t>(player + 0x4F, 0);
        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint16_t anim30 = GetAnim30(player);

        const bool hasCrossMask = (b0 & CROSS_MASK) == CROSS_MASK;

        // Observado en arquero real:
        // - p16 = 129 / 130 / 148 / 149
        // - anim30 = 0x0158 / 0x0275 / 0x0391 / 0x03E7
        // - p4F puede tomar valores como 46, 24, 1 según fase/contexto.
        //
        // No usamos g37E09CC acá porque ya usamos g37E09CC para encontrar
        // al actor correcto antes de llamar a esta función.
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

// -----------------------------------------------------------------------------
// API pública del módulo
// -----------------------------------------------------------------------------

bool ShouldUseOverallBallWeightForActionGuard(uintptr_t pesBase)
{
    const uintptr_t ball = GetBall(pesBase);
    if (ball == 0)
        return true;

    const uint32_t ballState = ReadOr<uint32_t>(ball + 0x84, 0xFFFFFFFF);

    // pes6.exe+37E09CC = id del actor que tiene/controla la pelota.
    const uint8_t actorId = ReadOr<uint8_t>(pesBase + G_BALL_ACTOR_ID, 0xFF);

    const uintptr_t actor = FindPlayerByActorId(pesBase, actorId);

    // Si no encontramos actor, no forzamos la guarda.
    // Dejamos que BallWeightController decida por ballState.
    if (actor == 0)
        return false;

    if (IsFieldKickOrCrossPreparing(actor))
        return true;

    if (IsGoalkeeperKickPreparing(actor, ballState))
        return true;

    return false;
}