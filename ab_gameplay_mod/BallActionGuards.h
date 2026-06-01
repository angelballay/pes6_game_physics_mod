#pragma once

#include <cstdint>

// Devuelve true cuando conviene forzar el peso base/overall del balon
// para evitar que el peso de posesion contamine predictores visuales
// de centros, tiros o saques del arquero.
bool ShouldUseOverallBallWeightForActionGuard(uintptr_t pesBase);
