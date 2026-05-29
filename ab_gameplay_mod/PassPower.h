#pragma once

#include <windows.h>
#include <stdint.h>

bool InstallPowerHook(uintptr_t pesBase);

DWORD GetPowerCount();
DWORD GetLastEDIOriginal();
DWORD GetLastEDIModified();
DWORD GetLastBall50Before();
DWORD GetLastDistSimple();
DWORD GetLastBoostMode();
DWORD GetLastBallGateMode();
DWORD GetLastPowerCtxCount();
DWORD GetLastPowerHadNewCtx();
DWORD GetLastBallD0XBits();
DWORD GetLastBallD0YBits();
DWORD GetLastBallD0ZBits();

DWORD GetLastBall1454XBits();
DWORD GetLastBall1458YBits();
DWORD GetLastBall145CZBits();

DWORD GetLastGeomDotBits();
DWORD GetLastGeomBallDistBits();
DWORD GetLastGeomPassDistBits();
DWORD GetLastGeomPBallRaw();
DWORD GetLastGeomRBallRaw();
DWORD GetLastGeomHasData();
DWORD GetLastAwkwardLongCandidate();