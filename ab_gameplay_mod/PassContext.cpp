#include "pch.h"
#include "PassContext.h"

#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"

// ------------------------------------------------------------
// Globals internos del módulo PassContext
// ------------------------------------------------------------

static uintptr_t g_base = 0;
static uintptr_t g_ctxReturn = 0;
static uintptr_t g_call_1A1570 = 0;

static volatile DWORD g_savedPasser = 0;
static volatile DWORD g_savedReceiver = 0;
static volatile DWORD g_ctxCount = 0;

// ------------------------------------------------------------
// Getters
// ------------------------------------------------------------

DWORD GetContextCount()
{
    return g_ctxCount;
}

DWORD GetSavedPasser()
{
    return g_savedPasser;
}

DWORD GetSavedReceiver()
{
    return g_savedReceiver;
}

// ------------------------------------------------------------
// Distancia discreta entre pasador y receptor
//
// jugador+204 = celda X aproximada
// jugador+205 = celda Y/Z aproximada
//
// distSimple = abs(receiverX - passerX)
//            + abs(receiverY - passerY)
// ------------------------------------------------------------

bool ReadPassContextDistance(
    DWORD passer,
    DWORD receiver,
    BYTE* passerX,
    BYTE* passerY,
    BYTE* receiverX,
    BYTE* receiverY,
    int* distSimple
)
{
    if (!passer || !receiver)
    {
        return false;
    }

    __try
    {
        *passerX = *(BYTE*)(passer + PesOffsets::PLAYER_CELL_X);
        *passerY = *(BYTE*)(passer + PesOffsets::PLAYER_CELL_Y);

        *receiverX = *(BYTE*)(receiver + PesOffsets::PLAYER_CELL_X);
        *receiverY = *(BYTE*)(receiver + PesOffsets::PLAYER_CELL_Y);

        int dx = (*receiverX >= *passerX)
            ? (*receiverX - *passerX)
            : (*passerX - *receiverX);

        int dy = (*receiverY >= *passerY)
            ? (*receiverY - *passerY)
            : (*passerY - *receiverY);

        *distSimple = dx + dy;

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ------------------------------------------------------------
// Hook A: pes6.exe+1A5905
//
// Original:
//   call pes6.exe+1A1570
//   mov ebx,eax
//
// Confirmado:
//   EDX = pasador
//   ESI = receptor
// ------------------------------------------------------------

extern "C" __declspec(naked) void Hook_Context_1A5905()
{
    __asm
    {
        // Guardar contexto del pase
        mov dword ptr[g_savedPasser], edx
        mov dword ptr[g_savedReceiver], esi
        inc dword ptr[g_ctxCount]

        // Ejecutar instrucciones originales reemplazadas:
        // call pes6.exe+1A1570
        call dword ptr[g_call_1A1570]

        // mov ebx,eax
        mov ebx, eax

        // Volver a pes6.exe+1A590C
        jmp dword ptr[g_ctxReturn]
    }
}

// ------------------------------------------------------------
// Instalador del hook de contexto
// ------------------------------------------------------------

bool InstallContextHook(uintptr_t pesBase)
{
    g_base = pesBase;

    uintptr_t hookAddress = g_base + PesAddresses::PASS_CONTEXT_HOOK;

    const unsigned char expected[] =
    {
        0xE8, 0x66, 0xBC, 0xFF, 0xFF, 0x8B, 0xD8
    };

    LogFormat(
        "Instalando hook contexto en pes6.exe+1A5905 = 0x%08X",
        (unsigned int)hookAddress
    );

    if (!CheckBytes(hookAddress, expected, sizeof(expected)))
    {
        WriteLog("[ERROR] No se instala hook contexto: bytes no coinciden.");
        return false;
    }

    g_call_1A1570 = g_base + PesAddresses::CALL_1A1570;
    g_ctxReturn = g_base + PesAddresses::PASS_CONTEXT_RETURN;

    LogFormat("g_call_1A1570 = 0x%08X", (unsigned int)g_call_1A1570);
    LogFormat("g_ctxReturn   = 0x%08X", (unsigned int)g_ctxReturn);

    if (!WriteJump(hookAddress, (void*)Hook_Context_1A5905, 7))
    {
        WriteLog("[ERROR] No se pudo escribir JMP en 1A5905.");
        return false;
    }

    WriteLog("[OK] Hook contexto instalado correctamente.");
    return true;
}