#pragma once

#include <windows.h>
#include <stdint.h>

bool InstallContextHook(uintptr_t pesBase);

DWORD GetContextCount();
DWORD GetSavedPasser();
DWORD GetSavedReceiver();

bool ReadPassContextDistance(
    DWORD passer,
    DWORD receiver,
    BYTE* passerX,
    BYTE* passerY,
    BYTE* receiverX,
    BYTE* receiverY,
    int* distSimple
);

bool ReadPassGeometryDot(
    DWORD passer,
    DWORD receiver,
    float* outDot,
    float* outBallDist,
    float* outPassDist,
    DWORD* outPasserBallDistRaw,
    DWORD* outReceiverBallDistRaw
);