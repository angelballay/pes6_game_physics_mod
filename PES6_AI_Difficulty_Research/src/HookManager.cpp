#include "HookManager.h"
#include "HookStubs.h"
#include "PesAddresses.h"

#include <cstring>

HookManager& HookManager::Instance()
{
    static HookManager instance;
    return instance;
}

bool HookManager::InstallAll()
{
    const uintptr_t base = PesAddresses::Base();
    if (base == 0)
        return false;

    g_DifficultyRuntimeAbs = base + PesAddresses::RVA_DIFFICULTY_RUNTIME;
    g_Table78BFA8Abs = base + PesAddresses::RVA_TABLE_78BFA8;

    g_Return_MOV_CL_65F21     = base + 0x065F27;
    g_Return_MOV_BL_550C1     = base + 0x0550C7;
    g_Return_MOVZX_EAX_59A5FD = base + 0x19A604;

    g_Return_CMP_11C253       = base + 0x11C25A;
    g_Return_CMP_11D3D2       = base + 0x11D3D9;
    g_Return_CMP_11D646       = base + 0x11D64D;
    g_Return_VALIDATE_11D9BB  = base + 0x11D9C2;
    g_Return_CMP_11DBFC       = base + 0x11DC03;
    g_Return_CMP_11DCC3       = base + 0x11DCCA;
    g_Return_CMP_11DD20       = base + 0x11DD27;
    g_Return_CMP_11DE91       = base + 0x11DE98;
    g_Return_TABLE_11DF53     = base + 0x11DF5F;
    g_Return_CMP_11E5F3       = base + 0x11E5FA;
    g_Return_MOV_BL_11EAC4    = base + 0x11EACA;
    g_Return_CMP_11EB66       = base + 0x11EB6D;
    g_Return_SWITCH_11EBE1    = base + 0x11EBE8;

    bool ok = true;

    // No gameplay tweaks in fix7. Every hook below is a neutral probe.
    ok = ok && InstallJump(base + PesAddresses::RVA_MOV_CL_65F21,     reinterpret_cast<void*>(&Hook_MOV_CL_65F21_Probe), 6);
    ok = ok && InstallJump(base + PesAddresses::RVA_MOV_BL_550C1,     reinterpret_cast<void*>(&Hook_MOV_BL_550C1_Probe), 6);
    ok = ok && InstallJump(base + PesAddresses::RVA_MOVZX_EAX_59A5FD, reinterpret_cast<void*>(&Hook_MOVZX_EAX_59A5FD_Probe), 7);

    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11C253,       reinterpret_cast<void*>(&Hook_CMP_11C253_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11D3D2,       reinterpret_cast<void*>(&Hook_CMP_11D3D2_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11D646,       reinterpret_cast<void*>(&Hook_CMP_11D646_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_VALIDATE_11D9BB,  reinterpret_cast<void*>(&Hook_VALIDATE_11D9BB_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11DBFC,       reinterpret_cast<void*>(&Hook_CMP_11DBFC_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11DCC3,       reinterpret_cast<void*>(&Hook_CMP_11DCC3_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11DD20,       reinterpret_cast<void*>(&Hook_CMP_11DD20_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11DE91,       reinterpret_cast<void*>(&Hook_CMP_11DE91_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_TABLE_11DF53,     reinterpret_cast<void*>(&Hook_TABLE_11DF53_Probe), 12);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11E5F3,       reinterpret_cast<void*>(&Hook_CMP_11E5F3_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_MOV_BL_11EAC4,    reinterpret_cast<void*>(&Hook_MOV_BL_11EAC4_Probe), 6);
    ok = ok && InstallJump(base + PesAddresses::RVA_CMP_11EB66,       reinterpret_cast<void*>(&Hook_CMP_11EB66_Probe), 7);
    ok = ok && InstallJump(base + PesAddresses::RVA_SWITCH_11EBE1,    reinterpret_cast<void*>(&Hook_SWITCH_11EBE1_Probe), 7);

    return ok;
}

void HookManager::RemoveAll()
{
    // Research DLL: no runtime unhook for now.
}

bool HookManager::InstallJump(uintptr_t target, void* detour, size_t length)
{
    if (length < 5 || target == 0 || detour == nullptr)
        return false;

    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(target), length, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    const uintptr_t src = target;
    const uintptr_t dst = reinterpret_cast<uintptr_t>(detour);
    const int32_t rel = static_cast<int32_t>(dst - src - 5);

    auto* bytes = reinterpret_cast<uint8_t*>(target);
    bytes[0] = 0xE9;
    std::memcpy(bytes + 1, &rel, sizeof(rel));

    for (size_t i = 5; i < length; ++i)
        bytes[i] = 0x90;

    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target), length);

    DWORD temp = 0;
    VirtualProtect(reinterpret_cast<void*>(target), length, oldProtect, &temp);
    return true;
}
