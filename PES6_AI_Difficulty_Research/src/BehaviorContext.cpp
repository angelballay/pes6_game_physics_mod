#include "BehaviorContext.h"
#include "MemoryReader.h"
#include "PesAddresses.h"
#include "ResearchConfig.h"

#include <cstdio>
#include <cmath>
#include <cstring>

namespace
{
    bool SamePtr(uint32_t a, uint32_t b)
    {
        return a != 0 && a == b;
    }

    bool IsPlayerLikeAddress(uint32_t ptr)
    {
        return ptr >= 0x01000000 && ptr <= 0x30000000;
    }

}

bool BehaviorContext::IsReasonableFloat(float v)
{
    if (!std::isfinite(v))
        return false;
    if (v < -1000000.0f || v > 1000000.0f)
        return false;
    return true;
}

bool BehaviorContext::IsReasonableVec3(const Vec3f& v)
{
    if (!v.valid)
        return false;

    // Reject the common all-zero case. In PES coordinates the active actors/ball normally
    // do not sit at exactly 0,0,0 during a live match. This keeps bad pointers from looking valid.
    if (std::fabs(v.x) < 0.0001f && std::fabs(v.y) < 0.0001f && std::fabs(v.z) < 0.0001f)
        return false;

    return true;
}

Vec3f BehaviorContext::ReadVec3(uint32_t base, uintptr_t xOff, uintptr_t yOff, uintptr_t zOff, bool enabled)
{
    Vec3f v{};
    if (!enabled || base == 0)
        return v;

    v.x = MemoryReader::ReadF32(base + xOff, 0.0f);
    v.y = MemoryReader::ReadF32(base + yOff, 0.0f);
    v.z = MemoryReader::ReadF32(base + zOff, 0.0f);
    v.valid = IsReasonableFloat(v.x) && IsReasonableFloat(v.y) && IsReasonableFloat(v.z);
    return v;
}

uint16_t BehaviorContext::ReadAnim30(uint32_t playerOrActor)
{
    if (playerOrActor == 0)
        return 0xFFFF;

    const uint32_t animPtr = MemoryReader::ReadU32(playerOrActor + ResearchConfig::PLAYER_ANIM_PTR, 0);
    if (animPtr == 0)
        return 0xFFFF;

    return MemoryReader::ReadU16(animPtr + ResearchConfig::ANIM30_OFFSET, 0xFFFF);
}


bool BehaviorContext::MatchesBallActorId(const ActorMiniSnapshot& mini, uint8_t ballActorId)
{
    if (!mini.likelyActor || ballActorId == 0xFF)
        return false;

    // 37E09CC has been observed as a global actor/player id. Depending on context,
    // it can match player+0 directly, local index, or team-adjusted local index.
    if (mini.id00 == ballActorId)
        return true;
    if (mini.index11 == ballActorId)
        return true;
    if (mini.team12 == 0 && static_cast<uint8_t>(mini.index11 + 1) == ballActorId)
        return true;
    if (mini.team12 == 1 && static_cast<uint8_t>(mini.index11 + 11) == ballActorId)
        return true;
    if (mini.team12 == 1 && static_cast<uint8_t>(mini.index11 + 12) == ballActorId)
        return true;
    return false;
}

void BehaviorContext::ConsiderCandidate(uint32_t value, const char* source, uint8_t ballActorId, uint32_t actor,
                                        uint32_t& bestOwner, char* bestOwnerSource, size_t bestOwnerSourceSize,
                                        uint32_t& firstActorCandidate, char* firstActorSource, size_t firstActorSourceSize)
{
    if (value == 0 || source == nullptr)
        return;

    ActorMiniSnapshot mini = ReadActorMini(value);
    if (!mini.likelyActor)
        return;

    if (firstActorCandidate == 0 && value != actor)
    {
        firstActorCandidate = value;
        _snprintf_s(firstActorSource, firstActorSourceSize, _TRUNCATE, "%s", source);
    }

    if (bestOwner == 0 && MatchesBallActorId(mini, ballActorId))
    {
        bestOwner = value;
        _snprintf_s(bestOwnerSource, bestOwnerSourceSize, _TRUNCATE, "%s", source);
    }
}

uint32_t BehaviorContext::ResolveBallOwnerPlayer(const PushadRegisters* regs, uint32_t gameEsp, uint32_t actor,
                                                 uint8_t ballActorId, char* sourceOut, size_t sourceOutSize)
{
    if (sourceOut && sourceOutSize)
        sourceOut[0] = '\0';

    uint32_t bestOwner = 0;
    uint32_t firstActorCandidate = 0;
    char bestOwnerSource[64]{};
    char firstActorSource[64]{};

    // Start with current actor. In many AI hooks, ESI is exactly the actor being evaluated.
    ConsiderCandidate(actor, "actor_esi", ballActorId, 0, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));

    const uint32_t globalActive = MemoryReader::ReadU32(PesAddresses::Abs(ResearchConfig::RVA_ACTIVE_PLAYER_PTR), 0);
    ConsiderCandidate(globalActive, "global_active", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));

    if (regs)
    {
        ConsiderCandidate(regs->eax, "reg_eax", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ebx, "reg_ebx", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ecx, "reg_ecx", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->edx, "reg_edx", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->esi, "reg_esi", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->edi, "reg_edi", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ebp, "reg_ebp", ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
    }

    const uint32_t stackOffsets[] = { 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C };
    const char* stackNames[] = { "st08", "st0C", "st10", "st14", "st18", "st1C", "st20", "st24", "st28", "st2C" };
    for (int i = 0; i < 10; ++i)
    {
        const uint32_t v = MemoryReader::ReadU32(gameEsp + stackOffsets[i], 0);
        ConsiderCandidate(v, stackNames[i], ballActorId, actor, bestOwner, bestOwnerSource, sizeof(bestOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
    }

    if (bestOwner != 0)
    {
        if (sourceOut && sourceOutSize)
            _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "%s", bestOwnerSource);
        return bestOwner;
    }

    if (sourceOut && sourceOutSize)
        _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "not_found");
    return 0;
}

uint32_t BehaviorContext::ResolveActivePlayer(const PushadRegisters* regs, uint32_t gameEsp, uint32_t actor,
                                              uint8_t ballActorId, uint32_t globalActive, uint32_t ballOwner,
                                              char* sourceOut, size_t sourceOutSize)
{
    if (sourceOut && sourceOutSize)
        sourceOut[0] = '\0';

    ActorMiniSnapshot g = ReadActorMini(globalActive);
    if (g.likelyActor && (IsReasonableVec3(g.pos) || IsReasonableVec3(g.altPos)))
    {
        if (sourceOut && sourceOutSize)
            _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "global_active");
        return globalActive;
    }

    // If global active is stale/invalid in this AI context, use the current ball owner when known.
    ActorMiniSnapshot owner = ReadActorMini(ballOwner);
    if (owner.likelyActor && (IsReasonableVec3(owner.pos) || IsReasonableVec3(owner.altPos)))
    {
        if (sourceOut && sourceOutSize)
            _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "ball_owner");
        return ballOwner;
    }

    // Last-resort target candidate from registers/stack. This is not guaranteed to be the human cursor,
    // but it is useful to compute a distance to the routine's visible actor target.
    uint32_t ignoredOwner = 0;
    uint32_t firstActorCandidate = 0;
    char ignoredOwnerSource[64]{};
    char firstActorSource[64]{};

    if (regs)
    {
        ConsiderCandidate(regs->eax, "reg_eax_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ebx, "reg_ebx_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ecx, "reg_ecx_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->edx, "reg_edx_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->edi, "reg_edi_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
        ConsiderCandidate(regs->ebp, "reg_ebp_target", ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
    }

    const uint32_t stackOffsets[] = { 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C };
    const char* stackNames[] = { "st08_target", "st0C_target", "st10_target", "st14_target", "st18_target", "st1C_target", "st20_target", "st24_target", "st28_target", "st2C_target" };
    for (int i = 0; i < 10; ++i)
    {
        const uint32_t v = MemoryReader::ReadU32(gameEsp + stackOffsets[i], 0);
        ConsiderCandidate(v, stackNames[i], ballActorId, actor, ignoredOwner, ignoredOwnerSource, sizeof(ignoredOwnerSource), firstActorCandidate, firstActorSource, sizeof(firstActorSource));
    }

    if (firstActorCandidate != 0)
    {
        if (sourceOut && sourceOutSize)
            _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "%s", firstActorSource);
        return firstActorCandidate;
    }

    if (g.likelyActor)
    {
        if (sourceOut && sourceOutSize)
            _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "global_active_no_pos");
        return globalActive;
    }

    if (sourceOut && sourceOutSize)
        _snprintf_s(sourceOut, sourceOutSize, _TRUNCATE, "not_found");
    return 0;
}

float BehaviorContext::DistanceXZ(const Vec3f& a, const Vec3f& b)
{
    if (!a.valid || !b.valid)
        return -1.0f;
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

bool BehaviorContext::IsLikelyActorPointer(uint32_t ptr)
{
    if (!IsPlayerLikeAddress(ptr))
        return false;

    const uint8_t team = MemoryReader::ReadU8(ptr + 0x12, 0xFF);
    const uint8_t idx  = MemoryReader::ReadU8(ptr + 0x11, 0xFF);
    const uint8_t id00 = MemoryReader::ReadU8(ptr + ResearchConfig::PLAYER_ID_OFFSET, 0xFF);
    if (team > 1 || idx > 31)
        return false;
    if (id00 == 0xFF || id00 > 31)
        return false;

    return true;
}

ActorMiniSnapshot BehaviorContext::ReadActorMini(uint32_t ptr)
{
    ActorMiniSnapshot s{};
    s.ptr = ptr;
    s.likelyActor = IsLikelyActorPointer(ptr);
    if (!s.likelyActor)
        return s;

    s.id00 = MemoryReader::ReadU8(ptr + ResearchConfig::PLAYER_ID_OFFSET, 0xFF);
    s.team12 = MemoryReader::ReadU8(ptr + 0x12, 0xFF);
    s.index11 = MemoryReader::ReadU8(ptr + 0x11, 0xFF);
    s.state16 = MemoryReader::ReadU8(ptr + ResearchConfig::PLAYER_STATE16, 0xFF);
    s.sub18 = MemoryReader::ReadU16(ptr + ResearchConfig::PLAYER_SUB18, 0xFFFF);
    s.b0 = MemoryReader::ReadU32(ptr + ResearchConfig::PLAYER_B0_OFFSET, 0);
    s.dirBits = static_cast<uint8_t>(s.b0 & 0xF0);
    s.modeBits = s.b0 & ~0xF0U;
    s.p114 = MemoryReader::ReadU32(ptr + ResearchConfig::PLAYER_P114_OFFSET, 0);
    s.anim30 = ReadAnim30(ptr);
    s.pos = ReadVec3(ptr,
                     ResearchConfig::ACTOR_POS_X_OFFSET,
                     ResearchConfig::ACTOR_POS_Y_OFFSET,
                     ResearchConfig::ACTOR_POS_Z_OFFSET,
                     ResearchConfig::kEnableSpatialLog && ResearchConfig::ACTOR_POSITION_OFFSETS_KNOWN);
    s.altPos = ReadVec3(ptr,
                        ResearchConfig::ACTOR_ALT_POS_X_OFFSET,
                        ResearchConfig::ACTOR_ALT_POS_Y_OFFSET,
                        ResearchConfig::ACTOR_ALT_POS_Z_OFFSET,
                        ResearchConfig::kEnableSpatialLog && ResearchConfig::ACTOR_POSITION_OFFSETS_KNOWN);

    if (!IsReasonableVec3(s.pos) && IsReasonableVec3(s.altPos))
        s.pos = s.altPos;

    return s;
}

void BehaviorContext::Append(char* out, size_t outSize, const char* text)
{
    if (!out || outSize == 0 || !text)
        return;
    const size_t used = std::strlen(out);
    if (used + 1 >= outSize)
        return;
    _snprintf_s(out + used, outSize - used, _TRUNCATE, "%s", text);
}

void BehaviorContext::ResolvePointer(char* out, size_t outSize, const char* label, uint32_t value, const BehaviorSnapshot& snap)
{
    if (!out || outSize == 0 || !label || value == 0)
        return;

    char part[320]{};
    if (SamePtr(value, snap.actor))
    {
        _snprintf_s(part, sizeof(part), _TRUNCATE, "%s=self:%08X;", label, value);
        Append(out, outSize, part);
        return;
    }
    if (SamePtr(value, snap.activePlayer))
    {
        _snprintf_s(part, sizeof(part), _TRUNCATE, "%s=active:%08X;", label, value);
        Append(out, outSize, part);
        return;
    }
    if (SamePtr(value, snap.ball))
    {
        _snprintf_s(part, sizeof(part), _TRUNCATE, "%s=ball:%08X;", label, value);
        Append(out, outSize, part);
        return;
    }

    ActorMiniSnapshot mini = ReadActorMini(value);
    if (mini.likelyActor)
    {
        const float dist = (snap.actorBestPos.valid && mini.pos.valid) ? DistanceXZ(snap.actorBestPos, mini.pos) : -1.0f;
        _snprintf_s(part, sizeof(part), _TRUNCATE,
            "%s=actor:%08X:id%u:t%u:i%u:s%02X:%04X:b0=%08X:dir=%02X:p114=%u:anim=%04X:d=%.1f;",
            label, value, mini.id00, mini.team12, mini.index11, mini.state16, mini.sub18,
            mini.b0, mini.dirBits, mini.p114, mini.anim30, dist);
        Append(out, outSize, part);
    }
}

BehaviorSnapshot BehaviorContext::Capture(uint32_t actor, const PushadRegisters* regs, uint32_t gameEsp,
                                          uint8_t actorTeam12, uint8_t actorIndex11)
{
    BehaviorSnapshot snap{};
    snap.actor = actor;
    const uint32_t globalActive = MemoryReader::ReadU32(PesAddresses::Abs(ResearchConfig::RVA_ACTIVE_PLAYER_PTR), 0);
    snap.ball = MemoryReader::ReadU32(PesAddresses::Abs(ResearchConfig::RVA_BALL_PTR), 0);
    snap.ballActorId = MemoryReader::ReadU8(PesAddresses::Abs(ResearchConfig::RVA_BALL_ACTOR_ID), 0xFF);

    snap.ballOwnerPlayer = ResolveBallOwnerPlayer(regs, gameEsp, actor, snap.ballActorId, snap.ballOwnerSource, sizeof(snap.ballOwnerSource));
    snap.ballOwnerLooksValid = IsLikelyActorPointer(snap.ballOwnerPlayer);
    snap.activePlayer = ResolveActivePlayer(regs, gameEsp, actor, snap.ballActorId, globalActive, snap.ballOwnerPlayer, snap.activeSource, sizeof(snap.activeSource));
    snap.activeLooksValid = IsLikelyActorPointer(snap.activePlayer);

    snap.actorId00 = MemoryReader::ReadU8(actor + ResearchConfig::PLAYER_ID_OFFSET, 0xFF);
    snap.activeId00 = MemoryReader::ReadU8(snap.activePlayer + ResearchConfig::PLAYER_ID_OFFSET, 0xFF);
    snap.activeTeam12 = MemoryReader::ReadU8(snap.activePlayer + 0x12, 0xFF);
    snap.activeIndex11 = MemoryReader::ReadU8(snap.activePlayer + 0x11, 0xFF);

    snap.actorB0 = MemoryReader::ReadU32(actor + ResearchConfig::PLAYER_B0_OFFSET, 0);
    snap.activeB0 = MemoryReader::ReadU32(snap.activePlayer + ResearchConfig::PLAYER_B0_OFFSET, 0);
    snap.actorDirBits = static_cast<uint8_t>(snap.actorB0 & 0xF0);
    snap.activeDirBits = static_cast<uint8_t>(snap.activeB0 & 0xF0);
    snap.actorModeBits = snap.actorB0 & ~0xF0U;
    snap.activeModeBits = snap.activeB0 & ~0xF0U;
    snap.actorAnim30 = ReadAnim30(actor);
    snap.activeAnim30 = ReadAnim30(snap.activePlayer);
    snap.actorP114 = MemoryReader::ReadU32(actor + ResearchConfig::PLAYER_P114_OFFSET, 0);
    snap.activeP114 = MemoryReader::ReadU32(snap.activePlayer + ResearchConfig::PLAYER_P114_OFFSET, 0);

    snap.actorPos = ReadVec3(actor,
                             ResearchConfig::ACTOR_POS_X_OFFSET,
                             ResearchConfig::ACTOR_POS_Y_OFFSET,
                             ResearchConfig::ACTOR_POS_Z_OFFSET,
                             ResearchConfig::kEnableSpatialLog && ResearchConfig::ACTOR_POSITION_OFFSETS_KNOWN);

    snap.actorAltPos = ReadVec3(actor,
                                ResearchConfig::ACTOR_ALT_POS_X_OFFSET,
                                ResearchConfig::ACTOR_ALT_POS_Y_OFFSET,
                                ResearchConfig::ACTOR_ALT_POS_Z_OFFSET,
                                ResearchConfig::kEnableSpatialLog && ResearchConfig::ACTOR_POSITION_OFFSETS_KNOWN);

    snap.actorBestPos = IsReasonableVec3(snap.actorPos) ? snap.actorPos : (IsReasonableVec3(snap.actorAltPos) ? snap.actorAltPos : Vec3f{});

    snap.activePos = ReadVec3(snap.activePlayer,
                              ResearchConfig::PLAYER_POS_X_OFFSET,
                              ResearchConfig::PLAYER_POS_Y_OFFSET,
                              ResearchConfig::PLAYER_POS_Z_OFFSET,
                              ResearchConfig::kEnableSpatialLog && ResearchConfig::PLAYER_POSITION_OFFSETS_KNOWN);

    snap.activeAltPos = ReadVec3(snap.activePlayer,
                                 ResearchConfig::PLAYER_ALT_POS_X_OFFSET,
                                 ResearchConfig::PLAYER_ALT_POS_Y_OFFSET,
                                 ResearchConfig::PLAYER_ALT_POS_Z_OFFSET,
                                 ResearchConfig::kEnableSpatialLog && ResearchConfig::PLAYER_POSITION_OFFSETS_KNOWN);

    snap.activeBestPos = IsReasonableVec3(snap.activePos) ? snap.activePos : (IsReasonableVec3(snap.activeAltPos) ? snap.activeAltPos : Vec3f{});

    snap.ballPos = ReadVec3(snap.ball,
                            ResearchConfig::BALL_POS_X_OFFSET,
                            ResearchConfig::BALL_POS_Y_OFFSET,
                            ResearchConfig::BALL_POS_Z_OFFSET,
                            ResearchConfig::kEnableSpatialLog);

    snap.ballPredPos = ReadVec3(snap.ball,
                                ResearchConfig::BALL_PRED_X_OFFSET,
                                ResearchConfig::BALL_PRED_Y_OFFSET,
                                ResearchConfig::BALL_PRED_Z_OFFSET,
                                ResearchConfig::kEnableSpatialLog);

    snap.distActorBall = DistanceXZ(snap.actorBestPos, snap.ballPos);
    snap.distActorActive = DistanceXZ(snap.actorBestPos, snap.activeBestPos);
    snap.distActiveBall = DistanceXZ(snap.activeBestPos, snap.ballPos);
    snap.distActorBallPred = DistanceXZ(snap.actorBestPos, snap.ballPredPos);

    snap.ballPower50 = snap.ball ? MemoryReader::ReadU32(snap.ball + ResearchConfig::BALL_POWER_CURRENT_OFFSET, 0) : 0;
    snap.ballPower80 = snap.ball ? MemoryReader::ReadU32(snap.ball + ResearchConfig::BALL_POWER_PREV_OFFSET, 0) : 0;
    snap.ballState84 = snap.ball ? MemoryReader::ReadU32(snap.ball + ResearchConfig::BALL_STATE_OFFSET, 0xFFFFFFFF) : 0xFFFFFFFF;
    snap.ballTimer88 = snap.ball ? MemoryReader::ReadU32(snap.ball + ResearchConfig::BALL_TOUCH_TIMER_OFFSET, 0) : 0;

    const ActorMiniSnapshot actorMini = ReadActorMini(actor);
    const ActorMiniSnapshot activeMini = ReadActorMini(snap.activePlayer);
    const bool actorOwnsOrTouchedBall = MatchesBallActorId(actorMini, snap.ballActorId);
    const bool activeOwnsOrTouchedBall = MatchesBallActorId(activeMini, snap.ballActorId);
    const bool actorVsActive = snap.activeTeam12 <= 1 && actorTeam12 <= 1 && actorTeam12 != snap.activeTeam12;
    const bool ballIsPassOrLoose = (snap.ballState84 == 1 || snap.ballState84 == 11);
    const bool nearRealBall = (snap.distActorBall >= 0.0f && snap.distActorBall <= ResearchConfig::NEAR_DISTANCE);
    const bool closeRealBall = (snap.distActorBall >= 0.0f && snap.distActorBall <= ResearchConfig::CLOSE_DISTANCE);
    const bool nearPredBall = (snap.distActorBallPred >= 0.0f && snap.distActorBallPred <= ResearchConfig::NEAR_DISTANCE);
    const bool closePredBall = (snap.distActorBallPred >= 0.0f && snap.distActorBallPred <= ResearchConfig::CLOSE_DISTANCE);

    if (actorOwnsOrTouchedBall)
    {
        _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "ACTOR_BALL_OWNER_LIKE");
    }
    else if (activeOwnsOrTouchedBall && actorVsActive && snap.distActorActive >= 0.0f)
    {
        if (snap.distActorActive <= ResearchConfig::CLOSE_DISTANCE)
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_OWNER_CLOSE");
        else if (snap.distActorActive <= ResearchConfig::NEAR_DISTANCE)
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_OWNER_NEAR");
        else
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_OWNER_FAR");
    }
    else if (closeRealBall)
    {
        _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "BALL_CLOSE");
    }
    else if (nearRealBall)
    {
        _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "BALL_NEAR");
    }
    else if (snap.distActorActive >= 0.0f && actorVsActive)
    {
        if (snap.distActorActive <= ResearchConfig::CLOSE_DISTANCE)
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_CLOSE");
        else if (snap.distActorActive <= ResearchConfig::NEAR_DISTANCE)
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_NEAR");
        else
            _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "DEFEND_ACTIVE_FAR");
    }
    else if (nearPredBall)
    {
        _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "PREDICTED_BALL_NEAR");
    }
    else
    {
        _snprintf_s(snap.phaseGuess, sizeof(snap.phaseGuess), _TRUNCATE, "UNKNOWN_OR_NO_SPATIAL");
    }

    if (closeRealBall)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "NEAR_REAL_BALL_CLOSE");
    else if (nearRealBall)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "NEAR_REAL_BALL");
    else if (closePredBall)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "NEAR_PREDICTED_BALL_CLOSE");
    else if (nearPredBall)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "NEAR_PREDICTED_BALL");
    else if (activeOwnsOrTouchedBall && actorVsActive && snap.distActorActive >= 0.0f && snap.distActorActive <= ResearchConfig::NEAR_DISTANCE)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "PRESS_ACTIVE_OWNER_LIKE");
    else if (ballIsPassOrLoose && nearPredBall)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "PASS_OR_LOOSE_PREDICTIVE_CHASE");
    else if (ballIsPassOrLoose)
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "PASS_OR_LOOSE_FAR_OR_UNKNOWN");
    else
        _snprintf_s(snap.spatialIntent, sizeof(snap.spatialIntent), _TRUNCATE, "FAR_OR_UNKNOWN");

    if (ResearchConfig::kEnableTargetResolver && regs)
    {
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "eax", regs->eax, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "ebx", regs->ebx, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "ecx", regs->ecx, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "edx", regs->edx, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "esi", regs->esi, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "edi", regs->edi, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "ebp", regs->ebp, snap);

        const uint32_t stack08 = MemoryReader::ReadU32(gameEsp + 0x08, 0);
        const uint32_t stack0C = MemoryReader::ReadU32(gameEsp + 0x0C, 0);
        const uint32_t stack10 = MemoryReader::ReadU32(gameEsp + 0x10, 0);
        const uint32_t stack14 = MemoryReader::ReadU32(gameEsp + 0x14, 0);
        const uint32_t stack18 = MemoryReader::ReadU32(gameEsp + 0x18, 0);
        const uint32_t stack1C = MemoryReader::ReadU32(gameEsp + 0x1C, 0);
        const uint32_t stack20 = MemoryReader::ReadU32(gameEsp + 0x20, 0);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st08", stack08, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st0C", stack0C, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st10", stack10, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st14", stack14, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st18", stack18, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st1C", stack1C, snap);
        ResolvePointer(snap.targetSummary, sizeof(snap.targetSummary), "st20", stack20, snap);
    }

    if (snap.targetSummary[0] == '\0')
        _snprintf_s(snap.targetSummary, sizeof(snap.targetSummary), _TRUNCATE, "none");

    return snap;
}

void BehaviorContext::FormatSpatial(char* out, size_t outSize, const BehaviorSnapshot& s)
{
    if (!out || outSize == 0)
        return;

    const uint32_t actorR1 = (s.actorB0 & ResearchConfig::R1_MASK) == ResearchConfig::R1_MASK ? 1U : 0U;
    const uint32_t actorR2 = (s.actorB0 & ResearchConfig::R2_MASK) == ResearchConfig::R2_MASK ? 1U : 0U;
    const uint32_t actorL2 = (s.actorB0 & ResearchConfig::L2_MASK) == ResearchConfig::L2_MASK ? 1U : 0U;
    const uint32_t activeR1 = (s.activeB0 & ResearchConfig::R1_MASK) == ResearchConfig::R1_MASK ? 1U : 0U;
    const uint32_t activeR2 = (s.activeB0 & ResearchConfig::R2_MASK) == ResearchConfig::R2_MASK ? 1U : 0U;
    const uint32_t activeL2 = (s.activeB0 & ResearchConfig::L2_MASK) == ResearchConfig::L2_MASK ? 1U : 0U;

    _snprintf_s(out, outSize, _TRUNCATE,
        "active=%08X active_valid=%u active_source=%s ball_owner=%08X ball_owner_valid=%u ball_owner_source=%s "
        "ball=%08X ball_actor_id=%u phase=%s spatial_intent=%s "
        "actor_id00=%u active_id00=%u active_team12=%u active_index11=%u "
        "actor_b0=%08X actor_dir=%02X actor_mode=%08X actor_r1=%u actor_r2=%u actor_l2=%u actor_p114=%u actor_anim30=%04X "
        "active_b0=%08X active_dir=%02X active_mode=%08X active_r1=%u active_r2=%u active_l2=%u active_p114=%u active_anim30=%04X "
        "ball84=%u ball88=%u "
        "actor_best=%s active_best=%s "
        "actor_pos_d0=%s(%.2f,%.2f,%.2f) actor_pos_e0=%s(%.2f,%.2f,%.2f) "
        "active_pos_d0=%s(%.2f,%.2f,%.2f) active_pos_e0=%s(%.2f,%.2f,%.2f) "
        "ball_pos20=%s(%.2f,%.2f,%.2f) ball_pred1454=%s(%.2f,%.2f,%.2f) "
        "dist_actor_ball=%.2f dist_actor_active=%.2f dist_active_ball=%.2f dist_actor_ball_pred=%.2f "
        "ball50=%u/0x%08X ball80=%u/0x%08X",
        s.activePlayer, s.activeLooksValid ? 1 : 0, s.activeSource,
        s.ballOwnerPlayer, s.ballOwnerLooksValid ? 1 : 0, s.ballOwnerSource,
        s.ball, s.ballActorId, s.phaseGuess, s.spatialIntent,
        s.actorId00, s.activeId00, s.activeTeam12, s.activeIndex11,
        s.actorB0, s.actorDirBits, s.actorModeBits, actorR1, actorR2, actorL2, s.actorP114, s.actorAnim30,
        s.activeB0, s.activeDirBits, s.activeModeBits, activeR1, activeR2, activeL2, s.activeP114, s.activeAnim30,
        s.ballState84, s.ballTimer88,
        s.actorBestPos.valid ? (IsReasonableVec3(s.actorPos) ? "D0" : (IsReasonableVec3(s.actorAltPos) ? "E0" : "na")) : "na",
        s.activeBestPos.valid ? (IsReasonableVec3(s.activePos) ? "D0" : (IsReasonableVec3(s.activeAltPos) ? "E0" : "na")) : "na",
        IsReasonableVec3(s.actorPos) ? "ok" : "na", s.actorPos.x, s.actorPos.y, s.actorPos.z,
        IsReasonableVec3(s.actorAltPos) ? "ok" : "na", s.actorAltPos.x, s.actorAltPos.y, s.actorAltPos.z,
        IsReasonableVec3(s.activePos) ? "ok" : "na", s.activePos.x, s.activePos.y, s.activePos.z,
        IsReasonableVec3(s.activeAltPos) ? "ok" : "na", s.activeAltPos.x, s.activeAltPos.y, s.activeAltPos.z,
        s.ballPos.valid ? "ok" : "na", s.ballPos.x, s.ballPos.y, s.ballPos.z,
        s.ballPredPos.valid ? "ok" : "na", s.ballPredPos.x, s.ballPredPos.y, s.ballPredPos.z,
        s.distActorBall, s.distActorActive, s.distActiveBall, s.distActorBallPred,
        s.ballPower50, s.ballPower50, s.ballPower80, s.ballPower80);
}

void BehaviorContext::FormatTargets(char* out, size_t outSize, const BehaviorSnapshot& s)
{
    if (!out || outSize == 0)
        return;
    _snprintf_s(out, outSize, _TRUNCATE, "targets=%s", s.targetSummary);
}
