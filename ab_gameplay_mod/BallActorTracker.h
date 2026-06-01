#pragma once

#include <cstdint>
#include <windows.h>

// Tracker liviano del actor/jugador que controla o toca la pelota.
// No escribe logs. Solo reutiliza las rutas RE donde PES 6 actualiza
// pes6.exe+37E09CC para guardar el puntero runtime del jugador activo.
bool InstallBallActorTracker(uintptr_t pesBase);

uintptr_t GetBallActorPlayer();
uint8_t GetBallActorId();
ULONGLONG GetBallActorTick();
bool HasRecentBallActor(ULONGLONG maxAgeMs = 1000);
