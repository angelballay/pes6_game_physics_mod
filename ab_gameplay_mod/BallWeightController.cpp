#include "pch.h"
#include "BallWeightController.h"

#include "BallActionGuards.h"
#include "BallActorTracker.h"
#include "GameplayConfig.h"
#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;
    constexpr DWORD BALL_STATE_PASS_OR_LOOSE = 1;

    static bool g_proL2FirstBlocked = false;
    static uintptr_t g_proL2FirstBlockedPlayer = 0;


    static bool g_proL2HoldAuthorized = false;
    static bool g_proL2HoldBlocked = false;
    static uintptr_t g_proL2HoldPlayer = 0;

    static bool g_proTechnicalAuthorized = false;
    static uintptr_t g_proTechnicalAuthorizedPlayer = 0;

    uintptr_t g_pesBase = 0;

    volatile LONG g_running = 0;
    HANDLE g_thread = nullptr;

    DWORD g_lastAppliedBits = 0;

    // Estado nuevo para R1 + R2 + cambio direccional.
    // Reemplaza el algoritmo viejo por InputDir/g_r2Charge para evitar duplicidad.
    uint32_t g_lastR1OnlyDir = 0;
    uintptr_t g_lastR1OnlyPlayer = 0;

    uint32_t g_prevR1OnlyDir = 0;
    ULONGLONG g_prevR1OnlyDirTick = 0;

    bool g_prevR2Held = false;
    bool g_r2ChargeConsumedForHold = false;

    ULONGLONG g_r2ChargeUntilMs = 0;
    uintptr_t g_r2ChargePlayer = 0;
    uint32_t g_r2ChargeFromDir = 0;
    uint32_t g_r2ChargeToDir = 0;

    // Estado exclusivo de ProBoost. No se comparte con BoostMode clasico.
    uint32_t g_proLastTechDir = 0;
    uintptr_t g_proLastTechPlayer = 0;

    uint32_t g_proPrevTechDir = 0;
    ULONGLONG g_proPrevTechDirTick = 0;

    bool g_proPrevR2Held = false;
    bool g_proChargeConsumedForHold = false;

    ULONGLONG g_proChargeUntilMs = 0;
    uintptr_t g_proChargePlayer = 0;
    uint32_t g_proChargeFromDir = 0;
    uint32_t g_proChargeToDir = 0;

    // Armado ProBoost: evita que L2 primero + R1 active 305.
    bool g_proR1OnlyArmed = false;
    uintptr_t g_proR1OnlyPlayer = 0;
    ULONGLONG g_proR1OnlyTick = 0;

    constexpr uint32_t INPUT_DIR_MASK = 0x000000F0;
    constexpr uint32_t R1_MASK = 0x01000800;
    constexpr uint32_t R2_MASK = 0x02000200;
    constexpr uint32_t CROSS_MASK = 0x00122000;
    constexpr uint32_t SHOT_MASK = 0x00488000;

    static DWORD FloatToBits(float value)
    {
        DWORD bits = 0;
        memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    template <typename T>
    T ReadOr(uintptr_t address, T fallback = T{})
    {
        T value{};
        return SafeRead<T>(address, value) ? value : fallback;
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

    static bool ReadBallState(DWORD* outState)
    {
        if (!outState || !g_pesBase)
            return false;

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

    static uint8_t ReadCurrentBallActorId()
    {
        if (!g_pesBase)
            return 0xFF;

        return ReadOr<uint8_t>(g_pesBase + PesAddresses::BALL_ACTOR_ID, 0xFF);
    }

    static uintptr_t GetActivePlayerFromGlobal()
    {
        if (!g_pesBase)
            return 0;

        const uintptr_t player =
            ReadOr<uintptr_t>(g_pesBase + PesAddresses::ACTIVE_PLAYER_PTR, 0);

        return LooksLikeValidPlayer(player) ? player : 0;
    }

    static bool PlayerMatchesCurrentBallActor(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint8_t currentActorId = ReadCurrentBallActorId();
        const uint8_t playerId = ReadOr<uint8_t>(player + 0x00, 0xFF);

        if (currentActorId == 0xFF || currentActorId == 0)
            return false;

        return playerId == currentActorId;
    }

    static bool PlayerHasUsefulControlInput(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);

        const bool r1 =
            (b0 & 0x01000800) == 0x01000800;

        const bool r2 =
            (b0 & 0x02000200) == 0x02000200;

        const uint32_t l2Mask = GetL2Mask();

        const bool l2 =
            l2Mask != 0 &&
            (b0 & l2Mask) == l2Mask;

        // Importante:
        // L2 también debe contar como input útil para que el controller
        // pueda ver el caso L2 primero y bloquear ProBoost.
        return r1 || r2 || l2;
    }
    static bool PlayerIsCloseEnoughToBall(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t p114 = ReadOr<uint32_t>(player + 0x114, 999999);

        // Usamos una distancia ya existente/configurable.
        // Para conducción R1 recta debería estar cerca.
        return p114 <= GetR2ChargeKeepDistMax();
    }

    static bool IsLiveBallOwnerCandidate(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        if (!PlayerMatchesCurrentBallActor(player))
            return false;

        if (!PlayerIsCloseEnoughToBall(player))
            return false;

        if (!PlayerHasUsefulControlInput(player))
            return false;

        return true;
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

    static uint16_t GetAnim30(uintptr_t player)
    {
        const uintptr_t animPtr = ReadOr<uintptr_t>(player + 0x04, 0);

        if (!animPtr)
            return 0;

        return ReadOr<uint16_t>(animPtr + 0x30, 0);
    }



    static bool GetRecentTouchForSamePlayer(
        uintptr_t player,
        BallTouchDebugSnapshot* out,
        ULONGLONG maxAgeMs)
    {
        if (!out || !LooksLikeValidPlayer(player))
            return false;

        BallTouchDebugSnapshot touch = {};
        const ULONGLONG now = GetTickCount64();

        if (!GetLastTouchDebugSnapshot(&touch))
            return false;

        if (touch.tick == 0 || now - touch.tick > maxAgeMs)
            return false;

        if (touch.player != player)
            return false;

        if (touch.ball84 != BALL_STATE_POSSESSION)
            return false;

        *out = touch;
        return true;
    }

    static bool IsInternalR1Sprint(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint32_t dirBits = b0 & 0xF0;

        const bool b0R1 =
            (b0 & 0x01000800) == 0x01000800;

        if (b0R1)
            return true;

        // Fallback corto solamente si el último touch real tenía R1.
        // No usar animaciones para inventar R1, porque trote+giro puede parecer sprint.
        BallTouchDebugSnapshot touch = {};
        if (GetRecentTouchForSamePlayer(player, &touch, 150))
        {
            const bool sameDir =
                touch.dirBits != 0 &&
                dirBits != 0 &&
                touch.dirBits == dirBits;

            if (touch.r1 && sameDir)
                return true;
        }

        return false;
    }

    static bool IsConfiguredL2MaskHeld(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t l2Mask = GetL2Mask();

        if (l2Mask == 0)
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);

        return (b0 & l2Mask) == l2Mask;
    }

    static bool IsInternalR2Control(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);

        if ((b0 & 0x02000200) == 0x02000200)
            return true;

        BallTouchDebugSnapshot touch = {};
        if (GetRecentTouchForSamePlayer(player, &touch, 250))
        {
            const uint32_t dirBits = b0 & 0xF0;

            const bool sameDir =
                touch.dirBits != 0 &&
                dirBits != 0 &&
                touch.dirBits == dirBits;

            if (touch.r2 && sameDir)
                return true;
        }

        return false;
    }

    static uintptr_t GetRecentActorPlayer()
    {
        DWORD ballState = 0xFFFFFFFF;

        if (!ReadBallState(&ballState))
            return 0;

        // Solo queremos resolver jugador vivo durante conducción/control.
        // En pase, centro, tiro, pelota suelta, etc. no hay que arrastrar ownership.
        if (ballState != BALL_STATE_POSSESSION)
            return 0;

        // 1) Primero probamos el player capturado por BallActorTracker.
        // Ya no lo descartamos por edad: lo revalidamos contra memoria viva.
        const uintptr_t actorPlayer = GetBallActorPlayer();

        if (IsLiveBallOwnerCandidate(actorPlayer))
            return actorPlayer;

        // 2) Fallback al último snapshot real de toque/contacto.
        // Tampoco depende de 3500 ms: depende de que siga coincidiendo con el actor actual.
        BallTouchDebugSnapshot touch = {};

        if (GetLastTouchDebugSnapshot(&touch) &&
            touch.ball84 == BALL_STATE_POSSESSION &&
            IsLiveBallOwnerCandidate(touch.player))
        {
            return touch.player;
        }

        // 3) Último fallback: jugador activo global del motor.
        // Esto cubre carrera recta larga donde el actor hook no refresca.
        const uintptr_t activePlayer = GetActivePlayerFromGlobal();

        if (IsLiveBallOwnerCandidate(activePlayer))
            return activePlayer;

        return 0;
    }

    static uint32_t GetPlayerDirBits(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return 0;

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);

        return b0 & 0xF0;
    }

    static uint32_t GetPlayerBallDistanceRaw(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return 999999;

        return ReadOr<uint32_t>(player + 0x114, 999999);
    }

    static bool IsValidR2LongTouchDirectionChange(uint32_t fromDir, uint32_t toDir)
    {
        if (fromDir == 0 || toDir == 0)
            return false;

        if (fromDir == toDir)
            return false;

        // Cardinal -> diagonal vecina
        if (fromDir == 0x20) // derecha
            return toDir == 0x30 || toDir == 0x60;

        if (fromDir == 0x80) // izquierda
            return toDir == 0x90 || toDir == 0xC0;

        if (fromDir == 0x40) // abajo
            return toDir == 0x60 || toDir == 0xC0;

        if (fromDir == 0x10) // arriba
            return toDir == 0x30 || toDir == 0x90;

        // Diagonal -> cardinal vecina
        if (fromDir == 0x30) // arriba + derecha
            return toDir == 0x20 || toDir == 0x10;

        if (fromDir == 0x60) // abajo + derecha
            return toDir == 0x20 || toDir == 0x40;

        if (fromDir == 0x90) // arriba + izquierda
            return toDir == 0x80 || toDir == 0x10;

        if (fromDir == 0xC0) // abajo + izquierda
            return toDir == 0x80 || toDir == 0x40;

        return false;
    }
    static void ResetR2DirectionalCharge()
    {
        g_lastR1OnlyDir = 0;
        g_lastR1OnlyPlayer = 0;

        g_prevR1OnlyDir = 0;
        g_prevR1OnlyDirTick = 0;

        g_prevR2Held = false;
        g_r2ChargeConsumedForHold = false;

        g_r2ChargeUntilMs = 0;
        g_r2ChargePlayer = 0;
        g_r2ChargeFromDir = 0;
        g_r2ChargeToDir = 0;
    }

    static void LogR2ChargeDebug(
        const char* reason,
        uintptr_t player,
        uint32_t b0,
        uint32_t fromDir,
        uint32_t toDir,
        bool r1Held,
        bool r2Held,
        bool r2Edge,
        bool validDir,
        bool active,
        uint32_t p114)
    {
        if (!GetDebugChargeDbg())
            return;

        static ULONGLONG s_lastLogTick = 0;
        static uintptr_t s_lastPlayer = 0;
        static uint32_t s_lastB0 = 0xFFFFFFFF;
        static uint32_t s_lastFrom = 0xFFFFFFFF;
        static uint32_t s_lastTo = 0xFFFFFFFF;
        static bool s_lastActive = false;
        static const char* s_lastReason = "";

        const ULONGLONG now = GetTickCount64();

        const bool changed =
            s_lastPlayer != player ||
            s_lastB0 != b0 ||
            s_lastFrom != fromDir ||
            s_lastTo != toDir ||
            s_lastActive != active ||
            s_lastReason != reason;

        if (!changed && now - s_lastLogTick < 200)
            return;

        s_lastLogTick = now;
        s_lastPlayer = player;
        s_lastB0 = b0;
        s_lastFrom = fromDir;
        s_lastTo = toDir;
        s_lastActive = active;
        s_lastReason = reason;

        LogFormat(
            "[CHARGEDBG] reason=%s player=0x%08X b0=0x%08X "
            "from=0x%02X to=0x%02X r1=%u r2=%u edge=%u validDir=%u "
            "active=%u p114=%u untilLeft=%d",
            reason,
            (unsigned int)player,
            (unsigned int)b0,
            (unsigned int)fromDir,
            (unsigned int)toDir,
            r1Held ? 1u : 0u,
            r2Held ? 1u : 0u,
            r2Edge ? 1u : 0u,
            validDir ? 1u : 0u,
            active ? 1u : 0u,
            (unsigned int)p114,
            g_r2ChargeUntilMs > now ? (int)(g_r2ChargeUntilMs - now) : 0);
    }

    static bool UpdateR2DirectionalCharge(bool r1Held, bool r2Held, uintptr_t player)
    {
        const ULONGLONG now = GetTickCount64();

        if (!LooksLikeValidPlayer(player))
        {
            ResetR2DirectionalCharge();
            LogR2ChargeDebug(
                "RESET_NO_PLAYER",
                player,
                0,
                0,
                0,
                r1Held,
                r2Held,
                false,
                false,
                false,
                0);
            return false;
        }

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint32_t curDir = GetPlayerDirBits(player);
        const uint32_t p114 = GetPlayerBallDistanceRaw(player);
        const bool r2Edge = r2Held && !g_prevR2Held;

        if (!r1Held)
        {
            const uint32_t prevDir = g_lastR1OnlyDir;
            ResetR2DirectionalCharge();

            LogR2ChargeDebug(
                "RESET_NO_R1",
                player,
                b0,
                prevDir,
                curDir,
                r1Held,
                r2Held,
                r2Edge,
                false,
                false,
                p114);

            return false;
        }

        if (curDir == 0)
        {
            g_prevR2Held = r2Held;
            g_r2ChargeUntilMs = 0;

            LogR2ChargeDebug(
                "NO_DIR",
                player,
                b0,
                g_lastR1OnlyDir,
                curDir,
                r1Held,
                r2Held,
                r2Edge,
                false,
                false,
                p114);

            return false;
        }

        // Estado base: R1 sin R2. Guardamos direccion previa real.
        if (!r2Held)
        {
            if (curDir != 0)
            {
                if (g_lastR1OnlyPlayer == player &&
                    g_lastR1OnlyDir != 0 &&
                    g_lastR1OnlyDir != curDir)
                {
                    g_prevR1OnlyDir = g_lastR1OnlyDir;
                    g_prevR1OnlyDirTick = now;
                }

                g_lastR1OnlyDir = curDir;
                g_lastR1OnlyPlayer = player;
            }
            g_prevR2Held = false;

            g_r2ChargeUntilMs = 0;
            g_r2ChargePlayer = 0;
            g_r2ChargeFromDir = 0;
            g_r2ChargeToDir = 0;

            g_r2ChargeConsumedForHold = false;

            LogR2ChargeDebug(
                "TRACK_R1_ONLY",
                player,
                b0,
                g_lastR1OnlyDir,
                curDir,
                r1Held,
                r2Held,
                false,
                false,
                false,
                p114
            );
            return false;
        }

        const bool samePlayer =
            g_lastR1OnlyPlayer != 0 &&
            g_lastR1OnlyPlayer == player;

        uint32_t fromDir = samePlayer ? g_lastR1OnlyDir : 0;

        bool validDir =
            IsValidR2LongTouchDirectionChange(fromDir, curDir);

        // Si el polling ya pis� lastR1OnlyDir con la nueva direcci�n,
        // probamos la direcci�n inmediatamente anterior.
        if (!validDir &&
            samePlayer &&
            g_prevR1OnlyDir != 0 &&
            now - g_prevR1OnlyDirTick <= 500 &&
            IsValidR2LongTouchDirectionChange(g_prevR1OnlyDir, curDir))
        {
            fromDir = g_prevR1OnlyDir;
            validDir = true;
        }

        bool active =
            g_r2ChargePlayer == player &&
            g_r2ChargeUntilMs != 0 &&
            now <= g_r2ChargeUntilMs;

        if (active)
        {
            if (p114 > GetR2ChargeKeepDistMax())
            {
                g_r2ChargeUntilMs = 0;
                g_r2ChargePlayer = 0;
                active = false;

                g_prevR2Held = r2Held;

                LogR2ChargeDebug(
                    "STOP_TOO_FAR",
                    player,
                    b0,
                    fromDir,
                    curDir,
                    r1Held,
                    r2Held,
                    r2Edge,
                    validDir,
                    false,
                    p114);

                return false;
            }

            g_prevR2Held = r2Held;

            LogR2ChargeDebug(
                "ACTIVE",
                player,
                b0,
                g_r2ChargeFromDir,
                g_r2ChargeToDir,
                r1Held,
                r2Held,
                r2Edge,
                validDir,
                true,
                p114);

            return true;
        }

        if (g_r2ChargeConsumedForHold)
        {
            g_prevR2Held = r2Held;

            LogR2ChargeDebug(
                "CONSUMED_WAIT_RELEASE",
                player,
                b0,
                fromDir,
                curDir,
                r1Held,
                r2Held,
                r2Edge,
                validDir,
                false,
                p114
            );

            return false;
        }

        // No exigimos flanco estricto: el polling puede perder el frame exacto.
        // La proteccion real es validDir + direccion R1-only previa + distancia.
        if (validDir)
        {
            if (p114 <= GetR2ChargeStartDistMax())
            {
                g_r2ChargePlayer = player;
                g_r2ChargeFromDir = fromDir;
                g_r2ChargeToDir = curDir;
                g_r2ChargeUntilMs = now + GetR2ChargeWindowMs();

                g_prevR2Held = r2Held;
                g_r2ChargeConsumedForHold = true;

                LogR2ChargeDebug(
                    "START",
                    player,
                    b0,
                    fromDir,
                    curDir,
                    r1Held,
                    r2Held,
                    r2Edge,
                    true,
                    true,
                    p114);

                return true;
            }

            g_prevR2Held = r2Held;

            LogR2ChargeDebug(
                "VALID_BUT_TOO_FAR",
                player,
                b0,
                fromDir,
                curDir,
                r1Held,
                r2Held,
                r2Edge,
                true,
                false,
                p114);

            return false;
        }

        g_prevR2Held = r2Held;

        LogR2ChargeDebug(
            r2Edge ? "R2_EDGE_INVALID_DIR" : "R2_HELD_INVALID_DIR",
            player,
            b0,
            fromDir,
            curDir,
            r1Held,
            r2Held,
            r2Edge,
            false,
            false,
            p114);

        return false;
    }

    static void ClearProBoostR1OnlyArm()
    {
        g_proR1OnlyArmed = false;
        g_proR1OnlyPlayer = 0;
        g_proR1OnlyTick = 0;
    }

    static void ResetProBoostChargeOnly()
    {
        g_proLastTechDir = 0;
        g_proLastTechPlayer = 0;

        g_proPrevTechDir = 0;
        g_proPrevTechDirTick = 0;

        g_proPrevR2Held = false;
        g_proChargeConsumedForHold = false;

        g_proChargeUntilMs = 0;
        g_proChargePlayer = 0;
        g_proChargeFromDir = 0;
        g_proChargeToDir = 0;
    }

    static void ResetProBoostState()
    {
        ResetProBoostChargeOnly();

        g_proR1OnlyArmed = false;
        g_proR1OnlyPlayer = 0;
        g_proR1OnlyTick = 0;

        g_proL2FirstBlocked = false;
        g_proL2FirstBlockedPlayer = 0;

        g_proTechnicalAuthorized = false;
        g_proTechnicalAuthorizedPlayer = 0;

        g_proL2HoldAuthorized = false;
        g_proL2HoldBlocked = false;
        g_proL2HoldPlayer = 0;
    }

    static bool UpdateProBoostTechnicalSprintHeld(bool r1Held, bool l2Held, uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return false;

        const uint32_t curDir = GetPlayerDirBits(player);

        // Soltar L2 termina el hold técnico completo.
        // Esto libera tanto el bloqueo L2->R1 como la autorización R1->L2.
        if (!l2Held)
        {
            g_proL2HoldAuthorized = false;
            g_proL2HoldBlocked = false;
            g_proL2HoldPlayer = 0;

            g_proL2FirstBlocked = false;
            g_proL2FirstBlockedPlayer = 0;

            g_proTechnicalAuthorized = false;
            g_proTechnicalAuthorizedPlayer = 0;

            // Al soltar L2, el próximo ProBoost debe volver a nacer
            // desde R1 solo, no desde un armado viejo.
            ClearProBoostR1OnlyArm();
        }

        // L2 primero, sin R1: bloquear este hold de L2.
        // Aunque después se presione R1, no debe activar 305 hasta soltar L2.
        if (l2Held && !r1Held)
        {
            g_proL2HoldBlocked = true;
            g_proL2HoldAuthorized = false;
            g_proL2HoldPlayer = player;

            g_proL2FirstBlocked = true;
            g_proL2FirstBlockedPlayer = player;

            g_proTechnicalAuthorized = false;
            g_proTechnicalAuthorizedPlayer = 0;

            // Clave:
            // si aparece L2 sin R1, cualquier R1-only anterior ya no sirve.
            // Esto evita que un armado viejo autorice L2 -> R1.
            ClearProBoostR1OnlyArm();

            return false;
        }

        // Sin R1 no hay ProBoost.
        if (!r1Held)
        {
            ResetProBoostState();
            return false;
        }

        // R1 solo: arma la secuencia válida.
        // Todavía no activa 305 en ProBoost.
        if (!l2Held)
        {
            g_proR1OnlyArmed = true;
            g_proR1OnlyPlayer = player;
            g_proR1OnlyTick = GetTickCount64();

            ResetProBoostChargeOnly();

            return false;
        }

        // Desde acá hay R1 + L2.

        // Si este hold de L2 empezó mal, nunca activar hasta soltar L2.
        if (g_proL2HoldBlocked)
            return false;

        // Si ya estaba autorizado este hold de L2, mantener 305.
        // No exigimos mismo player exacto porque tras tirarla larga puede haber
        // microcambios de contexto aunque B0 actual esté correcto.
        if (g_proL2HoldAuthorized)
        {
            if (curDir == 0)
                return false;

            g_proL2HoldPlayer = player;
            return true;
        }

        // Primera autorización válida:
        // solo se concede si antes vimos R1 solo.
        if (g_proR1OnlyArmed && g_proR1OnlyPlayer == player)
        {
            if (curDir == 0)
                return false;

            g_proL2HoldAuthorized = true;
            g_proL2HoldBlocked = false;
            g_proL2HoldPlayer = player;

            g_proTechnicalAuthorized = true;
            g_proTechnicalAuthorizedPlayer = player;

            return true;
        }

        return false;
    }

    static void LogProBoostChargeDebug(
        const char* reason,
        uintptr_t player,
        uint32_t b0,
        uint32_t fromDir,
        uint32_t toDir,
        bool technicalHeld,
        bool r2Held,
        bool r2Edge,
        bool validDir,
        bool active,
        uint32_t p114)
    {
        if (!GetDebugChargeDbg())
            return;

        static ULONGLONG s_lastLogTick = 0;
        static uintptr_t s_lastPlayer = 0;
        static uint32_t s_lastB0 = 0xFFFFFFFF;
        static uint32_t s_lastFrom = 0xFFFFFFFF;
        static uint32_t s_lastTo = 0xFFFFFFFF;
        static bool s_lastActive = false;
        static const char* s_lastReason = "";

        const ULONGLONG now = GetTickCount64();

        const bool changed =
            s_lastPlayer != player ||
            s_lastB0 != b0 ||
            s_lastFrom != fromDir ||
            s_lastTo != toDir ||
            s_lastActive != active ||
            s_lastReason != reason;

        if (!changed && now - s_lastLogTick < 200ULL)
            return;

        s_lastLogTick = now;
        s_lastPlayer = player;
        s_lastB0 = b0;
        s_lastFrom = fromDir;
        s_lastTo = toDir;
        s_lastActive = active;
        s_lastReason = reason;

        LogFormat(
            "[PROCHARGEDBG] reason=%s player=0x%08X b0=0x%08X "
            "from=0x%02X to=0x%02X tech=%u r2=%u edge=%u validDir=%u "
            "active=%u p114=%u untilLeft=%d",
            reason,
            (unsigned int)player,
            (unsigned int)b0,
            (unsigned int)fromDir,
            (unsigned int)toDir,
            technicalHeld ? 1u : 0u,
            r2Held ? 1u : 0u,
            r2Edge ? 1u : 0u,
            validDir ? 1u : 0u,
            active ? 1u : 0u,
            (unsigned int)p114,
            g_proChargeUntilMs > now ? (int)(g_proChargeUntilMs - now) : 0);
    }

    static bool UpdateProBoostDirectionalCharge(bool technicalHeld, bool r2Held, uintptr_t player)
    {
        const ULONGLONG now = GetTickCount64();

        if (!LooksLikeValidPlayer(player))
        {
            ResetProBoostChargeOnly();
            LogProBoostChargeDebug("RESET_NO_PLAYER", player, 0, 0, 0,
                technicalHeld, r2Held, false, false, false, 0);
            return false;
        }

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        const uint32_t curDir = GetPlayerDirBits(player);
        const uint32_t p114 = GetPlayerBallDistanceRaw(player);
        const bool r2Edge = r2Held && !g_proPrevR2Held;

        if (!technicalHeld)
        {
            const uint32_t prevDir = g_proLastTechDir;

            g_proLastTechDir = 0;
            g_proLastTechPlayer = 0;
            g_proPrevTechDir = 0;
            g_proPrevTechDirTick = 0;
            g_proPrevR2Held = r2Held;
            g_proChargeConsumedForHold = false;
            g_proChargeUntilMs = 0;
            g_proChargePlayer = 0;
            g_proChargeFromDir = 0;
            g_proChargeToDir = 0;

            LogProBoostChargeDebug("RESET_NO_TECH", player, b0, prevDir, curDir,
                technicalHeld, r2Held, r2Edge, false, false, p114);
            return false;
        }

        if (curDir == 0)
        {
            g_proPrevR2Held = r2Held;
            g_proChargeUntilMs = 0;

            LogProBoostChargeDebug("NO_DIR", player, b0, g_proLastTechDir, curDir,
                technicalHeld, r2Held, r2Edge, false, false, p114);
            return false;
        }

        // Estado base: R1+L2 sin R2. Guardamos direccion tecnica previa.
        if (!r2Held)
        {
            if (curDir != 0)
            {
                if (g_proLastTechPlayer == player &&
                    g_proLastTechDir != 0 &&
                    g_proLastTechDir != curDir)
                {
                    g_proPrevTechDir = g_proLastTechDir;
                    g_proPrevTechDirTick = now;
                }

                g_proLastTechDir = curDir;
                g_proLastTechPlayer = player;
            }

            g_proPrevR2Held = false;
            g_proChargeUntilMs = 0;
            g_proChargePlayer = 0;
            g_proChargeFromDir = 0;
            g_proChargeToDir = 0;
            g_proChargeConsumedForHold = false;

            LogProBoostChargeDebug("TRACK_TECH_ONLY", player, b0, g_proLastTechDir, curDir,
                technicalHeld, r2Held, false, false, false, p114);
            return false;
        }

        const bool samePlayer =
            g_proLastTechPlayer != 0 &&
            g_proLastTechPlayer == player;

        uint32_t fromDir = samePlayer ? g_proLastTechDir : 0;
        bool validDir = IsValidR2LongTouchDirectionChange(fromDir, curDir);

        if (!validDir &&
            samePlayer &&
            g_proPrevTechDir != 0 &&
            now - g_proPrevTechDirTick <= 500ULL &&
            IsValidR2LongTouchDirectionChange(g_proPrevTechDir, curDir))
        {
            fromDir = g_proPrevTechDir;
            validDir = true;
        }

        bool active =
            g_proChargePlayer == player &&
            g_proChargeUntilMs != 0 &&
            now <= g_proChargeUntilMs;

        if (active)
        {
            if (p114 > GetR2ChargeKeepDistMax())
            {
                g_proChargeUntilMs = 0;
                g_proChargePlayer = 0;
                active = false;
                g_proPrevR2Held = r2Held;

                LogProBoostChargeDebug("STOP_TOO_FAR", player, b0, fromDir, curDir,
                    technicalHeld, r2Held, r2Edge, validDir, false, p114);
                return false;
            }

            g_proPrevR2Held = r2Held;

            LogProBoostChargeDebug("ACTIVE", player, b0, g_proChargeFromDir, g_proChargeToDir,
                technicalHeld, r2Held, r2Edge, validDir, true, p114);
            return true;
        }

        if (g_proChargeConsumedForHold)
        {
            g_proPrevR2Held = r2Held;

            LogProBoostChargeDebug("CONSUMED_WAIT_RELEASE", player, b0, fromDir, curDir,
                technicalHeld, r2Held, r2Edge, validDir, false, p114);
            return false;
        }

        if (validDir)
        {
            if (p114 <= GetR2ChargeStartDistMax())
            {
                g_proChargePlayer = player;
                g_proChargeFromDir = fromDir;
                g_proChargeToDir = curDir;
                g_proChargeUntilMs = now + GetR2ChargeWindowMs();

                g_proPrevR2Held = r2Held;
                g_proChargeConsumedForHold = true;

                LogProBoostChargeDebug("START", player, b0, fromDir, curDir,
                    technicalHeld, r2Held, r2Edge, true, true, p114);
                return true;
            }

            g_proPrevR2Held = r2Held;

            LogProBoostChargeDebug("VALID_BUT_TOO_FAR", player, b0, fromDir, curDir,
                technicalHeld, r2Held, r2Edge, true, false, p114);
            return false;
        }

        g_proPrevR2Held = r2Held;

        LogProBoostChargeDebug(
            r2Edge ? "R2_EDGE_INVALID_DIR" : "R2_HELD_INVALID_DIR",
            player,
            b0,
            fromDir,
            curDir,
            technicalHeld,
            r2Held,
            r2Edge,
            false,
            false,
            p114);

        return false;
    }

    enum BallWeightDecisionReason
    {
        BW_REASON_STATE_PASS_OR_LOOSE = 1,
        BW_REASON_NOT_POSSESSION = 2,
        BW_REASON_PROTECTED = 3,
        BW_REASON_R1_R2_CHARGE = 4,
        BW_REASON_R1 = 5,
        BW_REASON_R2 = 6,
        BW_REASON_NORMAL = 7,
        BW_REASON_NO_PLAYER = 8,
        BW_REASON_DISABLED = 9,
        BW_REASON_PRO_R1_VANILLA = 10,
        BW_REASON_PRO_R1_L2 = 11,
        BW_REASON_PRO_R1_L2_R2_CHARGE = 12
    };

    static const char* GetBwReasonName(BallWeightDecisionReason reason)
    {
        switch (reason)
        {
        case BW_REASON_STATE_PASS_OR_LOOSE: return "STATE_PASS_OR_LOOSE";
        case BW_REASON_NOT_POSSESSION:      return "NOT_POSSESSION";
        case BW_REASON_PROTECTED:           return "PROTECTED";
        case BW_REASON_R1_R2_CHARGE:        return "R1_R2_CHARGE";
        case BW_REASON_R1:                  return "R1";
        case BW_REASON_R2:                  return "R2";
        case BW_REASON_NORMAL:              return "NORMAL";
        case BW_REASON_NO_PLAYER:           return "NO_PLAYER";
        case BW_REASON_DISABLED:            return "DISABLED";
        case BW_REASON_PRO_R1_VANILLA:      return "PRO_R1_VANILLA";
        case BW_REASON_PRO_R1_L2:           return "PRO_R1_L2";
        case BW_REASON_PRO_R1_L2_R2_CHARGE: return "PRO_R1_L2_R2_CHARGE";
        default:                            return "UNKNOWN";
        }
    }

    static void LogL2InputSnapshotIfNeeded(
        DWORD ballState,
        uintptr_t player,
        bool protectedAction,
        bool internalR1,
        bool internalR2,
        bool r1Held,
        bool r2Held,
        bool r2ChargeActive,
        float target,
        BallWeightDecisionReason reason)
    {
        if (!GetDebugL2Input())
            return;

        const ULONGLONG now = GetTickCount64();
        const uint32_t intervalMs = GetDebugL2InputIntervalMs();

        uint32_t ball50 = 0;
        uint32_t ball88 = 0;

        if (g_pesBase)
        {
            const uintptr_t ballBase =
                ReadOr<uintptr_t>(g_pesBase + PesAddresses::BALL_GLOBAL_PTR, 0);

            if (ballBase)
            {
                ball50 = ReadOr<uint32_t>(ballBase + PesOffsets::BALL_POWER, 0);
                ball88 = ReadOr<uint32_t>(ballBase + 0x88, 0);
            }
        }

        const uint32_t configuredL2Mask = GetL2Mask();

        const bool hasValidPlayer = LooksLikeValidPlayer(player);

        uint8_t playerId = 0xFF;
        uint32_t b0 = 0;
        uint32_t dirBits = 0;
        uint32_t modeBits = 0;
        uint32_t extraBits = 0;
        bool configuredL2Held = false;

        uint8_t p16 = 0;
        uint16_t p18 = 0;
        uint8_t p4D = 0;
        uint8_t p4F = 0;
        uint32_t p114 = 0;
        uint16_t anim30 = 0;

        if (hasValidPlayer)
        {
            playerId = ReadOr<uint8_t>(player + 0x00, 0xFF);

            b0 = ReadOr<uint32_t>(player + 0xB0, 0);

            dirBits = b0 & INPUT_DIR_MASK;
            modeBits = b0 & ~INPUT_DIR_MASK;

            extraBits =
                modeBits
                & ~R1_MASK
                & ~R2_MASK
                & ~CROSS_MASK
                & ~SHOT_MASK;

            configuredL2Held = IsConfiguredL2MaskHeld(player);

            p16 = ReadOr<uint8_t>(player + 0x16, 0);
            p18 = ReadOr<uint16_t>(player + 0x18, 0);
            p4D = ReadOr<uint8_t>(player + 0x4D, 0);
            p4F = ReadOr<uint8_t>(player + 0x4F, 0);
            p114 = ReadOr<uint32_t>(player + 0x114, 0);
            anim30 = GetAnim30(player);
        }

        BallTouchDebugSnapshot touch = {};
        const bool hasTouch = GetLastTouchDebugSnapshot(&touch);

        const ULONGLONG touchAge =
            hasTouch && touch.tick != 0
            ? now - touch.tick
            : 999999;

        const bool recentTouch =
            hasTouch &&
            touch.tick != 0 &&
            touchAge <= 500;

        uint32_t touchModeBits = 0;
        uint32_t touchExtraBits = 0;
        bool touchConfiguredL2Held = false;

        if (recentTouch)
        {
            touchModeBits = touch.b0 & ~INPUT_DIR_MASK;

            touchExtraBits =
                touchModeBits
                & ~R1_MASK
                & ~R2_MASK
                & ~CROSS_MASK
                & ~SHOT_MASK;

            touchConfiguredL2Held =
                configuredL2Mask != 0 &&
                (touch.b0 & configuredL2Mask) == configuredL2Mask;
        }

        // Evita spam: [L2DBG] solo escribe cuando hay input relevante para esta investigacion.
        // No loguea loops vacios tipo player=0, NORMAL/NO_PLAYER, sin B0 util y sin touch reciente.
        const bool playerInputUseful =
            hasValidPlayer &&
            b0 != 0 &&
            (r1Held ||
             r2Held ||
             internalR1 ||
             internalR2 ||
             configuredL2Held ||
             extraBits != 0 ||
             protectedAction ||
             r2ChargeActive);

        const bool touchInputUseful =
            recentTouch &&
            touch.b0 != 0 &&
            (touch.r1 ||
             touch.r2 ||
             touchConfiguredL2Held ||
             touchExtraBits != 0);

        const bool useful =
            playerInputUseful ||
            touchInputUseful;

        if (!useful)
            return;

        const int targetInt = static_cast<int>(target + 0.5f);

        static ULONGLONG s_lastLogTick = 0;
        static uintptr_t s_lastPlayer = 0;
        static uint32_t s_lastB0 = 0xFFFFFFFF;
        static uint32_t s_lastExtraBits = 0xFFFFFFFF;
        static uint32_t s_lastTouchB0 = 0xFFFFFFFF;
        static uint32_t s_lastTouchExtraBits = 0xFFFFFFFF;
        static uint32_t s_lastBallState = 0xFFFFFFFF;
        static int s_lastReason = -1;
        static int s_lastTargetInt = -1;
        static bool s_lastConfiguredL2Held = false;
        static bool s_lastTouchConfiguredL2Held = false;
        static bool s_lastR1Held = false;
        static bool s_lastR2Held = false;
        static bool s_lastCharge = false;
        static bool s_lastProtected = false;

        const bool changed =
            s_lastPlayer != player ||
            s_lastB0 != b0 ||
            s_lastExtraBits != extraBits ||
            s_lastTouchB0 != touch.b0 ||
            s_lastTouchExtraBits != touchExtraBits ||
            s_lastBallState != ballState ||
            s_lastReason != static_cast<int>(reason) ||
            s_lastTargetInt != targetInt ||
            s_lastConfiguredL2Held != configuredL2Held ||
            s_lastTouchConfiguredL2Held != touchConfiguredL2Held ||
            s_lastR1Held != r1Held ||
            s_lastR2Held != r2Held ||
            s_lastCharge != r2ChargeActive ||
            s_lastProtected != protectedAction;

        // Heartbeat solo para input util sostenido. Evita llenar disco si el estado no cambia.
        const bool heartbeat =
            intervalMs != 0 &&
            now - s_lastLogTick >= intervalMs;

        if (!changed && !heartbeat)
            return;

        s_lastLogTick = now;
        s_lastPlayer = player;
        s_lastB0 = b0;
        s_lastExtraBits = extraBits;
        s_lastTouchB0 = touch.b0;
        s_lastTouchExtraBits = touchExtraBits;
        s_lastBallState = ballState;
        s_lastReason = static_cast<int>(reason);
        s_lastTargetInt = targetInt;
        s_lastConfiguredL2Held = configuredL2Held;
        s_lastTouchConfiguredL2Held = touchConfiguredL2Held;
        s_lastR1Held = r1Held;
        s_lastR2Held = r2Held;
        s_lastCharge = r2ChargeActive;
        s_lastProtected = protectedAction;

        LogFormat(
            "[L2DBG] state=%u ball50=%u ball88=%u player=0x%08X id=%u "
            "b0=0x%08X dir=0x%02X mode=0x%08X extra=0x%08X "
            "r1=%u r2=%u cfgL2=%u l2Mask=0x%08X "
            "p16=%u p18=%u p4D=%u p4F=%u p114=%u anim30=0x%04X "
            "protected=%u charge=%u target=%.1f reason=%s "
            "touch=%u touchAge=%llu touchPlayer=0x%08X touchB0=0x%08X "
            "touchDir=0x%02X touchMode=0x%08X touchExtra=0x%08X touchCfgL2=%u "
            "touchR1=%u touchR2=%u touchP114=%u touchAnim30=0x%04X",
            (unsigned int)ballState,
            (unsigned int)ball50,
            (unsigned int)ball88,
            (unsigned int)player,
            (unsigned int)playerId,
            (unsigned int)b0,
            (unsigned int)dirBits,
            (unsigned int)modeBits,
            (unsigned int)extraBits,
            r1Held ? 1u : 0u,
            r2Held ? 1u : 0u,
            configuredL2Held ? 1u : 0u,
            (unsigned int)configuredL2Mask,
            (unsigned int)p16,
            (unsigned int)p18,
            (unsigned int)p4D,
            (unsigned int)p4F,
            (unsigned int)p114,
            (unsigned int)anim30,
            protectedAction ? 1u : 0u,
            r2ChargeActive ? 1u : 0u,
            target,
            GetBwReasonName(reason),
            hasTouch ? 1u : 0u,
            (unsigned long long)touchAge,
            hasTouch ? (unsigned int)touch.player : 0u,
            hasTouch ? (unsigned int)touch.b0 : 0u,
            hasTouch ? (unsigned int)touch.dirBits : 0u,
            (unsigned int)touchModeBits,
            (unsigned int)touchExtraBits,
            touchConfiguredL2Held ? 1u : 0u,
            hasTouch && touch.r1 ? 1u : 0u,
            hasTouch && touch.r2 ? 1u : 0u,
            hasTouch ? (unsigned int)touch.p114 : 0u,
            hasTouch ? (unsigned int)touch.anim30 : 0u);
    }

    static void LogBallWeightDecisionIfNeeded(
        DWORD ballState,
        uintptr_t player,
        bool protectedAction,
        bool internalR1,
        bool internalR2,
        bool r1Held,
        bool r2Held,
        bool r2ChargeActive,
        float target,
        BallWeightDecisionReason reason)
    {
        if (!GetDebugBwDec())
            return;

        const ULONGLONG now = GetTickCount64();

        uint32_t b0 = 0;
        uint32_t dirBits = 0;
        uint8_t p16 = 0;
        uint16_t p18 = 0;
        uint8_t p4D = 0;
        uint8_t p4F = 0;
        uint32_t p114 = 0;
        uint16_t anim30 = 0;

        if (LooksLikeValidPlayer(player))
        {
            b0 = ReadOr<uint32_t>(player + 0xB0, 0);
            dirBits = b0 & 0xF0;

            p16 = ReadOr<uint8_t>(player + 0x16, 0);
            p18 = ReadOr<uint16_t>(player + 0x18, 0);
            p4D = ReadOr<uint8_t>(player + 0x4D, 0);
            p4F = ReadOr<uint8_t>(player + 0x4F, 0);
            p114 = ReadOr<uint32_t>(player + 0x114, 0);
            anim30 = GetAnim30(player);
        }

        BallTouchDebugSnapshot touch = {};
        const bool hasTouch = GetLastTouchDebugSnapshot(&touch);
        const ULONGLONG touchAge =
            hasTouch && touch.tick != 0
            ? now - touch.tick
            : 999999;

        const bool samePlayer =
            hasTouch &&
            touch.player != 0 &&
            player != 0 &&
            touch.player == player;

        static uintptr_t s_lastPlayer = 0;
        static uint32_t s_lastB0 = 0xFFFFFFFF;
        static uint32_t s_lastTouchB0 = 0xFFFFFFFF;
        static int s_lastTargetInt = -1;
        static int s_lastReason = -1;
        static bool s_lastProtected = false;
        static bool s_lastInternalR1 = false;
        static bool s_lastInternalR2 = false;
        static bool s_lastCharge = false;
        static ULONGLONG s_lastLogTick = 0;

        const int targetInt = static_cast<int>(target + 0.5f);

        const bool changed =
            s_lastPlayer != player ||
            s_lastB0 != b0 ||
            s_lastTouchB0 != touch.b0 ||
            s_lastTargetInt != targetInt ||
            s_lastReason != static_cast<int>(reason) ||
            s_lastProtected != protectedAction ||
            s_lastInternalR1 != internalR1 ||
            s_lastInternalR2 != internalR2 ||
            s_lastCharge != r2ChargeActive;

        const bool interesting =
            protectedAction ||
            internalR1 ||
            internalR2 ||
            r1Held ||
            r2Held ||
            r2ChargeActive ||
            (hasTouch && touchAge <= 500 && (touch.r1 || touch.r2));

        const bool heartbeat =
            interesting && (now - s_lastLogTick >= 250);

        if (!changed && !heartbeat)
            return;

        s_lastPlayer = player;
        s_lastB0 = b0;
        s_lastTouchB0 = touch.b0;
        s_lastTargetInt = targetInt;
        s_lastReason = static_cast<int>(reason);
        s_lastProtected = protectedAction;
        s_lastInternalR1 = internalR1;
        s_lastInternalR2 = internalR2;
        s_lastCharge = r2ChargeActive;
        s_lastLogTick = now;

        LogFormat(
            "[BWDEC] state=%u player=0x%08X b0=0x%08X dir=0x%02X "
            "p16=%u p18=%u p4D=%u p4F=%u p114=%u anim30=0x%04X "
            "internalR1=%u internalR2=%u r1Held=%u r2Held=%u charge=%u protected=%u "
            "target=%.1f reason=%s "
            "touch=%u touchAge=%llu samePlayer=%u touchSrc=%u touchPlayer=0x%08X "
            "touchB0=0x%08X touchDir=0x%02X touchR1=%u touchR2=%u touchP16=%u touchP18=%u "
            "touchP4D=%u touchP4F=%u touchP114=%u touchAnim30=0x%04X",
            (unsigned int)ballState,
            (unsigned int)player,
            (unsigned int)b0,
            (unsigned int)dirBits,
            (unsigned int)p16,
            (unsigned int)p18,
            (unsigned int)p4D,
            (unsigned int)p4F,
            (unsigned int)p114,
            (unsigned int)anim30,
            internalR1 ? 1u : 0u,
            internalR2 ? 1u : 0u,
            r1Held ? 1u : 0u,
            r2Held ? 1u : 0u,
            r2ChargeActive ? 1u : 0u,
            protectedAction ? 1u : 0u,
            target,
            GetBwReasonName(reason),
            hasTouch ? 1u : 0u,
            (unsigned long long)touchAge,
            samePlayer ? 1u : 0u,
            hasTouch ? (unsigned int)touch.source : 0u,
            hasTouch ? (unsigned int)touch.player : 0u,
            hasTouch ? (unsigned int)touch.b0 : 0u,
            hasTouch ? (unsigned int)touch.dirBits : 0u,
            hasTouch && touch.r1 ? 1u : 0u,
            hasTouch && touch.r2 ? 1u : 0u,
            hasTouch ? (unsigned int)touch.p16 : 0u,
            hasTouch ? (unsigned int)touch.p18 : 0u,
            hasTouch ? (unsigned int)touch.p4D : 0u,
            hasTouch ? (unsigned int)touch.p4F : 0u,
            hasTouch ? (unsigned int)touch.p114 : 0u,
            hasTouch ? (unsigned int)touch.anim30 : 0u);
    }

    static float ResolveTargetBallWeight(DWORD ballState)
    {
        const float overall = GetOverallBallWeight();

        uintptr_t player = 0;
        bool protectedAction = false;
        bool internalR1 = false;
        bool internalR2 = false;
        bool r1Held = false;
        bool r2Held = false;
        bool r2ChargeActive = false;

        float target = overall;
        BallWeightDecisionReason reason = BW_REASON_NORMAL;

        if (ballState == BALL_STATE_PASS_OR_LOOSE)
        {
            ResetR2DirectionalCharge();
            ResetProBoostChargeOnly();

            target = GetBallWeightState1();
            reason = BW_REASON_STATE_PASS_OR_LOOSE;

            LogBallWeightDecisionIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            LogL2InputSnapshotIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            return target;
        }

        if (ballState != BALL_STATE_POSSESSION)
        {
            ResetR2DirectionalCharge();
            ResetProBoostChargeOnly();

            target = overall;
            reason = BW_REASON_NOT_POSSESSION;

            LogBallWeightDecisionIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            LogL2InputSnapshotIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            return target;
        }

        player = GetRecentActorPlayer();

        protectedAction =
            ShouldForceOverallForProtectedAction(player, ballState);

        if (protectedAction)
        {
            ResetR2DirectionalCharge();
            ResetProBoostState();

            target = overall;
            reason = BW_REASON_PROTECTED;

            LogBallWeightDecisionIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            LogL2InputSnapshotIfNeeded(
                ballState,
                player,
                protectedAction,
                internalR1,
                internalR2,
                r1Held,
                r2Held,
                r2ChargeActive,
                target,
                reason);

            return target;
        }

        internalR1 = IsInternalR1Sprint(player);
        internalR2 = IsInternalR2Control(player);

        r1Held = internalR1;
        r2Held = internalR2;

        const ConductionMode mode = GetConductionMode();

        if (mode == ConductionMode::Disabled)
        {
            ResetR2DirectionalCharge();
            ResetProBoostState();

            target = overall;
            reason = BW_REASON_DISABLED;
        }
        else if (mode == ConductionMode::Boost)
        {
            // Ruta original estable. No tocar: mantiene BoostMode v1.
            ResetProBoostState();

            r2ChargeActive =
                UpdateR2DirectionalCharge(r1Held, r2Held, player);

            if (!LooksLikeValidPlayer(player))
            {
                target = GetBallWeightNormalDribble();
                reason = BW_REASON_NO_PLAYER;
            }
            else if (r1Held && r2Held && r2ChargeActive)
            {
                target = GetBallWeightR1R2();
                reason = BW_REASON_R1_R2_CHARGE;
            }
            else if (r1Held)
            {
                target = GetBallWeightR1();
                reason = BW_REASON_R1;
            }
            else if (r2Held)
            {
                target = GetBallWeightR2();
                reason = BW_REASON_R2;
            }
            else
            {
                target = GetBallWeightNormalDribble();
                reason = BW_REASON_NORMAL;
            }
        }
        else // ConductionMode::ProBoost
        {
            // ProBoost usa estado propio. No comparte el charge ni prioridades de BoostMode.
            ResetR2DirectionalCharge();

            const bool l2Held = IsConfiguredL2MaskHeld(player);
            const bool technicalHeld =
                UpdateProBoostTechnicalSprintHeld(r1Held, l2Held, player);

            r2ChargeActive =
                UpdateProBoostDirectionalCharge(technicalHeld, r2Held, player);

            if (!LooksLikeValidPlayer(player))
            {
                target = GetBallWeightNormalDribble();
                reason = BW_REASON_NO_PLAYER;
            }
            else if (technicalHeld && r2Held && r2ChargeActive)
            {
                target = GetBallWeightR1R2();
                reason = BW_REASON_PRO_R1_L2_R2_CHARGE;
            }
            else if (technicalHeld)
            {
                target = GetProBallWeightR1L2();
                reason = BW_REASON_PRO_R1_L2;
            }
            else if (r1Held)
            {
                target = GetProBallWeightR1();
                reason = BW_REASON_PRO_R1_VANILLA;
            }
            else if (r2Held)
            {
                target = GetBallWeightR2();
                reason = BW_REASON_R2;
            }
            else
            {
                target = GetBallWeightNormalDribble();
                reason = BW_REASON_NORMAL;
            }
        }

        LogBallWeightDecisionIfNeeded(
            ballState,
            player,
            protectedAction,
            internalR1,
            internalR2,
            r1Held,
            r2Held,
            r2ChargeActive,
            target,
            reason);

        LogL2InputSnapshotIfNeeded(
            ballState,
            player,
            protectedAction,
            internalR1,
            internalR2,
            r1Held,
            r2Held,
            r2ChargeActive,
            target,
            reason);

        return target;
    }

    static DWORD WINAPI BallWeightThreadProc(LPVOID)
    {
        WriteLog("[BW] BallWeightController thread iniciado.");

        while (InterlockedCompareExchange(&g_running, 0, 0) != 0)
        {
            DWORD ballState = 0;

            if (ReadBallState(&ballState))
            {
                const float target = ResolveTargetBallWeight(ballState);
                ApplyBallWeightIfChanged(target, &g_lastAppliedBits);
            }

            Sleep(10);
        }

        WriteLog("[BW] BallWeightController thread finalizado.");
        return 0;
    }
}

bool StartBallWeightController(uintptr_t pesBase)
{
    if (!pesBase)
        return false;

    if (InterlockedCompareExchange(&g_running, 1, 0) != 0)
        return true;

    g_pesBase = pesBase;
    g_lastAppliedBits = 0;
    ResetR2DirectionalCharge();
    ResetProBoostState();

    g_thread = CreateThread(
        nullptr,
        0,
        BallWeightThreadProc,
        nullptr,
        0,
        nullptr);

    if (!g_thread)
    {
        InterlockedExchange(&g_running, 0);
        WriteLog("[BW][ERROR] No se pudo crear thread.");
        return false;
    }

    CloseHandle(g_thread);
    g_thread = nullptr;

    return true;
}

void StopBallWeightController()
{
    InterlockedExchange(&g_running, 0);
}
