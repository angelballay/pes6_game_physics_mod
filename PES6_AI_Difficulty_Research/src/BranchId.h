#pragma once

#include <cstdint>

enum class BranchId : uint32_t
{
    NONE = 0,

    // fix7: neutral semantic probes + 11C253/state-change focus. These hooks execute the original instruction(s)
    // and then log context. They do not change difficulty, tables, thresholds or actor fields.
    PROBE_MOV_CL_65F21      = 101, // 00465F21 - mov cl,[diff]
    PROBE_MOV_BL_550C1      = 102, // 004550C1 - mov bl,[diff] -> jmp 11E517
    PROBE_MOVZX_EAX_59A5FD  = 103, // 0059A5FD - movzx eax,[diff] -> jmp 11D298

    PROBE_CMP_11C253        = 110, // 0051C253 - cmp byte ptr [diff],02
    PROBE_CMP_11D3D2        = 111, // 0051D3D2 - cmp byte ptr [diff],02
    PROBE_CMP_11D646        = 112, // 0051D646 - cmp byte ptr [diff],02
    PROBE_VALIDATE_11D9BB   = 113, // 0051D9BB - movzx eax,[diff]; cmp eax,05 follows
    PROBE_CMP_11DBFC        = 114, // 0051DBFC - cmp byte ptr [diff],03
    PROBE_CMP_11DCC3        = 115, // 0051DCC3 - cmp byte ptr [diff],02
    PROBE_CMP_11DD20        = 116, // 0051DD20 - cmp byte ptr [diff],03
    PROBE_CMP_11DE91        = 117, // 0051DE91 - cmp byte ptr [diff],03
    PROBE_TABLE_11DF53      = 118, // 0051DF53 - local table 16,12,10,10,8,8, neutral
    PROBE_CMP_11E5F3        = 119, // 0051E5F3 - cmp byte ptr [diff],02
    PROBE_MOV_BL_11EAC4     = 120, // 0051EAC4 - mov bl,[diff]
    PROBE_CMP_11EB66        = 121, // 0051EB66 - cmp byte ptr [diff],02
    PROBE_SWITCH_11EBE1     = 122, // 0051EBE1 - movzx eax,[diff]; jump table follows
};

inline const char* BranchName(BranchId id)
{
    switch (id)
    {
    case BranchId::NONE: return "NONE";
    case BranchId::PROBE_MOV_CL_65F21: return "PROBE_MOV_CL_65F21";
    case BranchId::PROBE_MOV_BL_550C1: return "PROBE_MOV_BL_550C1_TO_11E517";
    case BranchId::PROBE_MOVZX_EAX_59A5FD: return "PROBE_MOVZX_EAX_59A5FD_TO_11D298";
    case BranchId::PROBE_CMP_11C253: return "PROBE_CMP_11C253_DIFF_GT_2_GATE";
    case BranchId::PROBE_CMP_11D3D2: return "PROBE_CMP_11D3D2_DIFF_GT_2_GATE";
    case BranchId::PROBE_CMP_11D646: return "PROBE_CMP_11D646_DIFF_GT_2_GATE";
    case BranchId::PROBE_VALIDATE_11D9BB: return "PROBE_VALIDATE_11D9BB_RANGE_0_5";
    case BranchId::PROBE_CMP_11DBFC: return "PROBE_CMP_11DBFC_DIFF_GE_3_GATE";
    case BranchId::PROBE_CMP_11DCC3: return "PROBE_CMP_11DCC3_DIFF_GE_2_GATE";
    case BranchId::PROBE_CMP_11DD20: return "PROBE_CMP_11DD20_DIFF_LE_3_GATE";
    case BranchId::PROBE_CMP_11DE91: return "PROBE_CMP_11DE91_DIFF_LE_3_GATE";
    case BranchId::PROBE_TABLE_11DF53: return "PROBE_TABLE_11DF53_NEUTRAL";
    case BranchId::PROBE_CMP_11E5F3: return "PROBE_CMP_11E5F3_DIFF_GT_2_GATE";
    case BranchId::PROBE_MOV_BL_11EAC4: return "PROBE_MOV_BL_11EAC4";
    case BranchId::PROBE_CMP_11EB66: return "PROBE_CMP_11EB66_DIFF_GT_2_GATE";
    case BranchId::PROBE_SWITCH_11EBE1: return "PROBE_SWITCH_11EBE1_CL_CLASS";
    default: return "UNKNOWN";
    }
}

inline bool IsProbeBranch(BranchId id)
{
    return id != BranchId::NONE;
}
