#include "pch.h"
#include "ModState.h"
#include "GameplayConfig.h"
#include "MemoryPatch.h"

#include <windows.h>

// El mod arranca activado por defecto.
static volatile LONG g_physicsModEnabled = 1;

// Direccion absoluta historica de la constante fisica de peso/magnetismo.
// Se mantiene aqui para no acoplar el toggle al base address del EXE.
constexpr uintptr_t BALL_WEIGHT_ADDRESS = 0x00B8AE70;
constexpr float VANILLA_BALL_WEIGHT = 188.0f;

bool IsPhysicsModEnabled()
{
    return InterlockedCompareExchange(&g_physicsModEnabled, 0, 0) != 0;
}

void SetPhysicsModEnabled(bool enabled)
{
    InterlockedExchange(&g_physicsModEnabled, enabled ? 1 : 0);
}

bool TogglePhysicsModEnabled()
{
    LONG current = InterlockedCompareExchange(&g_physicsModEnabled, 0, 0);
    LONG next = current ? 0 : 1;

    InterlockedExchange(&g_physicsModEnabled, next);

    return next != 0;
}
