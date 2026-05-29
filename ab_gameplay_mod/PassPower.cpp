#include "pch.h"
#include "PassPower.h"
#include "PassPowerConfig.h"

#include "Logger.h"
#include "MemoryPatch.h"
#include "PesAddresses.h"
#include "PassContext.h"
#include "ModState.h"

// ------------------------------------------------------------
// Globals internos del módulo PassPower
// ------------------------------------------------------------

static uintptr_t g_base = 0;
static uintptr_t g_powerReturn = 0;
static uintptr_t g_call_78020 = 0;

static volatile DWORD g_powerCount = 0;

static volatile DWORD g_lastEDIOriginal = 0;
static volatile DWORD g_lastEDIModified = 0;
static volatile DWORD g_lastBall50Before = 0;
static volatile DWORD g_lastDistSimple = 0;

static volatile DWORD g_lastBoostMode = 0;
static volatile DWORD g_lastBallGateMode = 0;

static volatile DWORD g_lastPowerCtxCount = 0;
static volatile DWORD g_prevPowerCtxCount = 0;
static volatile DWORD g_lastPowerHadNewCtx = 0;

// Variables usadas por el hook naked.
static volatile DWORD g_runtimeModifiedEDI = 0;
static volatile DWORD g_savedEDIForRestore = 0;


static volatile DWORD g_lastBallD0XBits = 0;
static volatile DWORD g_lastBallD0YBits = 0;
static volatile DWORD g_lastBallD0ZBits = 0;

static volatile DWORD g_lastBall1454XBits = 0;
static volatile DWORD g_lastBall1458YBits = 0;
static volatile DWORD g_lastBall145CZBits = 0;

static volatile DWORD g_lastGeomDotBits = 0;
static volatile DWORD g_lastGeomBallDistBits = 0;
static volatile DWORD g_lastGeomPassDistBits = 0;
static volatile DWORD g_lastGeomPBallRaw = 0;
static volatile DWORD g_lastGeomRBallRaw = 0;
static volatile DWORD g_lastGeomHasData = 0;
static volatile DWORD g_lastAwkwardLongCandidate = 0;
// ------------------------------------------------------------
// Getters para logging desde dllmain.cpp
// ------------------------------------------------------------

DWORD GetPowerCount() { return g_powerCount; }
DWORD GetLastEDIOriginal() { return g_lastEDIOriginal; }
DWORD GetLastEDIModified() { return g_lastEDIModified; }
DWORD GetLastBall50Before() { return g_lastBall50Before; }
DWORD GetLastDistSimple() { return g_lastDistSimple; }
DWORD GetLastBoostMode() { return g_lastBoostMode; }
DWORD GetLastBallGateMode() { return g_lastBallGateMode; }
DWORD GetLastPowerCtxCount() { return g_lastPowerCtxCount; }
DWORD GetLastPowerHadNewCtx() { return g_lastPowerHadNewCtx; }
DWORD GetLastBallD0XBits() { return g_lastBallD0XBits; }
DWORD GetLastBallD0YBits() { return g_lastBallD0YBits; }
DWORD GetLastBallD0ZBits() { return g_lastBallD0ZBits; }
DWORD GetLastBall1454XBits() { return g_lastBall1454XBits; }
DWORD GetLastBall1458YBits() { return g_lastBall1458YBits; }
DWORD GetLastBall145CZBits() { return g_lastBall145CZBits; }
DWORD GetLastGeomDotBits() { return g_lastGeomDotBits; }
DWORD GetLastGeomBallDistBits() { return g_lastGeomBallDistBits; }
DWORD GetLastGeomPassDistBits() { return g_lastGeomPassDistBits; }
DWORD GetLastGeomPBallRaw() { return g_lastGeomPBallRaw; }
DWORD GetLastGeomRBallRaw() { return g_lastGeomRBallRaw; }
DWORD GetLastGeomHasData() { return g_lastGeomHasData; }
DWORD GetLastAwkwardLongCandidate() { return g_lastAwkwardLongCandidate; }


static DWORD ApplySoftFloor(DWORD edi, DWORD target)
{
    if (edi >= target) return edi;
    return edi + ((target - edi) / 2);
}


// ------------------------------------------------------------
// Lectura segura de ball+50 antes de que 78020 lo sobrescriba
// ------------------------------------------------------------
static DWORD ReadBall50Before()
{
    DWORD value = 0;
    __try
    {
        DWORD ballBase = *(DWORD*)(g_base + PesAddresses::BALL_GLOBAL_PTR);
        if (ballBase)
        {
            value = *(DWORD*)(ballBase + PesOffsets::BALL_POWER);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        value = 0;
    }
    return value;
}

static bool ReadBallDebugPositions(
    float* d0x, float* d0y, float* d0z,
    float* b1454x, float* b1458y, float* b145cz
)
{
    __try
    {
        DWORD ballBase = *(DWORD*)(g_base + PesAddresses::BALL_GLOBAL_PTR);
        if (!ballBase) return false;

        *d0x = *(float*)(ballBase + 0xD0);
        *d0y = *(float*)(ballBase + 0xD4);
        *d0z = *(float*)(ballBase + 0xD8);

        *b1454x = *(float*)(ballBase + 0x1454);
        *b1458y = *(float*)(ballBase + 0x1458);
        *b145cz = *(float*)(ballBase + 0x145C);

        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// ------------------------------------------------------------
// Rescate por distancia real subestimada
// ------------------------------------------------------------
// Corrige únicamente casos donde la distancia discreta del juego
// se queda corta frente a la distancia geométrica real.
//
// Ejemplo:
// distSimple = 3..4, pero geomPassDist >= 4000
// distSimple = 5,    pero geomPassDist >= 5200
//
// No toca:
// - pases sin geometría
// - distancias 0..2
// - distancias 6+
// - pases que ya quedaron suficientemente fuertes
// - pases con EDI original >= ediThresholdIgnore
static DWORD ApplyRealDistanceUnderestimateRescue(
    DWORD ediOriginal,
    DWORD edi,
    int distSimple,
    bool hasGeom,
    float geomPassDist,
    bool* outApplied
)
{
    if (outApplied)
        *outApplied = false;

    if (!hasGeom)
        return edi;

    if (ediOriginal >= g_passConfig.ediThresholdIgnore)
        return edi;

    if (distSimple < 3 || distSimple > 5)
        return edi;

    DWORD softFloor = 0;
    DWORD postCap = 0;

    if (distSimple >= 3 && distSimple <= 4)
    {
        if (geomPassDist < g_passConfig.realDistUnderDist34Min)
            return edi;

        softFloor = g_passConfig.realDistUnderDist34SoftFloor;
        postCap = g_passConfig.realDistUnderDist34PostEdiMax;
    }
    else // distSimple == 5
    {
        if (geomPassDist < g_passConfig.realDistUnderDist5Min)
            return edi;

        softFloor = g_passConfig.realDistUnderDist5SoftFloor;
        postCap = g_passConfig.realDistUnderDist5PostEdiMax;
    }

    // Si ya quedó suficientemente fuerte, no tocar.
    if (edi >= postCap)
        return edi;

    DWORD before = edi;

    // Extra chico fijo, para no romper el balance actual.
    edi += g_passConfig.realDistUnderBoostExtra;

    // Piso blando: empuja hacia target, no fuerza directo.
    edi = ApplySoftFloor(edi, softFloor);

    // Cap local de seguridad.
    if (edi > postCap)
        edi = postCap;

    if (outApplied && edi != before)
        *outApplied = true;

    return edi;
}



// ------------------------------------------------------------
// Clasificación de inercia
// ------------------------------------------------------------
static BallInertiaBand GetBallInertiaBand(DWORD ball50Before)
{
    if (ball50Before == 0) return BALL_ZERO;
    if (ball50Before <= g_passConfig.inertia.veryLowLimit) return BALL_VERY_LOW;
    if (ball50Before <= g_passConfig.inertia.lowMidLimit) return BALL_LOW_MID;
    if (ball50Before <= g_passConfig.inertia.carryLimit) return BALL_CARRY;
    return BALL_HIGH;
}

// ------------------------------------------------------------
// Debug gate mode
// ------------------------------------------------------------
static DWORD MakeGateMode(BallInertiaBand band, bool isLowRange)
{
    DWORD base = isLowRange ? 0x10 : 0x20;
    switch (band)
    {
    case BALL_ZERO: return base + 0;
    case BALL_VERY_LOW: return base + 1;
    case BALL_LOW_MID: return base + 2;
    case BALL_CARRY: return base + 3;
    case BALL_HIGH: return base + 4;
    default: return base;
    }
}

static DWORD MakeNoContextGateMode(BallInertiaBand band)
{
    return 0x50 + (DWORD)band;
}

// ------------------------------------------------------------
// Extracción de Boosts a través de Lookup Tables
// ------------------------------------------------------------
static DWORD GetLowDistanceExtra(int distSimple, BallInertiaBand band)
{
    if (distSimple <= 2) return g_passConfig.lowBoostTable.dist0_2[band];
    if (distSimple <= 4) return g_passConfig.lowBoostTable.dist3_4[band];
    if (distSimple <= 6) return g_passConfig.lowBoostTable.dist5_6[band];
    return g_passConfig.lowBoostTable.dist7_plus[band];
}

static DWORD GetMidDistanceExtra(int distSimple, BallInertiaBand band)
{
    if (distSimple <= 2) return g_passConfig.midBoostTable.dist0_2[band];
    if (distSimple <= 4) return g_passConfig.midBoostTable.dist3_4[band];
    if (distSimple <= 6) return g_passConfig.midBoostTable.dist5_6[band];
    return g_passConfig.midBoostTable.dist7_plus[band];
}

// ------------------------------------------------------------
// Ajuste fino post-boost
// ------------------------------------------------------------
// Aumenta un 5% el boost aplicado, pero solo para pases
// de distancia 3 a 6.
// No afecta:
// - pases muy cortos dist 0-2
// - pases largos dist 7+
// - noContext
// - pases sin boost
static DWORD ApplyDistance3To6FineTune(DWORD ediOriginal, DWORD edi, int distSimple)
{
    if (distSimple < 3 || distSimple > 6)
    {
        return edi;
    }

    if (edi <= ediOriginal)
    {
        return edi;
    }

    DWORD boostApplied = edi - ediOriginal;

    // +5%, redondeado hacia abajo para no pasarnos.
    DWORD extra = boostApplied / 20;

    return edi + extra;
}

// ------------------------------------------------------------
// Boost para newCtx=0
// ------------------------------------------------------------
static DWORD ApplyNoContextBoost(DWORD ediOriginal, BallInertiaBand band)
{
    if (ediOriginal >= g_passConfig.ediThresholdIgnore)
    {
        g_lastBoostMode = 0x00;
        g_lastBallGateMode = MakeNoContextGateMode(band);
        return ediOriginal;
    }

    DWORD edi = ediOriginal;

    if (edi < g_passConfig.ediThresholdLow)
    {
        edi += g_passConfig.noContextLowBaseBoost[band];
        g_lastBoostMode = 0x41;
        g_lastBallGateMode = MakeNoContextGateMode(band);
    }
    else
    {
        edi += g_passConfig.noContextMidBaseBoost;
        g_lastBoostMode = 0x42;
        g_lastBallGateMode = MakeNoContextGateMode(band);
    }
    if (edi > g_passConfig.noContextEdiCap)
    {
        // No permitir que el cap reduzca un EDI original que ya venía
        // por encima del cap. El mod nunca debería debilitar el pase.
        if (ediOriginal < g_passConfig.noContextEdiCap)
        {
            edi = g_passConfig.noContextEdiCap;
        }
        else
        {
            edi = ediOriginal;
        }
    }


    return edi;
}

// ------------------------------------------------------------
// Boost mode para debug
// ------------------------------------------------------------
static DWORD MakeLowBoostMode(int distSimple, bool rescueApplied, bool softFloorApplied, bool shortRescueApplied)
{
    if (softFloorApplied) return 0x19;
    if (shortRescueApplied) return 0x17;
    if (rescueApplied) return 0x18;
    if (distSimple >= 7) return 0x13;
    if (distSimple >= 5) return 0x15;
    if (distSimple >= 3) return 0x12;
    return 0x10;
}

static DWORD MakeMidBoostMode(int distSimple)
{
    if (distSimple >= 7) return 0x23;
    if (distSimple >= 5) return 0x25;
    return 0x20;
}



// ------------------------------------------------------------
// Cálculo principal refactorizado
// ------------------------------------------------------------
extern "C" __declspec(noinline) DWORD __cdecl CalculateModifiedEDI(DWORD ediOriginal)
{
    g_powerCount++;

    g_lastEDIOriginal = ediOriginal;
    g_lastEDIModified = ediOriginal;
    g_lastBall50Before = 0;
    g_lastDistSimple = 0;
    g_lastBoostMode = 0;
    g_lastBallGateMode = 0;
    g_lastGeomDotBits = 0;
    g_lastGeomBallDistBits = 0;
    g_lastGeomPassDistBits = 0;
    g_lastGeomPBallRaw = 0;
    g_lastGeomRBallRaw = 0;
    g_lastGeomHasData = 0;
    g_lastAwkwardLongCandidate = 0;


    DWORD ctxCount = GetContextCount();
    g_lastPowerCtxCount = ctxCount;

    if (ctxCount != g_prevPowerCtxCount)
    {
        g_lastPowerHadNewCtx = 1;
        g_prevPowerCtxCount = ctxCount;
    }
    else
    {
        g_lastPowerHadNewCtx = 0;
    }

    // --------------------------------------------------------
    // Runtime toggle:
    // Si el mod esta desactivado, el hook sigue vivo pero
    // devuelve exactamente el EDI original. Gameplay vanilla.
    // --------------------------------------------------------
    if (!IsPhysicsModEnabled())
    {
        g_lastBoostMode = 0xFE;
        g_lastBallGateMode = 0xFE;
        g_lastEDIModified = ediOriginal;
        return ediOriginal;
    }


    DWORD ball50Before = ReadBall50Before();
    g_lastBall50Before = ball50Before;

    float d0x = 0.0f, d0y = 0.0f, d0z = 0.0f;
    float b1454x = 0.0f, b1458y = 0.0f, b145cz = 0.0f;

    if (ReadBallDebugPositions(&d0x, &d0y, &d0z, &b1454x, &b1458y, &b145cz))
    {
        g_lastBallD0XBits = *(DWORD*)&d0x;
        g_lastBallD0YBits = *(DWORD*)&d0y;
        g_lastBallD0ZBits = *(DWORD*)&d0z;

        g_lastBall1454XBits = *(DWORD*)&b1454x;
        g_lastBall1458YBits = *(DWORD*)&b1458y;
        g_lastBall145CZBits = *(DWORD*)&b145cz;
    }

    BallInertiaBand ballBand = GetBallInertiaBand(ball50Before);

    if (ediOriginal >= g_passConfig.ediThresholdIgnore)
    {
        g_lastBoostMode = 0x00;
        g_lastBallGateMode = 0x00;
        g_lastEDIModified = ediOriginal;
        return ediOriginal;
    }

    if (!g_lastPowerHadNewCtx)
    {
        DWORD ediNoCtx = ApplyNoContextBoost(ediOriginal, ballBand);
        g_lastEDIModified = ediNoCtx;
        return ediNoCtx;
    }

    DWORD passer = GetSavedPasser();
    DWORD receiver = GetSavedReceiver();
    BYTE passerX = 0, passerY = 0, receiverX = 0, receiverY = 0;
    int distSimple = 0;

    bool hasDistance = ReadPassContextDistance(
        passer, receiver, &passerX, &passerY, &receiverX, &receiverY, &distSimple
    );

    if (hasDistance) g_lastDistSimple = (DWORD)distSimple;
    else { g_lastDistSimple = 0; distSimple = 0; }

    // --------------------------------------------------------
    // Geometría auxiliar:
    // No reemplaza la distancia actual. Solo detecta casos donde
    // la pelota está del lado contrario al sentido del pase.
    // --------------------------------------------------------
    float geomDot = 0.0f;
    float geomBallDist = 0.0f;
    float geomPassDist = 0.0f;
    DWORD geomPBallRaw = 0;
    DWORD geomRBallRaw = 0;

    bool hasGeom = ReadPassGeometryDot(
        passer,
        receiver,
        &geomDot,
        &geomBallDist,
        &geomPassDist,
        &geomPBallRaw,
        &geomRBallRaw
    );

    if (hasGeom)
    {
        g_lastGeomHasData = 1;
        g_lastGeomDotBits = *(DWORD*)&geomDot;
        g_lastGeomBallDistBits = *(DWORD*)&geomBallDist;
        g_lastGeomPassDistBits = *(DWORD*)&geomPassDist;
        g_lastGeomPBallRaw = geomPBallRaw;
        g_lastGeomRBallRaw = geomRBallRaw;
    }

    bool awkwardLongRescueApplied = false;
    bool awkwardShortRescueApplied = false;
    DWORD awkwardShortFinalCap = 0;

    bool awkwardShortCandidate =
        hasGeom &&
        distSimple >= (int)g_passConfig.awkwardShortDistMin &&
        distSimple <= (int)g_passConfig.awkwardShortDistMax &&
        ediOriginal < g_passConfig.awkwardShortEdiMax &&
        geomDot <= g_passConfig.awkwardShortDotMax;

    bool awkwardLongCandidate =
        hasGeom &&
        distSimple >= (int)g_passConfig.awkwardLongDistMin &&
        ediOriginal < g_passConfig.awkwardLongEdiMax &&
        geomDot <= g_passConfig.awkwardLongDotMax;

    g_lastAwkwardLongCandidate = awkwardLongCandidate ? 1 : 0;

    DWORD edi = ediOriginal;

    if (edi < g_passConfig.ediThresholdLow)
    {
        edi += g_passConfig.baseLowBoost;

        DWORD extra = GetLowDistanceExtra(distSimple, ballBand);

        bool rescueApplied = false;
        bool shortRescueApplied = false;
        bool softFloorApplied = false;

        if (ediOriginal < g_passConfig.rescueLowEdiThreshold && distSimple >= g_passConfig.rescueLowDistThreshold)
        {
            if (ballBand == BALL_HIGH) extra += g_passConfig.rescueLowExtraHighInertia;
            else extra += g_passConfig.rescueLowExtraOtherInertia;
            rescueApplied = true;
        }

        if (ediOriginal < g_passConfig.shortRescueLowEdiThreshold && distSimple >= g_passConfig.shortRescueLowDistMin && distSimple <= g_passConfig.shortRescueLowDistMax)
        {
            if (ballBand == BALL_HIGH) extra += g_passConfig.shortRescueLowExtraHighInertia;
            else extra += g_passConfig.shortRescueLowExtraOtherInertia;
            shortRescueApplied = true;
        }

        edi += extra;

        if (ediOriginal < g_passConfig.rescueLowEdiThreshold && distSimple >= g_passConfig.rescueLowDistThreshold)
        {
            DWORD beforeFloor = edi;
            if (distSimple >= 7) edi = ApplySoftFloor(edi, g_passConfig.softFloorLowDist7_plus);
            else edi = ApplySoftFloor(edi, g_passConfig.softFloorLowDist5_6);
            if (edi != beforeFloor) softFloorApplied = true;
        }

        if (ediOriginal < g_passConfig.shortRescueLowEdiThreshold && distSimple >= g_passConfig.shortRescueLowDistMin && distSimple <= g_passConfig.shortRescueLowDistMax)
        {
            DWORD beforeFloor = edi;
            edi = ApplySoftFloor(edi, g_passConfig.softFloorLowDist3_4);
            if (edi != beforeFloor) softFloorApplied = true;
        }

        // ----------------------------------------------------
        // Rescate de pase largo incómodo:
        // Solo entra si:
        // - hay geometría válida
        // - distancia actual >= awkwardLongDistMin
        // - EDI original bajo/medio-bajo
        // - dot negativo: pelota del lado contrario del pase
        // ----------------------------------------------------
        if (awkwardLongCandidate)
        {
            edi += g_passConfig.awkwardLongExtra;

            DWORD beforeAwkwardFloor = edi;
            edi = ApplySoftFloor(edi, g_passConfig.awkwardLongSoftFloor);

            if (edi != beforeAwkwardFloor)
                softFloorApplied = true;

            awkwardLongRescueApplied = true;
        }

        if (awkwardShortCandidate)
        {
            bool awkwardShortRealLong =
                geomPassDist >= g_passConfig.awkwardShortRealLongPassDistMin;

            DWORD awkwardShortFloor = awkwardShortRealLong
                ? g_passConfig.awkwardShortRealLongSoftFloor
                : g_passConfig.awkwardShortSoftFloor;

            DWORD awkwardShortCap = awkwardShortRealLong
                ? g_passConfig.awkwardShortRealLongPostEdiMax
                : g_passConfig.awkwardShortPostEdiMax;

            if (edi < awkwardShortCap)
            {
                edi += g_passConfig.awkwardShortExtra;

                DWORD beforeAwkwardShortFloor = edi;
                edi = ApplySoftFloor(edi, awkwardShortFloor);

                if (edi > awkwardShortCap)
                    edi = awkwardShortCap;

                if (edi != beforeAwkwardShortFloor)
                    softFloorApplied = true;

                awkwardShortRescueApplied = true;
                awkwardShortFinalCap = awkwardShortCap;
            }
        }

        g_lastBallGateMode = MakeGateMode(ballBand, true);

        if (awkwardShortRescueApplied)
            g_lastBoostMode = 0x1B;
        else if (awkwardLongRescueApplied)
            g_lastBoostMode = 0x1A;
        else
            g_lastBoostMode = MakeLowBoostMode(distSimple, rescueApplied, softFloorApplied, shortRescueApplied);
    }
    else
    {
        edi += g_passConfig.baseMidBoost;
        DWORD extra = GetMidDistanceExtra(distSimple, ballBand);
        edi += extra;

        g_lastBallGateMode = MakeGateMode(ballBand, false);
        g_lastBoostMode = MakeMidBoostMode(distSimple);
    }
    edi = ApplyDistance3To6FineTune(ediOriginal, edi, distSimple);

    // Reaplicar cap específico de awkwardShort después del fine tune.
    // Sin esto, ApplyDistance3To6FineTune puede volver a subir el EDI
    // por encima de awkwardShortPostEdiMax / awkwardShortRealLongPostEdiMax.
    if (awkwardShortRescueApplied && awkwardShortFinalCap != 0 && edi > awkwardShortFinalCap)
    {
        edi = awkwardShortFinalCap;
    }

    // ----------------------------------------------------
    // Rescate quirúrgico por distancia real subestimada.
    // Caso típico:
    // distSimple 3..5, pero geomPassDist real indica que
    // el pase era medio/medio-largo y quedó demasiado bajo.
    // ----------------------------------------------------
    bool realDistUnderRescueApplied = false;

    edi = ApplyRealDistanceUnderestimateRescue(
        ediOriginal,
        edi,
        distSimple,
        hasGeom,
        geomPassDist,
        &realDistUnderRescueApplied
    );

    if (realDistUnderRescueApplied)
    {
        g_lastBoostMode = 0x1C;
    }

    if (edi > g_passConfig.ediMaxCap)
    {
        edi = g_passConfig.ediMaxCap;
        g_lastBoostMode = 0x99;
    }
    g_lastEDIModified = edi;
    return edi;
}

// ------------------------------------------------------------
// Hook B: pes6.exe+1A637B
// ------------------------------------------------------------

extern "C" __declspec(naked) void Hook_Power_1A637B()
{
    __asm
    {
        // Guardar EDI original para restaurarlo después del call.
        mov dword ptr[g_savedEDIForRestore], edi

        // Guardar registros que vamos a usar / que queremos preservar.
        push eax
        push ebx
        push ecx
        push edx

        // Calcular EDI modificado.
        push edi
        call CalculateModifiedEDI
        add esp, 4

        // Guardar resultado temporalmente.
        mov dword ptr[g_runtimeModifiedEDI], eax

        // Restaurar registros originales.
        pop edx
        pop ecx
        pop ebx
        pop eax

        // Aplicar EDI modificado solo para el call original.
        mov edi, dword ptr[g_runtimeModifiedEDI]

        // Instrucciones originales reemplazadas:
        push 00
        push ebp
        push edi
        push eax
        push esi
        call dword ptr[g_call_78020]

        // Restaurar EDI original después del call.
        mov edi, dword ptr[g_savedEDIForRestore]

        // Volver a pes6.exe+1A6386.
        jmp dword ptr[g_powerReturn]
    }
}

// ------------------------------------------------------------
// Instalador del hook de potencia
// ------------------------------------------------------------

bool InstallPowerHook(uintptr_t pesBase)
{
    g_base = pesBase;
    uintptr_t hookAddress = g_base + PesAddresses::PASS_POWER_HOOK;

    const unsigned char expected[] =
    {
        0x6A, 0x00, 0x55, 0x57, 0x50, 0x56, 0xE8, 0x9A, 0x1C, 0xED, 0xFF
    };

    LogFormat(
        "Instalando hook potencia en pes6.exe+1A637B = 0x%08X",
        (unsigned int)hookAddress
    );

    if (!CheckBytes(hookAddress, expected, sizeof(expected)))
    {
        WriteLog("[ERROR] No se instala hook potencia: bytes no coinciden.");
        return false;
    }

    g_call_78020 = g_base + PesAddresses::CALL_78020;
    g_powerReturn = g_base + PesAddresses::PASS_POWER_RETURN;

    LogFormat("g_call_78020  = 0x%08X", (unsigned int)g_call_78020);
    LogFormat("g_powerReturn = 0x%08X", (unsigned int)g_powerReturn);

    if (!WriteJump(hookAddress, (void*)Hook_Power_1A637B, 11))
    {
        WriteLog("[ERROR] No se pudo escribir JMP en 1A637B.");
        return false;
    }

    WriteLog("[OK] Hook potencia instalado correctamente.");
    return true;
}