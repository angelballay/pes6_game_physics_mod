#pragma once

#include <cstdint>

extern uintptr_t g_DifficultyRuntimeAbs;
extern uintptr_t g_Table78BFA8Abs;

extern uintptr_t g_Return_MOV_CL_65F21;
extern uintptr_t g_Return_MOV_BL_550C1;
extern uintptr_t g_Return_MOVZX_EAX_59A5FD;

extern uintptr_t g_Return_CMP_11C253;
extern uintptr_t g_Return_CMP_11D3D2;
extern uintptr_t g_Return_CMP_11D646;
extern uintptr_t g_Return_VALIDATE_11D9BB;
extern uintptr_t g_Return_CMP_11DBFC;
extern uintptr_t g_Return_CMP_11DCC3;
extern uintptr_t g_Return_CMP_11DD20;
extern uintptr_t g_Return_CMP_11DE91;
extern uintptr_t g_Return_TABLE_11DF53;
extern uintptr_t g_Return_CMP_11E5F3;
extern uintptr_t g_Return_MOV_BL_11EAC4;
extern uintptr_t g_Return_CMP_11EB66;
extern uintptr_t g_Return_SWITCH_11EBE1;
extern uintptr_t g_Return_POSITIVE_11F172;

extern "C" void Hook_MOV_CL_65F21_Probe();
extern "C" void Hook_MOV_BL_550C1_Probe();
extern "C" void Hook_MOVZX_EAX_59A5FD_Probe();

extern "C" void Hook_CMP_11C253_Probe();
extern "C" void Hook_CMP_11D3D2_Probe();
extern "C" void Hook_CMP_11D646_Probe();
extern "C" void Hook_VALIDATE_11D9BB_Probe();
extern "C" void Hook_CMP_11DBFC_Probe();
extern "C" void Hook_CMP_11DCC3_Probe();
extern "C" void Hook_CMP_11DD20_Probe();
extern "C" void Hook_CMP_11DE91_Probe();
extern "C" void Hook_TABLE_11DF53_Probe();
extern "C" void Hook_CMP_11E5F3_Probe();
extern "C" void Hook_MOV_BL_11EAC4_Probe();
extern "C" void Hook_CMP_11EB66_Probe();
extern "C" void Hook_SWITCH_11EBE1_Probe();
extern "C" void Hook_POSITIVE_11F172_Probe();
