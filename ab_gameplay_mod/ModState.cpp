#include "pch.h"
#include "ModState.h"

#include <windows.h>

// El mod de pases arranca activado por defecto.
// Importante: el BallWeightController NO depende de este toggle.
// El peso de pelota queda siempre gestionado por su propio controller
// para evitar restauraciones peligrosas en medio del partido.
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
