#include "pch.h"
#include "ActorDebugLogger.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>

namespace
{
    // ---------------------------------------------------------------------
    // Direcciones relativas a pes6.exe
    // ---------------------------------------------------------------------

    constexpr uintptr_t HOOK_ACTOR_WRITE_OFFSET = 0x001ADEC9;
    constexpr uintptr_t HOOK_ACTOR_WRITE_RETURN_OFFSET = 0x001ADECF;

    constexpr uintptr_t BALL_PTR_OFFSET = 0x007CCE94;
    constexpr uintptr_t BALL_ACTOR_ID_OFFSET = 0x0037E09CC;
    constexpr uintptr_t BALL_WEIGHT_OFFSET = 0x0078AE70;

    // El hook pisa 6 bytes:
    // 005ADEC9 - 88 15 CC09BE03 - mov [pes6.exe+37E09CC],dl
    constexpr size_t HOOK_SIZE = 6;

    // ---------------------------------------------------------------------
    // Configuración del logger
    // ---------------------------------------------------------------------

    // Para evitar logs gigantescos.
    // 0 = loguea todos los hits.
    // 20/30/50 = loguea como máximo cada X ms.
    constexpr DWORD LOG_THROTTLE_MS = 5;
    // Movimiento
    constexpr int KEY_UP = VK_UP;
    constexpr int KEY_LEFT = VK_LEFT;
    constexpr int KEY_DOWN = VK_DOWN;
    constexpr int KEY_RIGHT = VK_RIGHT;

    // Botones PES
    constexpr int KEY_L2 = 'Z';
    constexpr int KEY_L1 = 'Q';

    constexpr int KEY_R2 = 'C';
    constexpr int KEY_R1 = 'E';

    constexpr int KEY_TRIANGLE = 'W'; // pase largo
    constexpr int KEY_SQUARE = 'A'; // tiro
    constexpr int KEY_CROSS = 'S'; // pase corto
    constexpr int KEY_CIRCLE = 'D'; // centro

    constexpr int KEY_SELECT = VK_F1;
    constexpr int KEY_START = VK_SPACE;

    uintptr_t g_pesBase = 0;
    uintptr_t g_actorWriteHookAddress = 0;
    uintptr_t g_actorWriteReturnAddress = 0;
    uintptr_t g_actorIdAddress = 0;

    FILE* g_actorLog = nullptr;
    CRITICAL_SECTION g_logCs;
    bool g_logCsInitialized = false;

    DWORD g_lastLogTick = 0;
    uint32_t g_lineCounter = 0;

    // ---------------------------------------------------------------------
    // Safe reads
    // ---------------------------------------------------------------------

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

    uint8_t ReadU8(uintptr_t address, uint8_t fallback = 0)
    {
        return ReadOr<uint8_t>(address, fallback);
    }

    uint16_t ReadU16(uintptr_t address, uint16_t fallback = 0)
    {
        return ReadOr<uint16_t>(address, fallback);
    }

    uint32_t ReadU32(uintptr_t address, uint32_t fallback = 0)
    {
        return ReadOr<uint32_t>(address, fallback);
    }

    float ReadFloat(uintptr_t address, float fallback = 0.0f)
    {
        return ReadOr<float>(address, fallback);
    }

    uintptr_t ReadPtr(uintptr_t address, uintptr_t fallback = 0)
    {
        return ReadOr<uintptr_t>(address, fallback);
    }

    uint16_t ReadAnim30(uintptr_t player)
    {
        const uintptr_t animPtr = ReadPtr(player + 0x04, 0);
        if (!animPtr)
            return 0;

        return ReadU16(animPtr + 0x30, 0);
    }

    bool KeyDown(int vk)
    {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    // ---------------------------------------------------------------------
    // Log file
    // ---------------------------------------------------------------------

    bool BuildLogPath(char* outPath, DWORD outSize)
    {
        // Carpeta fija para debug. Más fácil de encontrar y evita problemas
        // con Program Files / VirtualStore.
        CreateDirectoryA("D:\\pes", nullptr);

        lstrcpynA(outPath, "D:\\pes\\pes6_actor_debug.log", outSize);
        return true;
    }

    void ActorLogRaw(const char* fmt, ...)
    {
        if (!g_actorLog)
            return;

        if (g_logCsInitialized)
            EnterCriticalSection(&g_logCs);

        va_list args;
        va_start(args, fmt);
        vfprintf(g_actorLog, fmt, args);
        va_end(args);

        fflush(g_actorLog);

        if (g_logCsInitialized)
            LeaveCriticalSection(&g_logCs);
    }

    bool OpenActorLog()
    {
        if (g_actorLog)
            return true;

        char path[MAX_PATH]{};
        if (!BuildLogPath(path, MAX_PATH))
            lstrcpynA(path, "pes6_actor_debug.log", MAX_PATH);

        fopen_s(&g_actorLog, path, "w");

        if (!g_actorLog)
            return false;

        if (!g_logCsInitialized)
        {
            InitializeCriticalSection(&g_logCs);
            g_logCsInitialized = true;
        }

        ActorLogRaw("========================================\n");
        ActorLogRaw("PES6 Actor Debug Logger\n");
        ActorLogRaw("Log separado del log de pases\n");
        ActorLogRaw("Hook: pes6.exe+1ADEC9\n");
        ActorLogRaw("========================================\n\n");

        ActorLogRaw(
            "line,timeMs,player,actorIdDL,playerId,g37E09CC_before,"
            "ball,ball50,ball84,ball88,weight,"
            "keyCross,keyTriangle,keyL1,keyCircle,keySquare,keyR1,keyR2,keyL2,"
            "keyUp,keyDown,keyLeft,keyRight,"
            "B0,p16,p18,p24,p26,p28,p2C,p3F,p4D,p4F,p62,p66,p110,p114,anim30\n"
        );

        return true;
    }

    void CloseLog()
    {
        if (g_actorLog)
        {
            ActorLogRaw("\n[END]\n");
            fclose(g_actorLog);
            g_actorLog = nullptr;
        }

        if (g_logCsInitialized)
        {
            DeleteCriticalSection(&g_logCs);
            g_logCsInitialized = false;
        }
    }

    // ---------------------------------------------------------------------
    // Hook patching
    // ---------------------------------------------------------------------

    bool WriteJump(uintptr_t src, uintptr_t dst, size_t patchSize)
    {
        if (patchSize < 5)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(src), patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        const intptr_t rel = static_cast<intptr_t>(dst) - static_cast<intptr_t>(src) - 5;

        auto* p = reinterpret_cast<uint8_t*>(src);
        p[0] = 0xE9;
        *reinterpret_cast<int32_t*>(p + 1) = static_cast<int32_t>(rel);

        for (size_t i = 5; i < patchSize; ++i)
            p[i] = 0x90;

        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(src), patchSize);

        DWORD tmp = 0;
        VirtualProtect(reinterpret_cast<void*>(src), patchSize, oldProtect, &tmp);

        return true;
    }

    // ---------------------------------------------------------------------
    // Runtime logging from hook
    // ---------------------------------------------------------------------

    void LogActorSnapshot(uintptr_t player, uint8_t actorIdFromDl)
    {
        const DWORD now = GetTickCount();

        if (LOG_THROTTLE_MS > 0 && (now - g_lastLogTick) < LOG_THROTTLE_MS)
            return;

        g_lastLogTick = now;
        ++g_lineCounter;

        const uintptr_t ball = ReadPtr(g_pesBase + BALL_PTR_OFFSET, 0);

        const uint8_t gActorBefore = ReadU8(g_pesBase + BALL_ACTOR_ID_OFFSET, 0xFF);

        const uint32_t ball50 = ball ? ReadU32(ball + 0x50, 0) : 0;
        const uint32_t ball84 = ball ? ReadU32(ball + 0x84, 0xFFFFFFFF) : 0xFFFFFFFF;
        const uint32_t ball88 = ball ? ReadU32(ball + 0x88, 0) : 0;

        const float weight = ReadFloat(g_pesBase + BALL_WEIGHT_OFFSET, 0.0f);

        const uint8_t playerId = ReadU8(player + 0x00, 0xFF);
        const uint32_t b0 = ReadU32(player + 0xB0, 0);

        const uint8_t p16 = ReadU8(player + 0x16, 0);
        const uint16_t p18 = ReadU16(player + 0x18, 0);
        const uint8_t p24 = ReadU8(player + 0x24, 0);

        const uint16_t p26 = ReadU16(player + 0x26, 0);
        const uint16_t p28 = ReadU16(player + 0x28, 0);
        const uint16_t p2C = ReadU16(player + 0x2C, 0);

        const uint8_t p3F = ReadU8(player + 0x3F, 0);
        const uint8_t p4D = ReadU8(player + 0x4D, 0);
        const uint8_t p4F = ReadU8(player + 0x4F, 0);
        const uint8_t p62 = ReadU8(player + 0x62, 0);
        const uint8_t p66 = ReadU8(player + 0x66, 0);

        const uint32_t p110 = ReadU32(player + 0x110, 0);
        const uint32_t p114 = ReadU32(player + 0x114, 0);

        const uint16_t anim30 = ReadAnim30(player);

        ActorLogRaw(
            "%u,%lu,0x%08X,%u,%u,%u,"
            "0x%08X,0x%08X,%u,%u,%.3f,"
            "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "0x%08X,%u,%u,%u,0x%04X,0x%04X,0x%04X,%u,%u,%u,%u,%u,0x%08X,0x%08X,0x%04X\n",
            g_lineCounter,
            now,
            static_cast<unsigned int>(player),
            static_cast<unsigned int>(actorIdFromDl),
            static_cast<unsigned int>(playerId),
            static_cast<unsigned int>(gActorBefore),

            static_cast<unsigned int>(ball),
            static_cast<unsigned int>(ball50),
            static_cast<unsigned int>(ball84),
            static_cast<unsigned int>(ball88),
            weight,

            KeyDown(KEY_CROSS) ? 1 : 0,     // S = X / pase corto
            KeyDown(KEY_TRIANGLE) ? 1 : 0,  // W = triángulo / pase largo
            KeyDown(KEY_L1) ? 1 : 0,        // Q = L1
            KeyDown(KEY_CIRCLE) ? 1 : 0,    // D = círculo / centro
            KeyDown(KEY_SQUARE) ? 1 : 0,    // A = cuadrado / tiro
            KeyDown(KEY_R1) ? 1 : 0,        // E = R1
            KeyDown(KEY_R2) ? 1 : 0,        // C = R2
            KeyDown(KEY_L2) ? 1 : 0,        // Z = L2
            KeyDown(KEY_UP) ? 1 : 0,
            KeyDown(KEY_DOWN) ? 1 : 0,
            KeyDown(KEY_LEFT) ? 1 : 0,
            KeyDown(KEY_RIGHT) ? 1 : 0,

            static_cast<unsigned int>(b0),
            static_cast<unsigned int>(p16),
            static_cast<unsigned int>(p18),
            static_cast<unsigned int>(p24),
            static_cast<unsigned int>(p26),
            static_cast<unsigned int>(p28),
            static_cast<unsigned int>(p2C),
            static_cast<unsigned int>(p3F),
            static_cast<unsigned int>(p4D),
            static_cast<unsigned int>(p4F),
            static_cast<unsigned int>(p62),
            static_cast<unsigned int>(p66),
            static_cast<unsigned int>(p110),
            static_cast<unsigned int>(p114),
            static_cast<unsigned int>(anim30)
        );
    }

    extern "C" __declspec(noinline)
        void __stdcall ActorDebugLogger_OnActorWrite(uintptr_t player, uint32_t edxValue)
    {
        if (!g_actorLog)
            return;

        const uint8_t actorIdFromDl = static_cast<uint8_t>(edxValue & 0xFF);
        LogActorSnapshot(player, actorIdFromDl);
    }

    // ---------------------------------------------------------------------
    // Naked hook
    // ---------------------------------------------------------------------

    __declspec(naked) void ActorWriteHook()
    {
        __asm
        {
            // Estamos en 005ADEC9.
            // Original:
            // mov [pes6.exe+37E09CC], dl

            pushfd
            pushad

            // args stdcall: (uintptr_t player, uint32_t edxValue)
            push edx
            push esi
            call ActorDebugLogger_OnActorWrite

            popad
            popfd

            // Ejecutar instrucción original sin asumir base fija:
            // mov [g_actorIdAddress], dl
            push eax
            mov eax, dword ptr[g_actorIdAddress]
            mov byte ptr[eax], dl
            pop eax

            jmp dword ptr[g_actorWriteReturnAddress]
        }
    }
}

bool InstallActorDebugLogger(uintptr_t pesBase)
{
    if (!pesBase)
        return false;

    g_pesBase = pesBase;
    g_actorWriteHookAddress = pesBase + HOOK_ACTOR_WRITE_OFFSET;
    g_actorWriteReturnAddress = pesBase + HOOK_ACTOR_WRITE_RETURN_OFFSET;
    g_actorIdAddress = pesBase + BALL_ACTOR_ID_OFFSET;

    if (!OpenActorLog())
        return false;

    ActorLogRaw("[INIT] pesBase=0x%08X\n", static_cast<unsigned int>(g_pesBase));
    ActorLogRaw("[INIT] hook=0x%08X return=0x%08X actorIdAddr=0x%08X\n",
        static_cast<unsigned int>(g_actorWriteHookAddress),
        static_cast<unsigned int>(g_actorWriteReturnAddress),
        static_cast<unsigned int>(g_actorIdAddress));

    const bool ok = WriteJump(
        g_actorWriteHookAddress,
        reinterpret_cast<uintptr_t>(&ActorWriteHook),
        HOOK_SIZE
    );

    ActorLogRaw(ok ? "[OK] ActorDebugLogger hook instalado.\n\n"
        : "[ERROR] No se pudo instalar ActorDebugLogger hook.\n\n");

    return ok;
}

void CloseActorDebugLogger()
{
    CloseLog();
}