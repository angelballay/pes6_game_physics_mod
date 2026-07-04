#pragma once

#include <stdint.h>

// ------------------------------------------------------------
// Parche extra de conduccion estilo PS2.
// ------------------------------------------------------------
// Replica el prototipo Lua:
//   si ball+84 == 0 -> 00B8AE70 = possession_ball_weight
//   si ball+84 != 0 -> 00B8AE70 = overall_ball_weight
//
// Corre en un thread propio y lee la config ya cargada en memoria.

bool StartBallWeightController(uintptr_t pesBase);
void StopBallWeightController();
