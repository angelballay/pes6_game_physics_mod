#pragma once

#include <windows.h>
#include <cstdint>

// ------------------------------------------------------------
// Capa de mapeo/validacion de configuracion del mod
// ------------------------------------------------------------

struct GameplayPhysicsConfig
{
    float overallBallWeight;

    // ball+84 == 1
    float ballWeightState1;

    // ball+84 == 0, conduccion/control
    float normalDribbleBallWeight;
    float r1BallWeight;
    float r2BallWeight;
    float r1R2BallWeight;

    // Mantiene overall durante ventanas cortas de centro/tiro/saque/pase alto
    // para que los predictores visuales no queden contaminados por peso de conduccion.
    uint32_t protectedActionLatchMs;

    // R1 + R2 + cambio direccional: tirarla larga / carga inteligente.
    uint32_t r2ChargeWindowMs;
    uint32_t r2ChargeStartDistMax;
    uint32_t r2ChargeKeepDistMax;

    // Compatibilidad historica. Deprecated: antes era el peso unico de posesion.
    float possessionBallWeight;
};

bool LoadGameplayPhysicsConfig(HMODULE moduleHandle);
const GameplayPhysicsConfig& GetGameplayPhysicsConfig();

float GetOverallBallWeight();
float GetBallWeightState1();
float GetBallWeightNormalDribble();
float GetBallWeightR1();
float GetBallWeightR2();
float GetBallWeightR1R2();
uint32_t GetProtectedActionLatchMs();

uint32_t GetR2ChargeWindowMs();
uint32_t GetR2ChargeStartDistMax();
uint32_t GetR2ChargeKeepDistMax();

// Deprecated, conservado para compatibilidad con codigo viejo.
float GetPossessionBallWeight();
