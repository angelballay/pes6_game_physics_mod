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

struct BallTouchDebugSnapshot
{
    uintptr_t player;
    uint32_t source;
    uint32_t b0;
    uint32_t dirBits;
    uint32_t ball50;
    uint32_t ball84;
    uint32_t ball88;
    uint8_t p16;
    uint16_t p18;
    uint8_t p4D;
    uint8_t p4F;
    uint16_t anim30;
    uint32_t p114;
    bool r1;
    bool r2;
    bool cross;
    bool shot;
    ULONGLONG tick;
};

bool GetLastTouchDebugSnapshot(BallTouchDebugSnapshot* out);