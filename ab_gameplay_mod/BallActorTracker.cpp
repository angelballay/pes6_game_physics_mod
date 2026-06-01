#include "pch.h"
#include "BallActorTracker.h"

#include "Logger.h"

#include <windows.h>
#include <cstdint>

namespace
{
    // ---------------------------------------------------------------------
    // Offsets relativos a pes6.exe
    // ---------------------------------------------------------------------

    constexpr uintptr_t BALL_ACTOR_ID_OFFSET = 0x0037E09CC;

    // Escrituras a pes6.exe+37E09CC observadas por RE.
    // Cada hook ejecuta la instruccion original y, antes de eso, intenta
    // capturar el puntero del jugador activo desde los registros.
    constexpr uintptr_t HOOK_1ADEC9_OFFSET = 0x001ADEC9; // mov [03BE09CC], dl
    constexpr uintptr_t RET_1ADEC9_OFFSET  = 0x001ADECF;
    constexpr size_t    SIZE_1ADEC9        = 6;

    constexpr uintptr_t HOOK_1AF6D6_OFFSET = 0x001AF6D6; // mov [03BE09CC], dl
    constexpr uintptr_t RET_1AF6D6_OFFSET  = 0x001AF6DC;
    constexpr size_t    SIZE_1AF6D6        = 6;

    constexpr uintptr_t HOOK_1AFB43_OFFSET = 0x001AFB43; // mov [03BE09CC], cl
    constexpr uintptr_t RET_1AFB43_OFFSET  = 0x001AFB49;
    constexpr size_t    SIZE_1AFB43        = 6;

    constexpr uintptr_t HOOK_07810D_OFFSET = 0x0007810D; // mov [03BE09CC], bl
    constexpr uintptr_t RET_07810D_OFFSET  = 0x00078113;
    constexpr size_t    SIZE_07810D        = 6;

    enum HookSource : uint32_t
    {
        SRC_1ADEC9_DL = 1,
        SRC_1AF6D6_DL = 2,
        SRC_1AFB43_CL = 3,
        SRC_07810D_BL = 4,
    };

    uintptr_t g_pesBase = 0;
    uintptr_t g_actorIdAddress = 0;

    uintptr_t g_ret_1ADEC9 = 0;
    uintptr_t g_ret_1AF6D6 = 0;
    uintptr_t g_ret_1AFB43 = 0;
    uintptr_t g_ret_07810D = 0;

    volatile LONG g_installed = 0;

    CRITICAL_SECTION g_actorCs;
    bool g_actorCsInitialized = false;

    uintptr_t g_ballActorPlayer = 0;
    uint8_t g_ballActorId = 0xFF;
    ULONGLONG g_ballActorTick = 0;

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

    uintptr_t ReadPtr(uintptr_t address, uintptr_t fallback = 0)
    {
        return ReadOr<uintptr_t>(address, fallback);
    }

    bool LooksLikeValidPlayer(uintptr_t player)
    {
        // Rango amplio observado para estructuras runtime del partido.
        // Evita aceptar basura obvia como flags, contadores o punteros nulos.
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

    bool RegisterLooksLikeActor(uintptr_t candidate, uint8_t actorRegId, uint8_t gActorBefore)
    {
        if (!LooksLikeValidPlayer(candidate))
            return false;

        const uint8_t id = ReadU8(candidate + 0x00, 0xFF);

        // Caso ideal: [player+0] coincide con el id que se esta escribiendo.
        if (actorRegId != 0xFF && actorRegId != 0 && id == actorRegId)
            return true;

        // Caso importante para 07810D_BL: BL a veces llega 0, pero
        // g37E09CC_before conserva el actor real.
        if (gActorBefore != 0xFF && gActorBefore != 0 && id == gActorBefore)
            return true;

        return false;
    }

    bool TryAcceptCandidate(uintptr_t candidate, uint8_t actorRegId, uint8_t gActorBefore, uintptr_t* outPlayer)
    {
        if (RegisterLooksLikeActor(candidate, actorRegId, gActorBefore))
        {
            if (outPlayer)
                *outPlayer = candidate;
            return true;
        }

        return false;
    }

    uintptr_t FindCandidatePlayer(
        uint32_t source,
        uint8_t actorRegId,
        uint8_t gActorBefore,
        uintptr_t eax,
        uintptr_t ebx,
        uintptr_t ecx,
        uintptr_t edx,
        uintptr_t esi,
        uintptr_t edi,
        uintptr_t ebp)
    {
        uintptr_t player = 0;

        // Rutas confirmadas por RE:
        // - 1ADEC9_DL: conduccion/toque normal. ESI suele ser el player.
        // - 1AFB43_CL: R1/R2 / pelota en movimiento. EDI suele ser el player.
        // - 07810D_BL: ruta baja muy ruidosa. EDI suele ser el player durante R1.
        // - 1AF6D6_DL: fallback, priorizamos EDI/ESI.
        switch (source)
        {
        case SRC_1ADEC9_DL:
            if (TryAcceptCandidate(esi, actorRegId, gActorBefore, &player)) return player;
            if (TryAcceptCandidate(edi, actorRegId, gActorBefore, &player)) return player;
            break;

        case SRC_1AFB43_CL:
        case SRC_07810D_BL:
        case SRC_1AF6D6_DL:
            if (TryAcceptCandidate(edi, actorRegId, gActorBefore, &player)) return player;
            if (TryAcceptCandidate(esi, actorRegId, gActorBefore, &player)) return player;
            break;
        }

        // Fallback acotado a registros, no escaneo de tabla/memoria.
        if (TryAcceptCandidate(eax, actorRegId, gActorBefore, &player)) return player;
        if (TryAcceptCandidate(ebx, actorRegId, gActorBefore, &player)) return player;
        if (TryAcceptCandidate(ecx, actorRegId, gActorBefore, &player)) return player;
        if (TryAcceptCandidate(edx, actorRegId, gActorBefore, &player)) return player;
        if (TryAcceptCandidate(ebp, actorRegId, gActorBefore, &player)) return player;

        return 0;
    }

    void StoreActor(uintptr_t player, uint8_t actorId)
    {
        if (!player || actorId == 0xFF)
            return;

        if (g_actorCsInitialized)
            EnterCriticalSection(&g_actorCs);

        g_ballActorPlayer = player;
        g_ballActorId = actorId;
        g_ballActorTick = GetTickCount64();

        if (g_actorCsInitialized)
            LeaveCriticalSection(&g_actorCs);
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

    extern "C" __declspec(noinline)
    void __stdcall BallActorTracker_OnActorWrite(
        uint32_t source,
        uint32_t actorRegIdValue,
        uintptr_t eax,
        uintptr_t ebx,
        uintptr_t ecx,
        uintptr_t edx,
        uintptr_t esi,
        uintptr_t edi,
        uintptr_t ebp)
    {
        if (!g_pesBase)
            return;

        const uint8_t actorRegId = static_cast<uint8_t>(actorRegIdValue & 0xFF);
        const uint8_t gActorBefore = ReadU8(g_pesBase + BALL_ACTOR_ID_OFFSET, 0xFF);

        const uintptr_t player = FindCandidatePlayer(
            source,
            actorRegId,
            gActorBefore,
            eax,
            ebx,
            ecx,
            edx,
            esi,
            edi,
            ebp
        );

        if (!player)
            return;

        const uint8_t playerId = ReadU8(player + 0x00, 0xFF);
        if (playerId == 0xFF)
            return;

        StoreActor(player, playerId);
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
            push 1
            call BallActorTracker_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr [g_actorIdAddress]
            mov byte ptr [eax], dl
            pop eax

            jmp dword ptr [g_ret_1ADEC9]
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
            push 2
            call BallActorTracker_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr [g_actorIdAddress]
            mov byte ptr [eax], dl
            pop eax

            jmp dword ptr [g_ret_1AF6D6]
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
            push 3
            call BallActorTracker_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr [g_actorIdAddress]
            mov byte ptr [eax], cl
            pop eax

            jmp dword ptr [g_ret_1AFB43]
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
            push 4
            call BallActorTracker_OnActorWrite

            popad
            popfd

            push eax
            mov eax, dword ptr [g_actorIdAddress]
            mov byte ptr [eax], bl
            pop eax

            jmp dword ptr [g_ret_07810D]
        }
    }

    bool InstallOneHook(const char* name, uintptr_t offset, uintptr_t hookFn, size_t size)
    {
        const uintptr_t address = g_pesBase + offset;
        const bool ok = WriteJump(address, hookFn, size);

        LogFormat(
            ok ? "[TRACKER][OK] Hook %s instalado." : "[TRACKER][ERROR] Hook %s fallo.",
            name
        );

        return ok;
    }
}

bool InstallBallActorTracker(uintptr_t pesBase)
{
    if (!pesBase)
        return false;

    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;

    g_pesBase = pesBase;
    g_actorIdAddress = pesBase + BALL_ACTOR_ID_OFFSET;

    g_ret_1ADEC9 = pesBase + RET_1ADEC9_OFFSET;
    g_ret_1AF6D6 = pesBase + RET_1AF6D6_OFFSET;
    g_ret_1AFB43 = pesBase + RET_1AFB43_OFFSET;
    g_ret_07810D = pesBase + RET_07810D_OFFSET;

    if (!g_actorCsInitialized)
    {
        InitializeCriticalSection(&g_actorCs);
        g_actorCsInitialized = true;
    }

    bool ok = true;

    ok &= InstallOneHook("1ADEC9", HOOK_1ADEC9_OFFSET, reinterpret_cast<uintptr_t>(&Hook_1ADEC9), SIZE_1ADEC9);
    ok &= InstallOneHook("1AF6D6", HOOK_1AF6D6_OFFSET, reinterpret_cast<uintptr_t>(&Hook_1AF6D6), SIZE_1AF6D6);
    ok &= InstallOneHook("1AFB43", HOOK_1AFB43_OFFSET, reinterpret_cast<uintptr_t>(&Hook_1AFB43), SIZE_1AFB43);
    ok &= InstallOneHook("07810D", HOOK_07810D_OFFSET, reinterpret_cast<uintptr_t>(&Hook_07810D), SIZE_07810D);

    LogFormat(
        ok ? "[TRACKER][OK] BallActorTracker activo."
           : "[TRACKER][WARN] BallActorTracker parcial."
    );

    return ok;
}

uintptr_t GetBallActorPlayer()
{
    uintptr_t value = 0;

    if (g_actorCsInitialized)
        EnterCriticalSection(&g_actorCs);

    value = g_ballActorPlayer;

    if (g_actorCsInitialized)
        LeaveCriticalSection(&g_actorCs);

    return value;
}

uint8_t GetBallActorId()
{
    uint8_t value = 0xFF;

    if (g_actorCsInitialized)
        EnterCriticalSection(&g_actorCs);

    value = g_ballActorId;

    if (g_actorCsInitialized)
        LeaveCriticalSection(&g_actorCs);

    return value;
}

ULONGLONG GetBallActorTick()
{
    ULONGLONG value = 0;

    if (g_actorCsInitialized)
        EnterCriticalSection(&g_actorCs);

    value = g_ballActorTick;

    if (g_actorCsInitialized)
        LeaveCriticalSection(&g_actorCs);

    return value;
}

bool HasRecentBallActor(ULONGLONG maxAgeMs)
{
    const ULONGLONG tick = GetBallActorTick();
    if (tick == 0)
        return false;

    return GetTickCount64() - tick <= maxAgeMs;
}
