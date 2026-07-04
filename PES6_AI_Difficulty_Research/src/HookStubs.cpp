#include "HookStubs.h"
#include "AiDifficultyLogger.h"

uintptr_t g_DifficultyRuntimeAbs = 0;
uintptr_t g_Table78BFA8Abs = 0;

uintptr_t g_Return_MOV_CL_65F21 = 0;
uintptr_t g_Return_MOV_BL_550C1 = 0;
uintptr_t g_Return_MOVZX_EAX_59A5FD = 0;

uintptr_t g_Return_CMP_11C253 = 0;
uintptr_t g_Return_CMP_11D3D2 = 0;
uintptr_t g_Return_CMP_11D646 = 0;
uintptr_t g_Return_VALIDATE_11D9BB = 0;
uintptr_t g_Return_CMP_11DBFC = 0;
uintptr_t g_Return_CMP_11DCC3 = 0;
uintptr_t g_Return_CMP_11DD20 = 0;
uintptr_t g_Return_CMP_11DE91 = 0;
uintptr_t g_Return_TABLE_11DF53 = 0;
uintptr_t g_Return_CMP_11E5F3 = 0;
uintptr_t g_Return_MOV_BL_11EAC4 = 0;
uintptr_t g_Return_CMP_11EB66 = 0;
uintptr_t g_Return_SWITCH_11EBE1 = 0;
uintptr_t g_Return_POSITIVE_11F172 = 0;

// IMPORTANT:
// MSVC x86 inline asm can be fragile when macros expand __asm blocks inside
// __declspec(naked) functions. Every hook below is intentionally written as
// one explicit asm block. This avoids parser errors like "push/mov/add is a
// reserved word" and keeps the naked stubs predictable.
//
// Logging convention used by every probe:
//   - Execute the original instruction(s) first.
//   - For CMP hooks, PUSHFD is placed immediately after the CMP so the game's
//     conditional jump receives the original CMP flags after POPFD.
//   - PUSHAD captures the post-original register context for AiLog_Branch.
//   - AiLog_Branch is __cdecl, so the stub cleans 8 bytes from the stack.

extern "C" __declspec(naked) void Hook_MOV_CL_65F21_Probe()
{
    __asm {
        // Original: 00465F21 - mov cl,[PES6.exe+37E094C]
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        mov cl, byte ptr [eax]
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 101
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_MOV_CL_65F21]
    }
}

extern "C" __declspec(naked) void Hook_MOV_BL_550C1_Probe()
{
    __asm {
        // Original: 004550C1 - mov bl,[PES6.exe+37E094C]
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        mov bl, byte ptr [eax]
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 102
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_MOV_BL_550C1]
    }
}

extern "C" __declspec(naked) void Hook_MOVZX_EAX_59A5FD_Probe()
{
    __asm {
        // Original: 0059A5FD - movzx eax,byte ptr [PES6.exe+37E094C]
        push ecx
        mov ecx, dword ptr [g_DifficultyRuntimeAbs]
        movzx eax, byte ptr [ecx]
        pop ecx

        pushfd
        pushad
        mov eax, esp
        push eax
        push 103
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_MOVZX_EAX_59A5FD]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11C253_Probe()
{
    __asm {
        // Original: 0051C253 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 110
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11C253]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11D3D2_Probe()
{
    __asm {
        // Original: 0051D3D2 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 111
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11D3D2]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11D646_Probe()
{
    __asm {
        // Original: 0051D646 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 112
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11D646]
    }
}

extern "C" __declspec(naked) void Hook_VALIDATE_11D9BB_Probe()
{
    __asm {
        // Original: 0051D9BB - movzx eax,byte ptr [PES6.exe+37E094C]
        push ecx
        mov ecx, dword ptr [g_DifficultyRuntimeAbs]
        movzx eax, byte ptr [ecx]
        pop ecx

        pushfd
        pushad
        mov eax, esp
        push eax
        push 113
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_VALIDATE_11D9BB]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11DBFC_Probe()
{
    __asm {
        // Original: 0051DBFC - cmp byte ptr [PES6.exe+37E094C],03
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 3
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 114
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11DBFC]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11DCC3_Probe()
{
    __asm {
        // Original: 0051DCC3 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 115
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11DCC3]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11DD20_Probe()
{
    __asm {
        // Original: 0051DD20 - cmp byte ptr [PES6.exe+37E094C],03
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 3
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 116
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11DD20]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11DE91_Probe()
{
    __asm {
        // Original: 0051DE91 - cmp byte ptr [PES6.exe+37E094C],03
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 3
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 117
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11DE91]
    }
}

extern "C" __declspec(naked) void Hook_TABLE_11DF53_Probe()
{
    __asm {
        // Original replaced bytes:
        // 0051DF53 - movzx ecx,byte ptr [PES6.exe+37E094C]
        // 0051DF5A - movzx eax,byte ptr [esp+ecx+0C]
        push edx
        mov edx, dword ptr [g_DifficultyRuntimeAbs]
        movzx ecx, byte ptr [edx]
        pop edx
        movzx eax, byte ptr [esp+ecx+0Ch]

        pushfd
        pushad
        mov eax, esp
        push eax
        push 118
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_TABLE_11DF53]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11E5F3_Probe()
{
    __asm {
        // Original: 0051E5F3 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 119
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11E5F3]
    }
}

extern "C" __declspec(naked) void Hook_MOV_BL_11EAC4_Probe()
{
    __asm {
        // Original: 0051EAC4 - mov bl,[PES6.exe+37E094C]
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        mov bl, byte ptr [eax]
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 120
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_MOV_BL_11EAC4]
    }
}

extern "C" __declspec(naked) void Hook_CMP_11EB66_Probe()
{
    __asm {
        // Original: 0051EB66 - cmp byte ptr [PES6.exe+37E094C],02
        push eax
        mov eax, dword ptr [g_DifficultyRuntimeAbs]
        cmp byte ptr [eax], 2
        pop eax

        pushfd
        pushad
        mov eax, esp
        push eax
        push 121
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_CMP_11EB66]
    }
}

extern "C" __declspec(naked) void Hook_SWITCH_11EBE1_Probe()
{
    __asm {
        // Original: 0051EBE1 - movzx eax,byte ptr [PES6.exe+37E094C]
        push ecx
        mov ecx, dword ptr [g_DifficultyRuntimeAbs]
        movzx eax, byte ptr [ecx]
        pop ecx

        pushfd
        pushad
        mov eax, esp
        push eax
        push 122
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_SWITCH_11EBE1]
    }
}


extern "C" __declspec(naked) void Hook_POSITIVE_11F172_Probe()
{
    __asm {
        // Original documented positive commit sequence:
        // 0051F172 - mov byte ptr [esi+16],0F
        // 0051F176 - mov word ptr [esi+18],0004
        // Return target: 0051F17C, where the routine continues to return positive.
        mov byte ptr [esi+16h], 0Fh
        mov word ptr [esi+18h], 0004h

        pushfd
        pushad
        mov eax, esp
        push eax
        push 130
        call AiLog_Branch
        add esp, 8
        popad
        popfd

        jmp dword ptr [g_Return_POSITIVE_11F172]
    }
}
