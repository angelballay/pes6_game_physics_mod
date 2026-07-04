#pragma once

#include <Windows.h>
#include <cstdint>

namespace PesAddresses
{
    // Central runtime difficulty used by gameplay/AI routines.
    constexpr uintptr_t RVA_DIFFICULTY_RUNTIME = 0x37E094C;

    // Neutral probes around difficulty reads. All hooks below must execute the
    // original instruction(s) and preserve flags where the replaced instruction is CMP.
    constexpr uintptr_t RVA_MOV_CL_65F21      = 0x065F21;
    constexpr uintptr_t RVA_MOV_BL_550C1      = 0x0550C1;
    constexpr uintptr_t RVA_MOVZX_EAX_59A5FD  = 0x19A5FD;

    constexpr uintptr_t RVA_CMP_11C253        = 0x11C253;
    constexpr uintptr_t RVA_CMP_11D3D2        = 0x11D3D2;
    constexpr uintptr_t RVA_CMP_11D646        = 0x11D646;
    constexpr uintptr_t RVA_VALIDATE_11D9BB   = 0x11D9BB;
    constexpr uintptr_t RVA_CMP_11DBFC        = 0x11DBFC;
    constexpr uintptr_t RVA_CMP_11DCC3        = 0x11DCC3;
    constexpr uintptr_t RVA_CMP_11DD20        = 0x11DD20;
    constexpr uintptr_t RVA_CMP_11DE91        = 0x11DE91;
    constexpr uintptr_t RVA_TABLE_11DF53      = 0x11DF53;
    constexpr uintptr_t RVA_CMP_11E5F3        = 0x11E5F3;
    constexpr uintptr_t RVA_MOV_BL_11EAC4     = 0x11EAC4;
    constexpr uintptr_t RVA_CMP_11EB66        = 0x11EB66;
    constexpr uintptr_t RVA_SWITCH_11EBE1     = 0x11EBE1;

    // Documented positive commit site: mov [esi+16],0F; mov [esi+18],0004; then return positive.
    // Hook is optional/experimental and disabled by default in ResearchConfig.
    constexpr uintptr_t RVA_POSITIVE_11F172    = 0x11F172;

    // Read-only reference only. fix6 no longer patches this table.
    constexpr uintptr_t RVA_TABLE_78BFA8      = 0x78BFA8;

    inline uintptr_t Base()
    {
        return reinterpret_cast<uintptr_t>(::GetModuleHandleA(nullptr));
    }

    inline uintptr_t Abs(uintptr_t rva)
    {
        return Base() + rva;
    }
}
