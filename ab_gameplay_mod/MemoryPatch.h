#pragma once

#include <windows.h>
#include <stdint.h>

bool CheckBytes(uintptr_t address, const unsigned char* expected, size_t count);
bool WriteJump(uintptr_t source, void* destination, size_t length);
bool WriteFloat(uintptr_t address, float value);