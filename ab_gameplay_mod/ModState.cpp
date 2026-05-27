#include "pch.h"
#include "ModState.h"

#include <windows.h>

// Arranca activado por defecto.
static volatile LONG g_physicsModEnabled = 1;

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