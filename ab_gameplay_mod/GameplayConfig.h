#pragma once
#include <windows.h>
#include <cstdint>

// Modo de conduccion. Se carga una sola vez desde config al iniciar.
enum class ConductionMode : uint32_t
{
    Disabled = 0,
    Boost = 1,
    ProBoost = 2
};


// ------------------------------------------------------------
// Capa de mapeo/validacion de configuracion del mod
// ------------------------------------------------------------
struct GameplayPhysicsConfig
{
    float overallBallWeight;
    float ballWeightState1;
    float normalDribbleBallWeight;
    float r1BallWeight;
    float r2BallWeight;
    float r1R2BallWeight;

    // Modo de conduccion activo. Default: Boost para mantener compatibilidad.
    ConductionMode conductionMode;

    // Pesos exclusivos de ProBoost. No afectan el BoostMode clasico.
    float proR1BallWeight;
    float proR1L2BallWeight;

    // Mantiene overall durante ventanas cortas de centro/tiro/saque/pase alto
    // para que los predictores visuales no queden contaminados por peso de conduccion.
    uint32_t protectedActionLatchMs;

    // R1 + R2 + cambio direccional: tirarla larga / carga inteligente.
    uint32_t r2ChargeWindowMs;
    uint32_t r2ChargeStartDistMax;
    uint32_t r2ChargeKeepDistMax;

    // Compatibilidad historica. Deprecated: antes era el peso unico de posesion.
    float possessionBallWeight;

    // Debug/logs detallados. Para release publico deberian quedar en 0.
    uint32_t debugTouchDbg;
    uint32_t debugBwDec;
    uint32_t debugChargeDbg;
    uint32_t debugActorDebugLogger;

    // Investigacion de input L2 en player+B0. Solo logs, no cambia gameplay.
    uint32_t debugL2Input;
    uint32_t debugL2InputIntervalMs;
    uint32_t l2Mask;
};

bool LoadGameplayPhysicsConfig(HMODULE moduleHandle);
const GameplayPhysicsConfig& GetGameplayPhysicsConfig();

float GetOverallBallWeight();
float GetBallWeightState1();
float GetBallWeightNormalDribble();
float GetBallWeightR1();
float GetBallWeightR2();
float GetBallWeightR1R2();

ConductionMode GetConductionMode();
const char* GetConductionModeName();
float GetProBallWeightR1();
float GetProBallWeightR1L2();

uint32_t GetProtectedActionLatchMs();
uint32_t GetR2ChargeWindowMs();
uint32_t GetR2ChargeStartDistMax();
uint32_t GetR2ChargeKeepDistMax();

// Deprecated, conservado para compatibilidad con codigo viejo.
float GetPossessionBallWeight();

uint32_t GetDebugTouchDbg();
uint32_t GetDebugBwDec();
uint32_t GetDebugChargeDbg();
uint32_t GetDebugActorDebugLogger();

uint32_t GetDebugL2Input();
uint32_t GetDebugL2InputIntervalMs();
uint32_t GetL2Mask();
