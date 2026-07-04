#include "AiDifficultyLogger.h"
#include "MemoryReader.h"
#include "PesAddresses.h"
#include "HookStubs.h"
#include "BehaviorContext.h"
#include "ResearchConfig.h"

#include <cstdio>
#include <cmath>

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
        case BranchId::PROBE_POSITIVE_11F172: return "direct_positive_commit_state_0F_0004";
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
        actorStateCache_[i].f48 = 0xFF;
        actorStateCache_[i].f4A = 0xFF;
        actorStateCache_[i].f62 = 0xFF;
        actorStateCache_[i].f98 = 0xFFFFFFFF;
        actorStateCache_[i].timer114 = 0xFFFFFFFF;
        actorStateCache_[i].f124 = 0xFFFFFFFF;
        actorStateCache_[i].stateEnterTick = 0;
        actorStateCache_[i].samplesInState = 0;
        actorStateCache_[i].initialized = false;
        actorStateCache_[i].motionInitialized = false;
        actorStateCache_[i].motionTick = 0;
        actorStateCache_[i].lastActorPosValid = false;
        actorStateCache_[i].lastBallPosValid = false;
        actorStateCache_[i].lastActivePosValid = false;
        actorStateCache_[i].lastPredPosValid = false;
        actorStateCache_[i].lastDistActorBall = -1.0f;
        actorStateCache_[i].lastDistActorActive = -1.0f;
        actorStateCache_[i].lastDistActiveBall = -1.0f;
        actorStateCache_[i].lastDistActorBallPred = -1.0f;
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
        "version=fix9_behavior_blackbox_motion_funnel_logger_no_tweaks tweaks=none "
        "focus=decision_context,state_changes,field_changes,target_resolver,spatial_context,motion_vectors,frames_in_state,11C253,550C1,59A5FD,11DBFC,11EB66,11EBE1 "
        "spatial_actor_offsets_known=%u player_offsets_known=%u ball_pos=20/24/28 actor_pos=D0/D4/D8 alt=E0/E4/E8 active_resolver=global_active_ball_owner_regs_stack motion_log=%u optional_positive_11F172=%u log_reopenable=1 noisy65F21=%u",
        static_cast<unsigned>(PesAddresses::Base()),
        ResearchConfig::ACTOR_POSITION_OFFSETS_KNOWN ? 1 : 0,
        ResearchConfig::PLAYER_POSITION_OFFSETS_KNOWN ? 1 : 0,
        ResearchConfig::kEnableMotionLog ? 1 : 0,
        ResearchConfig::kInstallExperimental11F172PositiveHook ? 1 : 0,
        ResearchConfig::kInstallNoisy65F21Probe ? 1 : 0
    );
    sink_.WriteLine(line);
}

uint32_t AiDifficultyLogger::IncrementHitCount(BranchId branch)
{
    const uint32_t raw = static_cast<uint32_t>(branch) & 0xFF;
    return ++branchHits_[raw];
}

namespace
{
    float Dist2D(float ax, float az, float bx, float bz)
    {
        const float dx = ax - bx;
        const float dz = az - bz;
        return std::sqrt(dx * dx + dz * dz);
    }

    float SpeedFromDelta(float distance, uint32_t dtMs)
    {
        if (dtMs == 0 || distance < 0.0f)
            return -1.0f;
        return distance * 1000.0f / static_cast<float>(dtMs);
    }

    bool IsClosing(float delta)
    {
        return delta <= -ResearchConfig::MOVING_TOWARD_DELTA;
    }
}

void AiDifficultyLogger::UpdateMotionCache(uint32_t actor, ULONGLONG tick, const BehaviorSnapshot& b, MotionMetrics& m)
{
    m = MotionMetrics{};
    if (!ResearchConfig::kEnableMotionLog || actor == 0)
        return;

    const int slot = static_cast<int>((actor >> 4) % kActorStateCacheSize);
    ActorStateCache& item = actorStateCache_[slot];
    if (!item.initialized || item.actor != actor)
        return;

    const bool actorValid = b.actorBestPos.valid;
    const bool ballValid = b.ballPos.valid;
    const bool activeValid = b.activeBestPos.valid;
    const bool predValid = b.ballPredPos.valid;

    if (!item.motionInitialized)
    {
        item.motionInitialized = true;
        item.motionTick = tick;
        item.lastActorX = b.actorBestPos.x;
        item.lastActorZ = b.actorBestPos.z;
        item.lastBallX = b.ballPos.x;
        item.lastBallZ = b.ballPos.z;
        item.lastActiveX = b.activeBestPos.x;
        item.lastActiveZ = b.activeBestPos.z;
        item.lastPredX = b.ballPredPos.x;
        item.lastPredZ = b.ballPredPos.z;
        item.lastActorPosValid = actorValid;
        item.lastBallPosValid = ballValid;
        item.lastActivePosValid = activeValid;
        item.lastPredPosValid = predValid;
        item.lastDistActorBall = b.distActorBall;
        item.lastDistActorActive = b.distActorActive;
        item.lastDistActiveBall = b.distActiveBall;
        item.lastDistActorBallPred = b.distActorBallPred;
        return;
    }

    const ULONGLONG rawDt = tick >= item.motionTick ? (tick - item.motionTick) : 0;
    const uint32_t dtMs = rawDt > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<uint32_t>(rawDt);
    m.valid = dtMs > 0;
    m.dtMs = dtMs;

    if (m.valid && actorValid && item.lastActorPosValid)
        m.actorSpeed = SpeedFromDelta(Dist2D(b.actorBestPos.x, b.actorBestPos.z, item.lastActorX, item.lastActorZ), dtMs);
    if (m.valid && ballValid && item.lastBallPosValid)
        m.ballSpeed = SpeedFromDelta(Dist2D(b.ballPos.x, b.ballPos.z, item.lastBallX, item.lastBallZ), dtMs);
    if (m.valid && activeValid && item.lastActivePosValid)
        m.activeSpeed = SpeedFromDelta(Dist2D(b.activeBestPos.x, b.activeBestPos.z, item.lastActiveX, item.lastActiveZ), dtMs);
    if (m.valid && predValid && item.lastPredPosValid)
        m.predSpeed = SpeedFromDelta(Dist2D(b.ballPredPos.x, b.ballPredPos.z, item.lastPredX, item.lastPredZ), dtMs);

    m.prevDistActorBall = item.lastDistActorBall;
    m.prevDistActorActive = item.lastDistActorActive;
    m.prevDistActiveBall = item.lastDistActiveBall;
    m.prevDistActorBallPred = item.lastDistActorBallPred;

    if (b.distActorBall >= 0.0f && item.lastDistActorBall >= 0.0f)
        m.deltaActorBall = b.distActorBall - item.lastDistActorBall;
    if (b.distActorActive >= 0.0f && item.lastDistActorActive >= 0.0f)
        m.deltaActorActive = b.distActorActive - item.lastDistActorActive;
    if (b.distActiveBall >= 0.0f && item.lastDistActiveBall >= 0.0f)
        m.deltaActiveBall = b.distActiveBall - item.lastDistActiveBall;
    if (b.distActorBallPred >= 0.0f && item.lastDistActorBallPred >= 0.0f)
        m.deltaActorBallPred = b.distActorBallPred - item.lastDistActorBallPred;

    m.actorMovingTowardBall = IsClosing(m.deltaActorBall);
    m.actorMovingTowardActive = IsClosing(m.deltaActorActive);
    m.actorMovingTowardPred = IsClosing(m.deltaActorBallPred);

    item.motionTick = tick;
    item.lastActorX = b.actorBestPos.x;
    item.lastActorZ = b.actorBestPos.z;
    item.lastBallX = b.ballPos.x;
    item.lastBallZ = b.ballPos.z;
    item.lastActiveX = b.activeBestPos.x;
    item.lastActiveZ = b.activeBestPos.z;
    item.lastPredX = b.ballPredPos.x;
    item.lastPredZ = b.ballPredPos.z;
    item.lastActorPosValid = actorValid;
    item.lastBallPosValid = ballValid;
    item.lastActivePosValid = activeValid;
    item.lastPredPosValid = predValid;
    item.lastDistActorBall = b.distActorBall;
    item.lastDistActorActive = b.distActorActive;
    item.lastDistActiveBall = b.distActiveBall;
    item.lastDistActorBallPred = b.distActorBallPred;
}

void AiDifficultyLogger::FormatMotion(char* out, size_t outSize, const MotionMetrics& m)
{
    if (!out || outSize == 0)
        return;
    const char* intent = "UNKNOWN";
    if (m.actorMovingTowardBall && m.actorMovingTowardPred)
        intent = "TOWARD_REAL_AND_PRED_BALL";
    else if (m.actorMovingTowardPred)
        intent = "TOWARD_PRED_BALL";
    else if (m.actorMovingTowardBall)
        intent = "TOWARD_REAL_BALL";
    else if (m.actorMovingTowardActive)
        intent = "TOWARD_ACTIVE";
    else if (m.valid)
        intent = "NO_CLOSING_SIGNAL";

    _snprintf_s(out, outSize, _TRUNCATE,
        "motion_valid=%u motion_dt_ms=%u actor_speed=%.2f ball_speed=%.2f active_speed=%.2f pred_speed=%.2f "
        "prev_dist_actor_ball=%.2f prev_dist_actor_active=%.2f prev_dist_active_ball=%.2f prev_dist_actor_ball_pred=%.2f "
        "delta_actor_ball=%.2f delta_actor_active=%.2f delta_active_ball=%.2f delta_actor_ball_pred=%.2f "
        "moving_to_ball=%u moving_to_active=%u moving_to_pred=%u motion_intent=%s",
        m.valid ? 1 : 0, m.dtMs, m.actorSpeed, m.ballSpeed, m.activeSpeed, m.predSpeed,
        m.prevDistActorBall, m.prevDistActorActive, m.prevDistActiveBall, m.prevDistActorBallPred,
        m.deltaActorBall, m.deltaActorActive, m.deltaActiveBall, m.deltaActorBallPred,
        m.actorMovingTowardBall ? 1 : 0, m.actorMovingTowardActive ? 1 : 0, m.actorMovingTowardPred ? 1 : 0, intent);
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
                                               uint8_t f48, uint8_t f4A, uint8_t f62, uint32_t f98, uint32_t timer114, uint32_t f124,
                                               uint8_t& prevState16, uint16_t& prevSub18, BranchId& prevBranch, ULONGLONG& prevTick,
                                               bool& fieldChanged, uint32_t& samplesInState, ULONGLONG& stateAge)
{
    const int slot = static_cast<int>((actor >> 4) % kActorStateCacheSize);
    ActorStateCache& item = actorStateCache_[slot];
    fieldChanged = false;
    samplesInState = 0;
    stateAge = 0;

    if (!item.initialized || item.actor != actor)
    {
        item.actor = actor;
        item.state16 = state16;
        item.sub18 = sub18;
        item.lastBranch = branch;
        item.lastTick = tick;
        item.f48 = f48;
        item.f4A = f4A;
        item.f62 = f62;
        item.f98 = f98;
        item.timer114 = timer114;
        item.f124 = f124;
        item.stateEnterTick = tick;
        item.samplesInState = 1;
        item.initialized = true;
        item.motionInitialized = false;
        prevState16 = 0xFF;
        prevSub18 = 0xFFFF;
        prevBranch = BranchId::NONE;
        prevTick = 0;
        samplesInState = item.samplesInState;
        return false;
    }

    const bool changed = (item.state16 != state16) || (item.sub18 != sub18);
    fieldChanged = (item.f48 != f48) || (item.f4A != f4A) || (item.f62 != f62) ||
                   (item.f98 != f98) || (item.f124 != f124) ||
                   (TimerBucket(item.timer114) != TimerBucket(timer114));

    prevState16 = item.state16;
    prevSub18 = item.sub18;
    prevBranch = item.lastBranch;
    prevTick = item.lastTick;

    if (changed)
    {
        item.stateEnterTick = tick;
        item.samplesInState = 1;
    }
    else
    {
        ++item.samplesInState;
    }

    item.state16 = state16;
    item.sub18 = sub18;
    item.lastBranch = branch;
    item.lastTick = tick;
    item.f48 = f48;
    item.f4A = f4A;
    item.f62 = f62;
    item.f98 = f98;
    item.timer114 = timer114;
    item.f124 = f124;

    samplesInState = item.samplesInState;
    stateAge = (item.stateEnterTick == 0 ? 0 : tick - item.stateEnterTick);
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
    const bool isDirectPositiveCommit = branch == BranchId::PROBE_POSITIVE_11F172;
    const bool isPositiveCandidate = actorState16 == 0x0F && actorSub18 == 0x0004;
    const bool isInterestingState = actorState16 == 0x0F || actorSub18 == 0x0004 || actorState16 == 0x16 || actorState16 == 0x17;
    const bool isImportantBranch = is11C253 || branch == BranchId::PROBE_MOV_BL_550C1 ||
                                   branch == BranchId::PROBE_MOVZX_EAX_59A5FD ||
                                   branch == BranchId::PROBE_CMP_11DBFC ||
                                   branch == BranchId::PROBE_CMP_11EB66 ||
                                   branch == BranchId::PROBE_SWITCH_11EBE1 ||
                                   branch == BranchId::PROBE_MOV_BL_11EAC4 ||
                                   branch == BranchId::PROBE_POSITIVE_11F172;

    uint32_t hit = 0;
    bool shouldLog = true;
    bool stateChanged = false;
    uint8_t prevState16 = 0xFF;
    uint16_t prevSub18 = 0xFFFF;
    BranchId prevBranch = BranchId::NONE;
    ULONGLONG prevTick = 0;
    bool fieldChanged = false;
    uint32_t samplesInState = 0;
    ULONGLONG stateAge = 0;

    EnterCriticalSection(&cs_);
    hit = IncrementHitCount(branch);
    shouldLog = ShouldLogProbe(actor, branch, difficulty, actorState16, actorSub18, actor4A, actor48,
                               actorTimer114, eaxBucket, ebxBucket, ecxBucket, edxBucket);
    if (likelyActor)
    {
        stateChanged = UpdateActorStateCache(actor, actorState16, actorSub18, branch, tick,
                                             actor48, actor4A, actor62, actor98, actorTimer114, actor124,
                                             prevState16, prevSub18, prevBranch, prevTick,
                                             fieldChanged, samplesInState, stateAge);
    }
    LeaveCriticalSection(&cs_);

    const uint16_t globalDiff4Value = MemoryReader::ReadU16(g_Table78BFA8Abs + 4 * 2, 0xFFFF);
    const uint16_t globalDiff5Value = MemoryReader::ReadU16(g_Table78BFA8Abs + 5 * 2, 0xFFFF);

    const uint32_t chosen = (branch == BranchId::PROBE_TABLE_11DF53) ? (regs->eax & 0xFFFF) : 0;
    const uint32_t idx = (branch == BranchId::PROBE_TABLE_11DF53) ? (regs->ecx & 0xFF) : 0;

    const BehaviorSnapshot behavior = BehaviorContext::Capture(actor, regs, esp, actorTeam12, actorIndex11);
    MotionMetrics motion{};
    EnterCriticalSection(&cs_);
    if (likelyActor)
        UpdateMotionCache(actor, tick, behavior, motion);
    LeaveCriticalSection(&cs_);

    char spatialPart[3600]{};
    char targetPart[1600]{};
    char motionPart[1800]{};
    BehaviorContext::FormatSpatial(spatialPart, sizeof(spatialPart), behavior);
    BehaviorContext::FormatTargets(targetPart, sizeof(targetPart), behavior);
    FormatMotion(motionPart, sizeof(motionPart), motion);

    if (stateChanged)
    {
        char stateLine[10000]{};
        _snprintf_s(stateLine, sizeof(stateLine), _TRUNCATE,
            "[AI_STATE_CHANGE] tick=%I64u dt=%I64u diff=%u/0x%02X actor=%08X team12=%u index11=%u "
            "from=%u/0x%02X:%u/0x%04X to=%u/0x%02X:%u/0x%04X "
            "branch=%s prev_branch=%s positive_candidate=%u samples_in_state=%u state_age_ms=%I64u flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u "
            "%s %s %s "
            "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
            "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X",
            tick, (prevTick == 0 ? 0 : tick - prevTick), difficulty, difficulty, actor, actorTeam12, actorIndex11,
            prevState16, prevState16, prevSub18, prevSub18,
            actorState16, actorState16, actorSub18, actorSub18,
            BranchName(branch), BranchName(prevBranch), isPositiveCandidate ? 1 : 0, samplesInState, stateAge,
            actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124, spatialPart, targetPart, motionPart,
            regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
            stack08, stack0C, stack10, stack14, stack18, stack1C, stack20);
        sink_.WriteLine(stateLine);
    }

    if (ResearchConfig::kEnableFieldChangeLog && fieldChanged && (isImportantBranch || stateChanged || actorState16 == 0x0F || actorState16 == 0x16 || actorState16 == 0x17))
    {
        char fieldLine[9000]{};
        _snprintf_s(fieldLine, sizeof(fieldLine), _TRUNCATE,
            "[AI_FIELD_CHANGE] tick=%I64u diff=%u/0x%02X actor=%08X team12=%u index11=%u "
            "state=%u/0x%02X:%u/0x%04X branch=%s prev_branch=%s "
            "f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u samples_in_state=%u state_age_ms=%I64u %s %s %s",
            tick, difficulty, difficulty, actor, actorTeam12, actorIndex11,
            actorState16, actorState16, actorSub18, actorSub18,
            BranchName(branch), BranchName(prevBranch),
            actor48, actor4A, actor62, actorTimer114, actor98, actor124,
            samplesInState, stateAge, spatialPart, targetPart, motionPart);
        sink_.WriteLine(fieldLine);
    }

    if (ResearchConfig::kEnableDecisionLog && (stateChanged || isPositiveCandidate || is11C253 || isDirectPositiveCommit))
    {
        char decisionLine[10000]{};
        _snprintf_s(decisionLine, sizeof(decisionLine), _TRUNCATE,
            "[AI_DECISION] tick=%I64u diff=%u/0x%02X branch=%s gate=%s actor=%08X likely_actor=%u team12=%u index11=%u "
            "from=%u/0x%02X:%u/0x%04X to=%u/0x%02X:%u/0x%04X state_changed=%u field_changed=%u positive_candidate=%u "
            "samples_in_state=%u state_age_ms=%I64u flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u "
            "%s %s %s "
            "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X",
            tick, difficulty, difficulty, BranchName(branch), GateHint(branch), actor, likelyActor ? 1 : 0, actorTeam12, actorIndex11,
            prevState16, prevState16, prevSub18, prevSub18,
            actorState16, actorState16, actorSub18, actorSub18,
            stateChanged ? 1 : 0, fieldChanged ? 1 : 0, isPositiveCandidate ? 1 : 0,
            samplesInState, stateAge, actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124,
            spatialPart, targetPart, motionPart,
            regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp);
        sink_.WriteLine(decisionLine);
    }

    if (isPositiveCandidate)
    {
        char commitLine[10000]{};
        _snprintf_s(commitLine, sizeof(commitLine), _TRUNCATE,
            "[AI_COMMIT_POSITIVE] tick=%I64u diff=%u/0x%02X branch=%s actor=%08X team12=%u index11=%u "
            "from=%u/0x%02X:%u/0x%04X to=%u/0x%02X:%u/0x%04X state_changed=%u "
            "samples_in_state=%u state_age_ms=%I64u %s %s %s",
            tick, difficulty, difficulty, BranchName(branch), actor, actorTeam12, actorIndex11,
            prevState16, prevState16, prevSub18, prevSub18,
            actorState16, actorState16, actorSub18, actorSub18, stateChanged ? 1 : 0,
            samplesInState, stateAge, spatialPart, targetPart, motionPart);
        sink_.WriteLine(commitLine);
    }

    if (ResearchConfig::kEnableFocusLog && (is11C253 || isPositiveCandidate || isDirectPositiveCommit))
    {
        char focusLine[10000]{};
        _snprintf_s(focusLine, sizeof(focusLine), _TRUNCATE,
            "[AI_11C253_FOCUS] hit=%u tick=%I64u branch=%s diff=%u/0x%02X actor=%08X likely_actor=%u team12=%u index11=%u "
            "state16=%u/0x%02X sub18=%u/0x%04X prev_state=%u/0x%02X prev_sub=%u/0x%04X state_changed=%u positive_candidate=%u "
            "flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u samples_in_state=%u state_age_ms=%I64u "
            "%s %s %s "
            "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
            "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X",
            hit, tick, BranchName(branch), difficulty, difficulty, actor, likelyActor ? 1 : 0, actorTeam12, actorIndex11,
            actorState16, actorState16, actorSub18, actorSub18,
            prevState16, prevState16, prevSub18, prevSub18, stateChanged ? 1 : 0, isPositiveCandidate ? 1 : 0,
            actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124, samplesInState, stateAge, spatialPart, targetPart, motionPart,
            regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
            stack08, stack0C, stack10, stack14, stack18, stack1C, stack20);
        sink_.WriteLine(focusLine);
    }

    // Always log first hits, important branches, state transitions and the 0F/0004 candidate.
    // Noisy loader 65F21 is still logged when its context changes, but it is not allowed to drown the semantic lines.
    if (!ResearchConfig::kEnableProbeLog)
        return;

    if (!shouldLog && hit > ResearchConfig::PROBE_FIRST_HITS_ALWAYS && !stateChanged && !isPositiveCandidate && !(isImportantBranch && isInterestingState))
        return;

    char line[10000]{};
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "[AI_PROBE] hit=%u tick=%I64u branch=%s gate=%s diff=%u/0x%02X "
        "actor=%08X likely_actor=%u team12=%u index11=%u state16=%u/0x%02X sub18=%u/0x%04X "
        "prev_state=%u/0x%02X prev_sub=%u/0x%04X state_changed=%u positive_candidate=%u "
        "flag24=%u f48=%u f4A=%u f62=%u timer114=%u f98=%u f124=%u samples_in_state=%u state_age_ms=%I64u "
        "%s %s %s "
        "regs eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X "
        "stack08=%08X stack0C=%08X stack10=%08X stack14=%08X stack18=%08X stack1C=%08X stack20=%08X "
        "table11DF53_idx=%u table11DF53_chosen=%u/0x%04X global78BFA8_d4=%u d5=%u",
        hit, tick, BranchName(branch), GateHint(branch), difficulty, difficulty,
        actor, likelyActor ? 1 : 0, actorTeam12, actorIndex11, actorState16, actorState16, actorSub18, actorSub18,
        prevState16, prevState16, prevSub18, prevSub18, stateChanged ? 1 : 0, isPositiveCandidate ? 1 : 0,
        actorFlag24, actor48, actor4A, actor62, actorTimer114, actor98, actor124, samplesInState, stateAge, spatialPart, targetPart, motionPart,
        regs->eax, regs->ebx, regs->ecx, regs->edx, regs->esi, regs->edi, regs->ebp, esp,
        stack08, stack0C, stack10, stack14, stack18, stack1C, stack20,
        idx, chosen, chosen, globalDiff4Value, globalDiff5Value);

    sink_.WriteLine(line);
}

extern "C" void __cdecl AiLog_Branch(uint32_t branchId, PushadRegisters* regs)
{
    AiDifficultyLogger::Instance().LogFromHook(static_cast<BranchId>(branchId), regs);
}
