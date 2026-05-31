#pragma once

#include <windows.h>

// ------------------------------------------------------------
// Capa de mapeo/validacion de configuracion del mod
// ------------------------------------------------------------

struct GameplayPhysicsConfig
{
    float overallBallWeight;
    float possessionBallWeight;
};

bool LoadGameplayPhysicsConfig(HMODULE moduleHandle);
const GameplayPhysicsConfig& GetGameplayPhysicsConfig();

float GetOverallBallWeight();
float GetPossessionBallWeight();
