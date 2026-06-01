#include "pch.h"
#include "ActorDebugLogger.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <strsafe.h>

namespace
{
    // ---------------------------------------------------------------------
    // Direcciones relativas a pes6.exe
    // ---------------------------------------------------------------------

    constexpr uintptr_t BALL_PTR_OFFSET = 0x007CCE94;
    constexpr uintptr_t BALL_ACTOR_ID_OFFSET = 0x0037E09CC;
    constexpr uintptr_t BALL_WEIGHT_OFFSET = 0x0078AE70;

    // Escrituras a pes6.exe+37E09CC que queremos observar.
    constexpr uintptr_t HOOK_1ADEC9_OFFSET = 0x001ADEC9; // mov [03BE09CC], dl
    constexpr uintptr_t RET_1ADEC9_OFFSET = 0x001ADECF;
    constexpr size_t    SIZE_1ADEC9 = 6;

    constexpr uintptr_t HOOK_1AF6D6_OFFSET = 0x001AF6D6; // mov [03BE09CC], dl
    constexpr uintptr_t RET_1AF6D6_OFFSET = 0x001AF6DC;
    constexpr size_t    SIZE_1AF6D6 = 6;

    constexpr uintptr_t HOOK_1AFB43_OFFSET = 0x001AFB43; // mov [03BE09CC], cl
    constexpr uintptr_t RET_1AFB43_OFFSET = 0x001AFB49;
    constexpr size_t    SIZE_1AFB43 = 6;

    constexpr uintptr_t HOOK_07810D_OFFSET = 0x0007810D; // mov [03BE09CC], bl
    constexpr uintptr_t RET_07810D_OFFSET = 0x00078113;
    constexpr size_t    SIZE_07810D = 6;

    // ---------------------------------------------------------------------
    // Configuración del logger
    // ---------------------------------------------------------------------

    // Para mapear R1 / doble R1 conviene 0.
    // Si el archivo queda enorme, subir a 10 o 20.
    constexpr ULONGLONG LOG_THROTTLE_MS = 0;

    // Movimiento
    constexpr int KEY_UP = VK_UP;
    constexpr int KEY_LEFT = VK_LEFT;
    constexpr int KEY_DOWN = VK_DOWN;
    constexpr int KEY_RIGHT = VK_RIGHT;

    // Botones PES según tu mapeo
    constexpr int KEY_L2 = 'Z';
    constexpr int KEY_L1 = 'Q';

    constexpr int KEY_R2 = 'C';
    constexpr int KEY_R1 = 'E';

    constexpr int KEY_TRIANGLE = 'W'; // pase largo
    constexpr int KEY_SQUARE = 'A'; // tiro
    constexpr int KEY_CROSS = 'S'; // pase corto
    constexpr int KEY_CIRCLE = 'D'; // centro

    uintptr_t g_pesBase = 0;
    uintptr_t g_actorIdAddress = 0;

    uintptr_t g_ret_1ADEC9 = 0;
    uintptr_t g_ret_1AF6D6 = 0;
    uintptr_t g_ret_1AFB43 = 0;
    uintptr_t g_ret_07810D = 0;

    FILE* g_actorLog = nullptr;
    CRITICAL_SECTION g_logCs;
    bool g_logCsInitialized = false;

    ULONGLONG g_lastLogTick = 0;
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
    // Validación de player pointer
    // ---------------------------------------------------------------------

    bool LooksLikeValidPlayer(uintptr_t player)
    {
        // Rango amplio observado para estructuras dinámicas de PES6.
        // Evita leer basura obvia como 0, flags, números chicos, etc.
        if (player < 0x01000000 || player > 0x08000000)
            return false;

        const uint8_t id = ReadU8(player + 0x00, 0xFF);
        if (id == 0xFF || id > 31)
            return false;

        const uintptr_t animPtr = ReadPtr(player + 0x04, 0);
        if (!animPtr)
            return false;

        return true;
    }

    bool RegisterLooksLikeActor(
        uintptr_t candidate,
        uint8_t actorRegId,
        uint8_t gActorBefore)
    {
        if (!LooksLikeValidPlayer(candidate))
            return false;

        const uint8_t id = ReadU8(candidate + 0x00, 0xFF);

        // Caso ideal: el registro contiene un player cuyo [player+0]
        // coincide con el id que se está escribiendo en 37E09CC.
        if (actorRegId != 0xFF && actorRegId != 0 && id == actorRegId)
            return true;

        // Caso importante para 07810D_BL:
        // BL puede venir 0, pero g37E09CC_before todavía contiene
        // el actor real anterior. Si algún registro apunta a player
        // con id == g37E09CC_before, lo aceptamos.
        if (gActorBefore != 0xFF && gActorBefore != 0 && id == gActorBefore)
            return true;

        return false;
    }

    const char* FindCandidatePlayerRegister(
        uint8_t actorRegId,
        uint8_t gActorBefore,
        uintptr_t eax,
        uintptr_t ebx,
        uintptr_t ecx,
        uintptr_t edx,
        uintptr_t esi,
        uintptr_t edi,
        uintptr_t ebp,
        uintptr_t& outPlayer)
    {
        struct Candidate
        {
            const char* name;
            uintptr_t value;
        };

        // Orden intencional:
        // ESI fue útil en 1ADEC9.
        // EDI fue útil en 1AFB43 y 07810D para R1/R2.
        Candidate candidates[] =
        {
            { "ESI", esi },
            { "EDI", edi },
            { "EAX", eax },
            { "EBX", ebx },
            { "ECX", ecx },
            { "EDX", edx },
            { "EBP", ebp },
        };

        for (const auto& c : candidates)
        {
            if (RegisterLooksLikeActor(c.value, actorRegId, gActorBefore))
            {
                outPlayer = c.value;
                return c.name;
            }
        }

        outPlayer = 0;
        return "NONE";
    }

    // ---------------------------------------------------------------------
    // Log file
    // ---------------------------------------------------------------------

    bool BuildLogPath(char* outPath, DWORD outSize)
    {
        CreateDirectoryA("D:\\pes", nullptr);

        if (outSize == 0)
            return false;

        HRESULT hr = StringCchCopyA(outPath, outSize, "D:\\pes\\pes6_actor_debug.log");
        return SUCCEEDED(hr);
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
        {
            HRESULT hr = StringCchCopyA(path, MAX_PATH, "D:\\pes\\pes6_actor_debug.log");
            if (FAILED(hr))
                return false;
        }

        fopen_s(&g_actorLog, path, "w");

        if (!g_actorLog)
            return false;

        if (!g_logCsInitialized)
        {
            InitializeCriticalSection(&g_logCs);
            g_logCsInitialized = true;
        }

        ActorLogRaw("========================================\n");
        ActorLogRaw("PES6 Actor Debug Logger - multi source\n");
        ActorLogRaw("Log separado del log de pases\n");
        ActorLogRaw("Hooks: 005ADEC9 / 005AF6D6 / 005AFB43 / 0047810D\n");
        ActorLogRaw("========================================\n\n");

        ActorLogRaw("[PATH] %s\n", path);

        ActorLogRaw(
            "line,timeMs,source,actorRegId,candidateReg,"
            "player,playerId,g37E09CC_before,"
            "EAX,EBX,ECX,EDX,ESI,EDI,EBP,"
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
    // Runtime logging from hooks
    // ---------------------------------------------------------------------

    void LogActorSnapshot(
        const char* source,
        uint8_t actorRegId,
        uintptr_t eax,
        uintptr_t ebx,
        uintptr_t ecx,
        uintptr_t edx,
        uintptr_t esi,
        uintptr_t edi,
        uintptr_t ebp)
    {
        const ULONGLONG now = GetTickCount64();

        if (LOG_THROTTLE_MS > 0 && (now - g_lastLogTick) < LOG_THROTTLE_MS)
            return;

        g_lastLogTick = now;
        ++g_lineCounter;

        const uint8_t gActorBefore = ReadU8(g_pesBase + BALL_ACTOR_ID_OFFSET, 0xFF);

        uintptr_t player = 0;
        const char* candidateReg = FindCandidatePlayerRegister(
            actorRegId,
            gActorBefore,
            eax,
            ebx,
            ecx,
            edx,
            esi,
            edi,
            ebp,
            player
        );

        const uintptr_t ball = ReadPtr(g_pesBase + BALL_PTR_OFFSET, 0);

        const uint32_t ball50 = ball ? ReadU32(ball + 0x50, 0) : 0;
        const uint32_t ball84 = ball ? ReadU32(ball + 0x84, 0xFFFFFFFF) : 0xFFFFFFFF;
        const uint32_t ball88 = ball ? ReadU32(ball + 0x88, 0) : 0;

        const float weight = ReadFloat(g_pesBase + BALL_WEIGHT_OFFSET, 0.0f);

        const uint8_t playerId = player ? ReadU8(player + 0x00, 0xFF) : 0xFF;
        const uint32_t b0 = player ? ReadU32(player + 0xB0, 0) : 0;

        const uint8_t p16 = player ? ReadU8(player + 0x16, 0) : 0;
        const uint16_t p18 = player ? ReadU16(player + 0x18, 0) : 0;
        const uint8_t p24 = player ? ReadU8(player + 0x24, 0) : 0;

        const uint16_t p26 = player ? ReadU16(player + 0x26, 0) : 0;
        const uint16_t p28 = player ? ReadU16(player + 0x28, 0) : 0;
        const uint16_t p2C = player ? ReadU16(player + 0x2C, 0) : 0;

        const uint8_t p3F = player ? ReadU8(player + 0x3F, 0) : 0;
        const uint8_t p4D = player ? ReadU8(player + 0x4D, 0) : 0;
        const uint8_t p4F = player ? ReadU8(player + 0x4F, 0) : 0;
        const uint8_t p62 = player ? ReadU8(player + 0x62, 0) : 0;
        const uint8_t p66 = player ? ReadU8(player + 0x66, 0) : 0;

        const uint32_t p110 = player ? ReadU32(player + 0x110, 0) : 0;
        const uint32_t p114 = player ? ReadU32(player + 0x114, 0) : 0;

        const uint16_t anim30 = player ? ReadAnim30(player) : 0;

        ActorLogRaw(
            "%u,%llu,%s,%u,%s,"
            "0x%08X,%u,%u,"
            "0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,0x%08X,"
            "0x%08X,0x%08X,%u,%u,%.3f,"
            "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
            "0x%08X,%u,%u,%u,0x%04X,0x%04X,0x%04X,%u,%u,%u,%u,%u,0x%08X,0x%08X,0x%04X\n",
            g_lineCounter,
            static_cast<unsigned long long>(now),
            source,
            static_cast<unsigned int>(actorRegId),
            candidateReg,

            static_cast<unsigned int>(player),
            static_cast<unsigned int>(playerId),
            static_cast<unsigned int>(gActorBefore),

            static_cast<unsigned int>(eax),
            static_cast<unsigned int>(ebx),
            static_cast<unsigned int>(ecx),
            static_cast<unsigned int>(edx),
            static_cast<unsigned int>(esi),
            static_cast<unsigned int>(edi),
            static_cast<unsigned int>(ebp),

            static_cast<unsigned int>(ball),
            static_cast<unsigned int>(ball50),
            static_cast<unsigned int>(ball84),
            static_cast<unsigned int>(ball88),
            weight,

            KeyDown(KEY_CROSS) ? 1 : 0,
            KeyDown(KEY_TRIANGLE) ? 1 : 0,
            KeyDown(KEY_L1) ? 1 : 0,
            KeyDown(KEY_CIRCLE) ? 1 : 0,
            KeyDown(KEY_SQUARE) ? 1 : 0,
            KeyDown(KEY_R1) ? 1 : 0,
            KeyDown(KEY_R2) ? 1 : 0,
            KeyDown(KEY_L2) ? 1 : 0,
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
        void __stdcall ActorDebugLogger_OnActorWrite(
            const char* source,
            uint32_t actorRegId,
            uintptr_t eax,
            uintptr_t ebx,
            uintptr_t ecx,
            uintptr_t edx,
            uintptr_t esi,
            uintptr_t edi,
            uintptr_t ebp)
    {
        if (!g_actorLog)
            return;

        LogActorSnapshot(
            source,
            static_cast<uint8_t>(actorRegId & 0xFF),
            eax,
            ebx,
            ecx,
            edx,
            esi,
            edi,
            ebp
        );
    }

    // ---------------------------------------------------------------------
    // Naked hooks
    // ---------------------------------------------------------------------

    __declspec(naked) void Hook_1ADEC9()
    {
        __asm
        {
            // Original: mov [03BE09CC], dl

            pushfd
            pushad

            push ebp
            push edi
            push esi
            push edx
            push ecx
            push ebx
            push eax
            movzx eax, dl
            push eax
            push offset Source_1ADEC9
            call ActorDebugLogger_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr[g_actorIdAddress]
            mov byte ptr[eax], dl
            pop eax

            jmp dword ptr[g_ret_1ADEC9]

            Source_1ADEC9:
            __emit '1'
                __emit 'A'
                __emit 'D'
                __emit 'E'
                __emit 'C'
                __emit '9'
                __emit '_'
                __emit 'D'
                __emit 'L'
                __emit 0
        }
    }

    __declspec(naked) void Hook_1AF6D6()
    {
        __asm
        {
            // Original: mov [03BE09CC], dl

            pushfd
            pushad

            push ebp
            push edi
            push esi
            push edx
            push ecx
            push ebx
            push eax
            movzx eax, dl
            push eax
            push offset Source_1AF6D6
            call ActorDebugLogger_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr[g_actorIdAddress]
            mov byte ptr[eax], dl
            pop eax

            jmp dword ptr[g_ret_1AF6D6]

            Source_1AF6D6:
            __emit '1'
                __emit 'A'
                __emit 'F'
                __emit '6'
                __emit 'D'
                __emit '6'
                __emit '_'
                __emit 'D'
                __emit 'L'
                __emit 0
        }
    }

    __declspec(naked) void Hook_1AFB43()
    {
        __asm
        {
            // Original: mov [03BE09CC], cl

            pushfd
            pushad

            push ebp
            push edi
            push esi
            push edx
            push ecx
            push ebx
            push eax
            movzx eax, cl
            push eax
            push offset Source_1AFB43
            call ActorDebugLogger_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr[g_actorIdAddress]
            mov byte ptr[eax], cl
            pop eax

            jmp dword ptr[g_ret_1AFB43]

            Source_1AFB43:
            __emit '1'
                __emit 'A'
                __emit 'F'
                __emit 'B'
                __emit '4'
                __emit '3'
                __emit '_'
                __emit 'C'
                __emit 'L'
                __emit 0
        }
    }

    __declspec(naked) void Hook_07810D()
    {
        __asm
        {
            // Original: mov [03BE09CC], bl

            pushfd
            pushad

            push ebp
            push edi
            push esi
            push edx
            push ecx
            push ebx
            push eax
            movzx eax, bl
            push eax
            push offset Source_07810D
            call ActorDebugLogger_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr[g_actorIdAddress]
            mov byte ptr[eax], bl
            pop eax

            jmp dword ptr[g_ret_07810D]

            Source_07810D:
            __emit '0'
                __emit '7'
                __emit '8'
                __emit '1'
                __emit '0'
                __emit 'D'
                __emit '_'
                __emit 'B'
                __emit 'L'
                __emit 0
        }
    }

    bool InstallOneHook(const char* name, uintptr_t offset, uintptr_t hookFn, size_t size)
    {
        const uintptr_t address = g_pesBase + offset;
        const bool ok = WriteJump(address, hookFn, size);

        ActorLogRaw(
            ok ? "[OK] Hook %s instalado.\n" : "[ERROR] Hook %s fallo.\n",
            name
        );

        return ok;
    }
}

bool InstallActorDebugLogger(uintptr_t pesBase)
{
    if (!pesBase)
        return false;

    g_pesBase = pesBase;
    g_actorIdAddress = pesBase + BALL_ACTOR_ID_OFFSET;

    g_ret_1ADEC9 = pesBase + RET_1ADEC9_OFFSET;
    g_ret_1AF6D6 = pesBase + RET_1AF6D6_OFFSET;
    g_ret_1AFB43 = pesBase + RET_1AFB43_OFFSET;
    g_ret_07810D = pesBase + RET_07810D_OFFSET;

    if (!OpenActorLog())
        return false;

    ActorLogRaw(
        "[INIT] pesBase=0x%08X actorIdAddr=0x%08X\n",
        static_cast<unsigned int>(g_pesBase),
        static_cast<unsigned int>(g_actorIdAddress)
    );

    bool ok = true;

    ok &= InstallOneHook(
        "1ADEC9",
        HOOK_1ADEC9_OFFSET,
        reinterpret_cast<uintptr_t>(&Hook_1ADEC9),
        SIZE_1ADEC9
    );

    ok &= InstallOneHook(
        "1AF6D6",
        HOOK_1AF6D6_OFFSET,
        reinterpret_cast<uintptr_t>(&Hook_1AF6D6),
        SIZE_1AF6D6
    );

    ok &= InstallOneHook(
        "1AFB43",
        HOOK_1AFB43_OFFSET,
        reinterpret_cast<uintptr_t>(&Hook_1AFB43),
        SIZE_1AFB43
    );

    ok &= InstallOneHook(
        "07810D",
        HOOK_07810D_OFFSET,
        reinterpret_cast<uintptr_t>(&Hook_07810D),
        SIZE_07810D
    );

    ActorLogRaw(
        ok ? "[OK] ActorDebugLogger multi-hook activo.\n\n"
        : "[WARN] ActorDebugLogger multi-hook parcial.\n\n"
    );

    return ok;
}

void CloseActorDebugLogger()
{
    CloseLog();
}