#include "AiDifficultyLogger.h"
#include "MemoryReader.h"
#include "PesAddresses.h"
#include "HookStubs.h"

#include <cstdio>

namespace
{
    uint32_t ActorAddr(const PushadRegisters* r)
    {
        return r ? r->esi : 0;
    }

    uint32_t StackAddr(const PushadRegisters* r)
    {
        // Hook stubs do pushfd before pushad. PUSHAD stores ESP after PUSHFD,
        // so the saved value is originalESP - 4. Add 4 to recover the game stack.
        return r ? (r->esp + 4) : 0;
    }

    uint32_t TimerBucket(uint32_t timer)
    {
        return timer / 128;
    }

    uint32_t RegBucket(uint32_t value)
    {
        // Coarse bucket to reduce spam while still surfacing meaningful context changes.
        return value / 16;
    }

    const char* GateHint(BranchId branch)
    {
        switch (branch)
        {
        case BranchId::PROBE_MOV_BL_550C1: return "entry_to_11E517";
        case BranchId::PROBE_MOVZX_EAX_59A5FD: return "entry_to_11D298";
        case BranchId::PROBE_MOV_CL_65F21: return "frequent_global_diff_loader";
        case BranchId::PROBE_CMP_11C253: return "cmp_diff_2_then_ja";
        case BranchId::PROBE_CMP_11D3D2: return "cmp_diff_2_then_jna";
        case BranchId::PROBE_CMP_11D646: return "cmp_diff_2_then_ja";
        case BranchId::PROBE_VALIDATE_11D9BB: return "range_check_0_5";
        case BranchId::PROBE_CMP_11DBFC: return "cmp_diff_3_then_jb";
        case BranchId::PROBE_CMP_11DCC3: return "cmp_diff_2_then_jb";
        case BranchId::PROBE_CMP_11DD20: return "cmp_diff_3_then_ja";
        case BranchId::PROBE_CMP_11DE91: return "cmp_diff_3_then_jbe";
        case BranchId::PROBE_TABLE_11DF53: return "local_table_16_12_10_10_8_8_neutral";
        case BranchId::PROBE_CMP_11E5F3: return "cmp_diff_2_then_ja";
        case BranchId::PROBE_MOV_BL_11EAC4: return "mov_bl_diff_inside_11E_block";
        case BranchId::PROBE_CMP_11EB66: return "cmp_diff_2_then_ja";
        case BranchId::PROBE_SWITCH_11EBE1: return "jump_table_diff_to_CL_class";
        default: return "unknown";
        }
    }
}

AiDifficultyLogger& AiDifficultyLogger::Instance()
{
    static AiDifficultyLogger instance;
    return instance;
}

AiDifficultyLogger::AiDifficultyLogger()
    : initialized_(false)
{
    InitializeCriticalSection(&cs_);
    for (int i = 0; i < 256; ++i)
        branchHits_[i] = 0;

    for (int i = 0; i < kActorStateCacheSize; ++i)
    {
        actorStateCache_[i].actor = 0;
        actorStateCache_[i].state16 = 0xFF;
        actorStateCache_[i].sub18 = 0xFFFF;
        actorStateCache_[i].lastBranch = BranchId::NONE;
        actorStateCache_[i].lastTick = 0;
        actorStateCache_[i].initialized = false;
    }
}

bool AiDifficultyLogger::Initialize()
{
    if (initialized_)
        return true;

    if (!sink_.Open("D:\\pes\\IA\\logs.txt"))
        return false;

    initialized_ = true;
    LogHeader();
    return true;
}

void AiDifficultyLogger::Shutdown()
{
    if (!initialized_)
        return;

    sink_.WriteLine("[AI_DIFFICULTY_RESEARCH] shutdown");
    sink_.Close();
    initialized_ = false;
}

void AiDifficultyLogger::LogHeader()
{
    char line[1600]{};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[AI_DIFFICULTY_RESEARCH] start base=%08X log=D:/pes/IA/logs.txt "
        "version=fix7_11C253_deep_state_logger_no_tweaks tweaks=none "
        "focus=11C253_state_transitions,0F_0004_positive_candidate,550C1_to_11E517,59A5FD_to_11D298,11DBFC,11EB66,11EBE1,secondary_diff_gates",
        static_cast<unsigned>(PesAddresses::Base())
    );
    sink_.WriteLine(line);
}

uint32_t AiDifficultyLogger::IncrementHitCount(BranchId branch)
{
    const uint32_t raw = static_cast<uint32_t>(branch) & 0xFF;
    return ++branchHits_[raw];
}


bool AiDifficultyLogger::IsLikelyActor(uint32_t actor, uint8_t index11, uint8_t team12) const
{
    // Observed player/actor structs live around 0x03BDxxxx in this PES6 build.
    // Avoid treating small integers or stack temporaries as actors (for example
    // 11D3D2 sometimes has ESI=0000027A in the previous logs).
    if (actor < 0x02000000 || actor > 0x08000000)
        return false;

    if (team12 > 1)
        return false;

    // Most match actors have indices 0..21. Keep a small margin for special actors,
    // but reject obviously uninitialized memory.
    if (index11 > 31)
        return false;

    return true;
}

bool AiDifficultyLogger::UpdateActorStateCache(uint32_t actor, uint8_t state16, uint16_t sub18, BranchId branch, ULONGLONG tick,
                                               uint8_t& prevState16, uint16_t& prevSub18, BranchId& prevBranch, ULONGLONG& prevTick)
{
    const int slot = static_cast<int>((actor >> 4) % kActorStateCacheSize);
    ActorStateCache& item = actorStateCache_[slot];

    if (!item.initialized || item.actor != actor)
    {
        item.actor = actor;
        item.state16 = state16;
        item.sub18 = sub18;
        item.lastBranch = branch;
        item.lastTick = tick;
        item.initialized = true;
        prevState16 = 0xFF;
        prevSub18 = 0xFFFF;
        prevBranch = BranchId::NONE;
        prevTick = 0;
        return false;
    }

    const bool changed = (item.state16 != state16) || (item.sub18 != sub18);
    prevState16 = item.state16;
    prevSub18 = item.sub18;
    prevBranch = item.lastBranch;
    prevTick = item.lastTick;

    item.state16 = state16;
    item.sub18 = sub18;
    item.lastBranch = branch;
    item.lastTick = tick;

    return changed;
}

bool AiDifficultyLogger::ShouldLogProbe(uint32_t actor, BranchId branch, uint8_t difficulty, uint8_t state16,
                                        uint16_t sub18, uint8_t actor4A, uint8_t actor48, uint32_t timer114,
                                        uint32_t eaxBucket, uint32_t ebxBucket, uint32_t ecxBucket, uint32_t edxBucket)
{
    const int slot = static_cast<int>(((actor >> 4) + static_cast<uint32_t>(branch) * 13U) % kProbeCacheSize);
    ProbeSampleCache& item = probeCache_[slot];

    const bool changed = item.actor != actor || item.branch != branch || item.difficulty != difficulty ||
                         item.state16 != state16 || item.sub18 != sub18 || item.actor4A != actor4A ||
                         item.actor48 != actor48 || item.timerBucket != TimerBucket(timer114) ||
                         item.eaxBucket != eaxBucket || item.ebxBucket != ebxBucket ||
                         item.ecxBucket != ecxBucket || item.edxBucket != edxBucket;

    item.actor = actor;
    item.branch = branch;
    item.difficulty = difficulty;
    item.state16 = state16;
    item.sub18 = sub18;
    item.actor4A = actor4A;
    item.actor48 = actor48;
    item.timerBucket = TimerBucket(timer114);
    item.eaxBucket = eaxBucket;
    item.ebxBucket = ebxBucket;
    item.ecxBucket = ecxBucket;
    item.edxBucket = edxBucket;
    return changed;
}

void AiDifficultyLogger::LogFromHook(BranchId branch, const PushadRegisters* regs)
{
    if (!initialized_ || regs == nullptr)
        return;

    const uint32_t actor = ActorAddr(regs);
    const uint32_t esp = StackAddr(regs);
    const ULONGLONG tick = GetTickCount64();

    const uint8_t difficulty = MemoryReader::ReadU8(PesAddresses::Abs(PesAddresses::RVA_DIFFICULTY_RUNTIME));

    const uint8_t  actorState16 = MemoryReader::ReadU8(actor + 0x16);
    const uint16_t actorSub18 = MemoryReader::ReadU16(actor + 0x18);
    const uint8_t  actorTeam12 = MemoryReader::ReadU8(actor + 0x12);
    const uint8_t  actorIndex11 = MemoryReader::ReadU8(actor + 0x11);
    const uint8_t  actorFlag24 = MemoryReader::ReadU8(actor + 0x24);
    const uint8_t  actor48 = MemoryReader::ReadU8(actor + 0x48);
    const uint8_t  actor4A = MemoryReader::ReadU8(actor + 0x4A);
    const uint8_t  actor62 = MemoryReader::ReadU8(actor + 0x62);
    const uint32_t actor98 = MemoryReader::ReadU32(actor + 0x98);
    const uint32_t actorTimer114 = MemoryReader::ReadU32(actor + 0x114);
    const uint32_t actor124 = MemoryReader::ReadU32(actor + 0x124);

    const uint32_t stack08 = MemoryReader::ReadU32(esp + 0x08);
    const uint32_t stack0C = MemoryReader::ReadU32(esp + 0x0C);
    const uint32_t stack10 = MemoryReader::ReadU32(esp + 0x10);
    const uint32_t stack14 = MemoryReader::ReadU32(esp + 0x14);
    const uint32_t stack18 = MemoryReader::ReadU32(esp + 0x18);
    const uint32_t stack1C = MemoryReader::ReadU32(esp + 0x1C);
    const uint32_t stack20 = MemoryReader::ReadU32(esp + 0x20);

    const uint32_t eaxBucket = RegBucket(regs->eax);
    const uint32_t ebxBucket = RegBucket(regs->ebx);
    const uint32_t ecxBucket = RegBucket(regs->ecx);
    const uint32_t edxBucket = RegBucket(regs->edx);

    const bool likelyActor = IsLikelyActor(actor, actorIndex11, actorTeam12);
    const bool is11C253 = branch == BranchId::PROBE_CMP_11C253;
    const bool isPositiveCandidate = actorState16 == 0x0F && actorSub18 == 0x0004;
    const bool isInterestingState = actorState16 == 0x0F || actorSub18 == 0x0004 || actorState16 == 0x16 || actorState16 == 0x17;
    const bool isImportantBranch = is11C253 || branch == BranchId::PROBE_MOV_BL_550C1 ||
                                   branch == BranchId::PROBE_MOVZX_EAX_59A5FD ||
                                   branch == BranchId::PROBE_CMP_11DBFC ||
                                   branch == BranchId::PROBE_CMP_11EB66 ||
                                   branch == BranchId::PROBE_SWITCH_11EBE1 ||
                                   branch == BranchId::PROBE_MOV_BL_11EAC4;

    uint32_t hit = 0;
    bool shouldLog = true;
    bool stateChanged = false;
    uint8_t prevState16 = 0xFF;
    uint16_t prevSub18 = 0xFFFF;
    BranchId prevBranch = BranchId::NONE;
    ULONGLONG prevTick = 0;

    EnterCriticalSection(&cs_);
    hit = IncrementHitCount(branch);
    shouldLog = ShouldLogProbe(actor, branch, difficulty, actorState16, actorSub18, actor4A, actor48,
                               actorTimer114, eaxBucket, ebxBucket, ecxBucket, edxBucket);
    if (likelyActor)
    {
        stateChanged = UpdateActorStateCache(actor, actorState16, actorSub18, branch, tick,
                                             prevState16, prevSub18, prevBranch, prevTick);
    }
    LeaveCriticalSection(&cs_);

    const uint16_t globalDiff4Value = MemoryReader::ReadU16(g_Table78BFA8Abs + 4 * 2, 0xFFFF);
    const uint16_t globalDiff5Value = MemoryReader::ReadU16(g_Table78BFA8Abs + 5 * 2, 0xFFFF);

    const uint32_t chosen = (branch == BranchId::PROBE_TABLE_11DF53) ? (regs->eax & 0xFFFF) : 0;
    const uint32_t idx = (branch == BranchId::PROBE_TABLE_11DF53) ? (regs->ecx & 0xFF) : 0;

    if (stateChanged)
    {
        char stateLine[2200]{};
        _snprintf_s(stateLine, sizeof(stateLine), _TRUNCATE,
            "[AI_STATE_CHANGE] tick=%I64u dt=%I64u diff=%u/0x%02X actor=%08X team12=%u index11=%u "
            "from=%u/0x%02X:%u/0x%04X to=%u/0x%02X:%u/0x%04X "
            "branch=%s prev_branch=%s positive_candidate=%u flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u "
            "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
            "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X",
            tick, (prevTick == 0 ? 0 : tick - prevTick), difficulty, difficulty, actor, actorTeam12, actorIndex11,
            prevState16, prevState16, prevSub18, prevSub18,
            actorState16, actorState16, actorSub18, actorSub18,
            BranchName(branch), BranchName(prevBranch), isPositiveCandidate ? 1 : 0,
            actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124,
            regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
            stack08, stack0C, stack10, stack14, stack18, stack1C, stack20);
        sink_.WriteLine(stateLine);
    }

    if (is11C253 || isPositiveCandidate)
    {
        char focusLine[2200]{};
        _snprintf_s(focusLine, sizeof(focusLine), _TRUNCATE,
            "[AI_11C253_FOCUS] hit=%u tick=%I64u branch=%s diff=%u/0x%02X actor=%08X likely_actor=%u team12=%u index11=%u "
            "state16=%u/0x%02X sub18=%u/0x%04X prev_state=%u/0x%02X prev_sub=%u/0x%04X state_changed=%u positive_candidate=%u "
            "flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u "
            "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
            "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X",
            hit, tick, BranchName(branch), difficulty, difficulty, actor, likelyActor ? 1 : 0, actorTeam12, actorIndex11,
            actorState16, actorState16, actorSub18, actorSub18,
            prevState16, prevState16, prevSub18, prevSub18, stateChanged ? 1 : 0, isPositiveCandidate ? 1 : 0,
            actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124,
            regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
            stack08, stack0C, stack10, stack14, stack18, stack1C, stack20);
        sink_.WriteLine(focusLine);
    }

    // Always log first hits, important branches, state transitions and the 0F/0004 candidate.
    // Noisy loader 65F21 is still logged when its context changes, but it is not allowed to drown the semantic lines.
    if (!shouldLog && hit > 3 && !stateChanged && !isPositiveCandidate && !(isImportantBranch && isInterestingState))
        return;

    char line[2600]{};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[AI_PROBE] hit=%u tick=%I64u branch=%s gate=%s diff=%u/0x%02X "
        "actor=%08X likely_actor=%u team12=%u index11=%u state16=%u/0x%02X sub18=%u/0x%04X "
        "prev_state=%u/0x%02X prev_sub=%u/0x%04X state_changed=%u positive_candidate=%u "
        "flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u "
        "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
        "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X "
        "table11DF53_idx=%u table11DF53_chosen=%u/0x%04X global78BFA8_d4=%u d5=%u",
        hit, tick, BranchName(branch), GateHint(branch), difficulty, difficulty,
        actor, likelyActor ? 1 : 0, actorTeam12, actorIndex11, actorState16, actorState16, actorSub18, actorSub18,
        prevState16, prevState16, prevSub18, prevSub18, stateChanged ? 1 : 0, isPositiveCandidate ? 1 : 0,
        actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124,
        regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
        stack08, stack0C, stack10, stack14, stack18, stack1C, stack20,
        idx, chosen, chosen, globalDiff4Value, globalDiff5Value);

    sink_.WriteLine(line);
}

extern "C" void __cdecl AiLog_Branch(uint32_t branchId, PushadRegisters* regs)
{
    AiDifficultyLogger::Instance().LogFromHook(static_cast<BranchId>(branchId), regs);
}
