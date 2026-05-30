#include "pch.h"
#include "ModState.h"
#include "MemoryPatch.h"

#include <windows.h>

// El mod arranca activado por defecto.
static volatile LONG g_physicsModEnabled = 1;

// Dirección absoluta de la masa de la pelota.
constexpr uintptr_t BALL_MASS_ADDRESS = 0x00B8AE70;

static void ApplyBallMass(bool enabled)
{
    float newMass = enabled ? 198.0f : 188.0f;
    WriteFloat(BALL_MASS_ADDRESS, newMass);
}

bool IsPhysicsModEnabled()
{
    return InterlockedCompareExchange(&g_physicsModEnabled, 0, 0) != 0;
}

void SetPhysicsModEnabled(bool enabled)
{
    InterlockedExchange(&g_physicsModEnabled, enabled ? 1 : 0);
    ApplyBallMass(enabled);
}

bool TogglePhysicsModEnabled()
{
    LONG current = InterlockedCompareExchange(&g_physicsModEnabled, 0, 0);
    LONG next = current ? 0 : 1;

    InterlockedExchange(&g_physicsModEnabled, next);
    ApplyBallMass(next != 0);

    return next != 0;
}