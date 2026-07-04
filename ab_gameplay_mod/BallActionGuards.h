#pragma once

#include <cstdint>
#include <windows.h>

// Devuelve true cuando conviene forzar overall_ball_weight para evitar que
// pesos de conduccion contaminen predictores visuales de centros, tiros,
// Q+W/pase alto o saques de arquero.
// Usa el puntero directo del jugador activo obtenido por BallActorTracker.
bool ShouldForceOverallForProtectedAction(uintptr_t player, DWORD ballState);
