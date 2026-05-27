#pragma once

#include <windows.h>

// ------------------------------------------------------------
// Clasificación de inercia previa
// ------------------------------------------------------------
enum BallInertiaBand
{
    BALL_ZERO = 0,
    BALL_VERY_LOW = 1,
    BALL_LOW_MID = 2,
    BALL_CARRY = 3,
    BALL_HIGH = 4
};

// Definimos la configuración para las inercias
struct BallInertiaConfig {
    DWORD veryLowLimit;
    DWORD lowMidLimit;
    DWORD carryLimit;
};

// Configuramos los boosts por distancia e inercia usando Arrays (Lookup Tables)
// El índice del array corresponde al BallInertiaBand (0 = ZERO, 1 = VERY_LOW, 2 = LOW_MID, 3 = CARRY, 4 = HIGH)
struct DistanceBoostTable {
    DWORD dist0_2[5];
    DWORD dist3_4[5];
    DWORD dist5_6[5];
    DWORD dist7_plus[5];
};

struct PassPowerConfig {
    BallInertiaConfig inertia;

    DistanceBoostTable lowBoostTable;
    DistanceBoostTable midBoostTable;
    
    // Boost base sumado siempre en LOW y MID
    DWORD baseLowBoost;
    DWORD baseMidBoost;

    // Umbrales de EDI
    DWORD ediThresholdLow;    // Por debajo es pase LOW
    DWORD ediThresholdIgnore; // Por encima no se toca el pase
    DWORD ediMaxCap;          // Tope máximo de seguridad

    // No context soft boost
    DWORD noContextLowBaseBoost[5];
    DWORD noContextMidBaseBoost;
    DWORD noContextEdiCap;

    // Rescate para pase castigado LOW
    DWORD rescueLowEdiThreshold;
    DWORD rescueLowDistThreshold;
    DWORD rescueLowExtraHighInertia;
    DWORD rescueLowExtraOtherInertia;

    // Rescate corto/medio-corto LOW
    DWORD shortRescueLowEdiThreshold;
    DWORD shortRescueLowDistMin;
    DWORD shortRescueLowDistMax;
    DWORD shortRescueLowExtraHighInertia;
    DWORD shortRescueLowExtraOtherInertia;

    // Pisos blandos
    DWORD softFloorLowDist5_6;
    DWORD softFloorLowDist7_plus;
    DWORD softFloorLowDist3_4;
};

// Instancia global con la configuración por defecto
extern PassPowerConfig g_passConfig;