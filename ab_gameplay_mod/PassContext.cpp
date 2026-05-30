#include "pch.h"
#include "PassContext.h"

#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"
#include <math.h>

// ------------------------------------------------------------
// Globals internos del módulo PassContext
// ------------------------------------------------------------

static uintptr_t g_base = 0;
static uintptr_t g_ctxReturn = 0;
static uintptr_t g_call_1A1570 = 0;

static volatile DWORD g_savedPasser = 0;
static volatile DWORD g_savedReceiver = 0;
static volatile DWORD g_ctxCount = 0;

struct Vec3
{
    float x;
    float y;
    float z;
};

static bool ReadPlayerLogicPos(DWORD player, Vec3* out)
{
    if (!player || !out)
        return false;

    __try
    {
        // Candidato encontrado por Cheat Engine:
        // player+E0 = X
        // player+E4 = altura / eje vertical
        // player+E8 = Z/Y cancha
        out->x = *(float*)(player + 0xE0);
        out->y = *(float*)(player + 0xE4);
        out->z = *(float*)(player + 0xE8);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static DWORD ReadPlayerBallDistanceRaw(DWORD player)
{
    if (!player)
        return 0;

    __try
    {
        // Candidato encontrado por Cheat Engine:
        // parece representar cercanía/distancia jugador-pelota.
        return *(DWORD*)(player + 0x114);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

static bool ReadBallWorldPos(Vec3* out)
{
    if (!out)
        return false;

    __try
    {
        DWORD ballBase = *(DWORD*)(g_base + PesAddresses::BALL_GLOBAL_PTR);
        if (!ballBase)
            return false;

        // Confirmado por Cheat Engine:
        // ball+20 = X
        // ball+24 = altura
        // ball+28 = Z/Y cancha
        out->x = *(float*)(ballBase + 0x20);
        out->y = *(float*)(ballBase + 0x24);
        out->z = *(float*)(ballBase + 0x28);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool ReadPlayerWorldPos(DWORD player, DWORD* outPhys, Vec3* out)
{
    if (!player || !outPhys || !out)
        return false;

    __try
    {
        // Hipótesis a confirmar:
        // player+0xD0 -> estructura física/posición
        DWORD phys = *(DWORD*)(player + 0xD0);
        if (!phys)
            return false;

        *outPhys = phys;

        // Confirmado en CE para la estructura física:
        // phys+00 = X
        // phys+04 = altura
        // phys+08 = Z/Y cancha
        out->x = *(float*)(phys + 0x00);
        out->y = *(float*)(phys + 0x04);
        out->z = *(float*)(phys + 0x08);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
extern "C" __declspec(noinline) void __cdecl LogPassGeometryOnly(DWORD passer, DWORD receiver, DWORD count)
{
    Vec3 ball = {};

    Vec3 pposPhys = {};
    Vec3 rposPhys = {};
    DWORD pPhys = 0;
    DWORD rPhys = 0;

    Vec3 pposLogic = {};
    Vec3 rposLogic = {};

    bool hasBall = ReadBallWorldPos(&ball);

    // Este intento hoy sabemos que falla, pero lo dejamos para comparar.
    bool hasPasserPhys = ReadPlayerWorldPos(passer, &pPhys, &pposPhys);
    bool hasReceiverPhys = ReadPlayerWorldPos(receiver, &rPhys, &rposPhys);

    // Nuevo candidato: posición directamente desde la estructura lógica.
    bool hasPasserLogic = ReadPlayerLogicPos(passer, &pposLogic);
    bool hasReceiverLogic = ReadPlayerLogicPos(receiver, &rposLogic);

    DWORD pBallDistRaw = ReadPlayerBallDistanceRaw(passer);
    DWORD rBallDistRaw = ReadPlayerBallDistanceRaw(receiver);

    LogFormat(
        "[GEOM_CTX] passId=%u passer=0x%08X receiver=0x%08X "
        "hasBall=%u "
        "pPhys=0x%08X rPhys=0x%08X hasPPhys=%u hasRPhys=%u "
        "hasPLogic=%u hasRLogic=%u "
        "ball=(%.2f,%.2f,%.2f) "
        "pPhysPos=(%.2f,%.2f,%.2f) "
        "rPhysPos=(%.2f,%.2f,%.2f) "
        "pLogic=(%.2f,%.2f,%.2f) "
        "rLogic=(%.2f,%.2f,%.2f) "
        "pBallDistRaw=%u rBallDistRaw=%u",
        (unsigned int)count,
        (unsigned int)passer,
        (unsigned int)receiver,

        hasBall ? 1 : 0,

        (unsigned int)pPhys,
        (unsigned int)rPhys,
        hasPasserPhys ? 1 : 0,
        hasReceiverPhys ? 1 : 0,

        hasPasserLogic ? 1 : 0,
        hasReceiverLogic ? 1 : 0,

        ball.x, ball.y, ball.z,

        pposPhys.x, pposPhys.y, pposPhys.z,
        rposPhys.x, rposPhys.y, rposPhys.z,

        pposLogic.x, pposLogic.y, pposLogic.z,
        rposLogic.x, rposLogic.y, rposLogic.z,

        (unsigned int)pBallDistRaw,
        (unsigned int)rBallDistRaw
    );
}

bool ReadPassGeometryDot(
    DWORD passer,
    DWORD receiver,
    float* outDot,
    float* outBallDist,
    float* outPassDist,
    DWORD* outPasserBallDistRaw,
    DWORD* outReceiverBallDistRaw
)
{
    if (!passer || !receiver || !outDot || !outBallDist || !outPassDist)
        return false;

    Vec3 ball = {};
    Vec3 ppos = {};
    Vec3 rpos = {};

    if (!ReadBallWorldPos(&ball))
        return false;

    if (!ReadPlayerLogicPos(passer, &ppos))
        return false;

    if (!ReadPlayerLogicPos(receiver, &rpos))
        return false;

    float passX = rpos.x - ppos.x;
    float passZ = rpos.z - ppos.z;

    float ballX = ball.x - ppos.x;
    float ballZ = ball.z - ppos.z;

    float passLenSq = passX * passX + passZ * passZ;
    float ballLenSq = ballX * ballX + ballZ * ballZ;

    if (passLenSq < 1.0f || ballLenSq < 1.0f)
        return false;

    float passLen = sqrtf(passLenSq);
    float ballLen = sqrtf(ballLenSq);

    float dot = ((passX * ballX) + (passZ * ballZ)) / (passLen * ballLen);

    *outDot = dot;
    *outBallDist = ballLen;
    *outPassDist = passLen;

    if (outPasserBallDistRaw)
        *outPasserBallDistRaw = ReadPlayerBallDistanceRaw(passer);

    if (outReceiverBallDistRaw)
        *outReceiverBallDistRaw = ReadPlayerBallDistanceRaw(receiver);

    return true;
}

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
        // ESI = pasador / jugador cerca de la pelota
        // EDX = receptor / destino del pase
        mov dword ptr[g_savedPasser], esi
        mov dword ptr[g_savedReceiver], edx
        inc dword ptr[g_ctxCount]

        // Log mínimo, sin cambiar lógica existente
        pushfd
        pushad

        mov eax, dword ptr[g_ctxCount]
        push eax        // count
        push edx        // receiver
        push esi        // passer
        call LogPassGeometryOnly
        add esp, 0Ch

        popad
        popfd

        // Ejecutar instrucciones originales reemplazadas:
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