#include "pch.h"
#include "BallWeightController.h"

#include "GameplayConfig.h"
#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"
#include "BallActionGuards.h"
#include "BallActorTracker.h"
#include "BallActorTracker.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

namespace
{
    constexpr DWORD BALL_STATE_POSSESSION = 0;
    constexpr DWORD BALL_STATE_PASS_OR_LOOSE = 1;
    constexpr float VANILLA_BALL_WEIGHT = 188.0f;

    constexpr uintptr_t ACTIVE_PLAYER_PTR_OFFSET = 0x0037E0AA0;

    static volatile LONG g_controllerRunning = 0;
    static HANDLE g_controllerThread = nullptr;
    static uintptr_t g_pesBase = 0;

    constexpr uintptr_t BWDBG_BALL_GLOBAL_PTR_OFFSET = 0x007CCE94;
    constexpr uintptr_t BWDBG_BALL_WEIGHT_STATIC_OFFSET = 0x0078AE70;

    constexpr uint32_t BWDBG_R1_MASK = 0x01000800;
    constexpr uint32_t BWDBG_R2_MASK = 0x02000200;
    constexpr uint32_t BWDBG_CROSS_MASK = 0x00122000;
    constexpr uint32_t BWDBG_SHOT_MASK = 0x00488000;

    uint32_t g_lastR1OnlyDir = 0;
    uintptr_t g_lastR1OnlyPlayer = 0;

    bool g_prevR2Held = false;

    ULONGLONG g_r2ChargeUntilMs = 0;
    uintptr_t g_r2ChargePlayer = 0;
    uint32_t g_r2ChargeFromDir = 0;
    uint32_t g_r2ChargeToDir = 0;


    enum BallWeightDecisionReason
    {
        BW_REASON_STATE_PASS_OR_LOOSE = 1,
        BW_REASON_NOT_POSSESSION = 2,
        BW_REASON_PROTECTED = 3,
        BW_REASON_R1_R2_CHARGE = 4,
        BW_REASON_R1 = 5,
        BW_REASON_R2 = 6,
        BW_REASON_NORMAL = 7,
        BW_REASON_NO_PLAYER = 8
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
        default:                            return "UNKNOWN";
        }
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

    struct InputDir
    {
        int x; // -1 izquierda, 0 neutro, 1 derecha
        int y; // -1 abajo,    0 neutro, 1 arriba
    };

    struct R2DirectionalChargeState
    {
        bool wasR1Held = false;
        bool wasR2InternalHeld = false;

        bool pending = false;
        bool active = false;
        bool consumed = false;

        ULONGLONG pendingUntilMs = 0;
        ULONGLONG activeUntilMs = 0;

        uintptr_t actor = 0;
        InputDir dirBeforeR2{ 0, 0 };
    };

    static R2DirectionalChargeState g_r2Charge;

    constexpr int R2_CHARGE_WINDOW_MS = 600;
    constexpr int R2_INPUT_GRACE_MS = 120;

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

    static uintptr_t ReadActivePlayerGlobal()
    {
        if (!g_pesBase)
            return 0;

        const uintptr_t player =
            ReadOr<uintptr_t>(g_pesBase + ACTIVE_PLAYER_PTR_OFFSET, 0);

        return LooksLikeValidPlayer(player) ? player : 0;
    }

    static InputDir DirFromB0(uint32_t b0)
    {
        const uint32_t dirBits = b0 & 0x000000F0;

        switch (dirBits)
        {
        case 0x10: return { 0,  1 };  // arriba
        case 0x20: return { 1,  0 };  // derecha
        case 0x30: return { 1,  1 };  // arriba + derecha
        case 0x40: return { 0, -1 };  // abajo
        case 0x60: return { 1, -1 };  // abajo + derecha
        case 0x80: return { -1, 0 };  // izquierda
        case 0x90: return { -1, 1 };  // arriba + izquierda
        case 0xC0: return { -1,-1 };  // abajo + izquierda
        default:   return { 0,  0 };
        }
    }

    static InputDir GetPlayerInternalDir(uintptr_t player)
    {
        if (!LooksLikeValidPlayer(player))
            return { 0, 0 };

        const uint32_t b0 = ReadOr<uint32_t>(player + 0xB0, 0);
        return DirFromB0(b0);
    }

    static bool SameDir(InputDir a, InputDir b)
    {
        return a.x == b.x && a.y == b.y;
    }

    static int AxisCount(InputDir d)
    {
        int count = 0;

        if (d.x != 0)
            count++;

        if (d.y != 0)
            count++;

        return count;
    }

    static bool IsValidR2DirectionChange(InputDir beforeR2, InputDir current)
    {
        if (SameDir(beforeR2, current))
            return false;

        const int beforeAxes = AxisCount(beforeR2);
        const int currentAxes = AxisCount(current);

        if (beforeAxes == 0 || currentAxes == 0)
            return false;

        // Solo aceptamos cardinal <-> diagonal.
        const bool cardinalToDiagonal =
            beforeAxes == 1 && currentAxes == 2;

        const bool diagonalToCardinal =
            beforeAxes == 2 && currentAxes == 1;

        if (!cardinalToDiagonal && !diagonalToCardinal)
            return false;

        // Deben compartir eje.
        // Ejemplo válido:
        // derecha (1,0) -> arriba-derecha (1,1)
        //
        // Ejemplo inválido:
        // derecha (1,0) -> arriba-izquierda (-1,1)
        const bool sharesX =
            beforeR2.x != 0 && beforeR2.x == current.x;

        const bool sharesY =
            beforeR2.y != 0 && beforeR2.y == current.y;

        return sharesX || sharesY;
    }

    static void ResetR2DirectionalCharge()
    {
        g_r2Charge.pending = false;
        g_r2Charge.active = false;
        g_r2Charge.consumed = false;

        g_r2Charge.pendingUntilMs = 0;
        g_r2Charge.activeUntilMs = 0;

        g_r2Charge.dirBeforeR2 = { 0, 0 };
    }
    static bool UpdateR2DirectionalCharge(
        bool r1Held,
        bool r2InternalHeld,
        uintptr_t player)
    {
        const ULONGLONG now = GetTickCount64();

        if (!LooksLikeValidPlayer(player))
        {
            ResetR2DirectionalCharge();

            g_r2Charge.wasR1Held = r1Held;
            g_r2Charge.wasR2InternalHeld = r2InternalHeld;

            return false;
        }

        const bool actorChanged =
            g_r2Charge.actor != 0 && g_r2Charge.actor != player;

        if (actorChanged)
        {
            ResetR2DirectionalCharge();
            g_r2Charge.actor = player;
        }

        if (g_r2Charge.actor == 0)
        {
            g_r2Charge.actor = player;
        }

        const bool r1ReleasedNow =
            !r1Held && g_r2Charge.wasR1Held;

        if (!r1Held || r1ReleasedNow)
        {
            ResetR2DirectionalCharge();

            g_r2Charge.wasR1Held = r1Held;
            g_r2Charge.wasR2InternalHeld = r2InternalHeld;

            return false;
        }

        const InputDir currentDir = GetPlayerInternalDir(player);

        const bool r2PressedNow =
            r2InternalHeld && !g_r2Charge.wasR2InternalHeld;

        // Mientras corre con R1 y todavía no entró R2,
        // guardamos la dirección base ya procesada por PES.
        if (r1Held && !r2InternalHeld && !g_r2Charge.pending && !g_r2Charge.active)
        {
            if (AxisCount(currentDir) > 0)
                g_r2Charge.dirBeforeR2 = currentDir;
        }

        // Flanco interno de R2.
        // No activamos directamente: abrimos una pequeña gracia para que
        // la diagonal llegue en el mismo tick o unos ms después.
        if (r1Held && r2PressedNow && !g_r2Charge.consumed)
        {
            g_r2Charge.pending = true;
            g_r2Charge.pendingUntilMs =
                now + static_cast<ULONGLONG>(R2_INPUT_GRACE_MS);
        }

        if (g_r2Charge.pending)
        {
            if (now > g_r2Charge.pendingUntilMs)
            {
                g_r2Charge.pending = false;
            }
            else if (IsValidR2DirectionChange(g_r2Charge.dirBeforeR2, currentDir))
            {
                g_r2Charge.pending = false;
                g_r2Charge.active = true;
                g_r2Charge.consumed = true;
                g_r2Charge.activeUntilMs =
                    now + static_cast<ULONGLONG>(R2_CHARGE_WINDOW_MS);
            }
        }

        if (g_r2Charge.active)
        {
            const bool expired = now > g_r2Charge.activeUntilMs;

            if (expired || !r1Held || !r2InternalHeld)
            {
                g_r2Charge.active = false;
            }
        }

        g_r2Charge.wasR1Held = r1Held;
        g_r2Charge.wasR2InternalHeld = r2InternalHeld;

        return g_r2Charge.active;
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
        // 1) Mejor fuente: actor reciente de pelota.
        if (HasRecentBallActor(1000))
        {
            const uintptr_t player = GetBallActorPlayer();

            if (LooksLikeValidPlayer(player))
                return player;
        }

        // 2) Fallback importante: jugador activo/controlado por cursor.
        // Esto evita que la conducción se quede sin player cuando 37E09CC
        // no se refresca en el último segundo.
        {
            const uintptr_t player = ReadActivePlayerGlobal();

            if (LooksLikeValidPlayer(player))
                return player;
        }

        // 3) Último recurso: último actor conocido aunque no sea reciente.
        // Solo lo usamos si sigue pareciendo una estructura válida.
        {
            const uintptr_t player = GetBallActorPlayer();

            if (LooksLikeValidPlayer(player))
                return player;
        }

        return 0;
    }

    static uint32_t ReadBallU32ForBwDebug(uintptr_t offset, uint32_t fallback = 0)
    {
        if (!g_pesBase)
            return fallback;

        const uintptr_t ball =
            ReadOr<uintptr_t>(g_pesBase + BWDBG_BALL_GLOBAL_PTR_OFFSET, 0);

        if (!ball)
            return fallback;

        return ReadOr<uint32_t>(ball + offset, fallback);
    }

    static float ReadCurrentBallWeightForBwDebug()
    {
        if (!g_pesBase)
            return 0.0f;

        return ReadOr<float>(g_pesBase + BWDBG_BALL_WEIGHT_STATIC_OFFSET, 0.0f);
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
        const ULONGLONG now = GetTickCount64();

        uint32_t b0 = 0;
        uint32_t dirBits = 0;
        uint8_t p16 = 0;
        uint16_t p18 = 0;
        uint8_t p4D = 0;
        uint8_t p4F = 0;
        uint32_t p114 = 0;
        uint16_t anim30 = 0;

        bool b0R1 = false;
        bool b0R2 = false;
        bool b0Cross = false;
        bool b0Shot = false;

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

            b0R1 = (b0 & BWDBG_R1_MASK) == BWDBG_R1_MASK;
            b0R2 = (b0 & BWDBG_R2_MASK) == BWDBG_R2_MASK;
            b0Cross = (b0 & BWDBG_CROSS_MASK) == BWDBG_CROSS_MASK;
            b0Shot = (b0 & BWDBG_SHOT_MASK) == BWDBG_SHOT_MASK;
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

        const uint32_t ball50 = ReadBallU32ForBwDebug(0x50, 0);
        const uint32_t ball84 = ReadBallU32ForBwDebug(0x84, 0xFFFFFFFF);
        const uint32_t ball88 = ReadBallU32ForBwDebug(0x88, 0);
        const float currentWeight = ReadCurrentBallWeightForBwDebug();

        // Firma de cambios para no spamear demasiado.
        static uintptr_t s_lastPlayer = 0;
        static uint32_t s_lastB0 = 0xFFFFFFFF;
        static uint32_t s_lastTouchB0 = 0xFFFFFFFF;
        static int s_lastTargetInt = -1;
        static int s_lastReason = -1;
        static bool s_lastProtected = false;
        static bool s_lastInternalR1 = false;
        static bool s_lastInternalR2 = false;
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
            s_lastInternalR2 != internalR2;

        // Heartbeat corto cuando hay algo interesante, para ver timing.
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
        s_lastLogTick = now;

        LogFormat(
            "[BWDEC] state=%u ball84=%u ball50=%u ball88=%u "
            "player=0x%08X b0=0x%08X dir=0x%02X "
            "p16=%u p18=%u p4D=%u p4F=%u p114=%u anim30=0x%04X "
            "b0R1=%u b0R2=%u b0Cross=%u b0Shot=%u "
            "internalR1=%u internalR2=%u r1Held=%u r2Held=%u charge=%u protected=%u "
            "target=%.1f current=%.1f reason=%s "
            "touch=%u touchAge=%llu samePlayer=%u "
            "touchSrc=%u touchPlayer=0x%08X touchB0=0x%08X touchDir=0x%02X "
            "touchR1=%u touchR2=%u touchCross=%u touchShot=%u "
            "touchP16=%u touchP18=%u touchP4D=%u touchP4F=%u touchP114=%u touchAnim30=0x%04X "
            "touchBall50=%u touchBall84=%u touchBall88=%u",
            (unsigned int)ballState,
            (unsigned int)ball84,
            (unsigned int)ball50,
            (unsigned int)ball88,

            (unsigned int)player,
            (unsigned int)b0,
            (unsigned int)dirBits,

            (unsigned int)p16,
            (unsigned int)p18,
            (unsigned int)p4D,
            (unsigned int)p4F,
            (unsigned int)p114,
            (unsigned int)anim30,

            b0R1 ? 1u : 0u,
            b0R2 ? 1u : 0u,
            b0Cross ? 1u : 0u,
            b0Shot ? 1u : 0u,

            internalR1 ? 1u : 0u,
            internalR2 ? 1u : 0u,
            r1Held ? 1u : 0u,
            r2Held ? 1u : 0u,
            r2ChargeActive ? 1u : 0u,
            protectedAction ? 1u : 0u,

            target,
            currentWeight,
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
            hasTouch && touch.cross ? 1u : 0u,
            hasTouch && touch.shot ? 1u : 0u,

            hasTouch ? (unsigned int)touch.p16 : 0u,
            hasTouch ? (unsigned int)touch.p18 : 0u,
            hasTouch ? (unsigned int)touch.p4D : 0u,
            hasTouch ? (unsigned int)touch.p4F : 0u,
            hasTouch ? (unsigned int)touch.p114 : 0u,
            hasTouch ? (unsigned int)touch.anim30 : 0u,

            hasTouch ? (unsigned int)touch.ball50 : 0u,
            hasTouch ? (unsigned int)touch.ball84 : 0u,
            hasTouch ? (unsigned int)touch.ball88 : 0u
        );
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
                reason
            );

            return target;
        }

        if (ballState != BALL_STATE_POSSESSION)
        {
            ResetR2DirectionalCharge();

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
                reason
            );

            return target;
        }

        player = GetRecentActorPlayer();

        protectedAction =
            ShouldForceOverallForProtectedAction(player, ballState);

        if (protectedAction)
        {
            ResetR2DirectionalCharge();

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
                reason
            );

            return target;
        }

        internalR1 = IsInternalR1Sprint(player);
        internalR2 = IsInternalR2Control(player);

        r1Held = internalR1;
        r2Held = internalR2;

        r2ChargeActive =
            UpdateR2DirectionalCharge(r1Held, internalR2, player);

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
            reason
        );

        return target;
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
