#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include "AiDifficultyLogger.h"

struct Vec3f
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool valid = false;
};

struct ActorMiniSnapshot
{
    uint32_t ptr = 0;
    bool likelyActor = false;
    uint8_t id00 = 0xFF;
    uint8_t team12 = 0xFF;
    uint8_t index11 = 0xFF;
    uint8_t state16 = 0xFF;
    uint16_t sub18 = 0xFFFF;
    uint32_t b0 = 0;
    uint8_t dirBits = 0;
    uint32_t modeBits = 0;
    uint32_t p114 = 0;
    uint16_t anim30 = 0xFFFF;
    Vec3f pos;
    Vec3f altPos;
};

struct BehaviorSnapshot
{
    uint32_t actor = 0;
    uint32_t activePlayer = 0;
    uint32_t ballOwnerPlayer = 0;
    uint32_t ball = 0;

    bool activeLooksValid = false;
    bool ballOwnerLooksValid = false;
    char activeSource[64]{};
    char ballOwnerSource[64]{};

    uint8_t actorId00 = 0xFF;
    uint8_t activeId00 = 0xFF;
    uint8_t activeTeam12 = 0xFF;
    uint8_t activeIndex11 = 0xFF;
    uint8_t ballActorId = 0xFF;

    uint32_t actorB0 = 0;
    uint32_t activeB0 = 0;
    uint8_t actorDirBits = 0;
    uint8_t activeDirBits = 0;
    uint32_t actorModeBits = 0;
    uint32_t activeModeBits = 0;
    uint16_t actorAnim30 = 0xFFFF;
    uint16_t activeAnim30 = 0xFFFF;
    uint32_t actorP114 = 0;
    uint32_t activeP114 = 0;

    Vec3f actorPos;
    Vec3f actorAltPos;
    Vec3f actorBestPos;
    Vec3f activePos;
    Vec3f activeAltPos;
    Vec3f activeBestPos;
    Vec3f ballPos;
    Vec3f ballPredPos;

    float distActorBall = -1.0f;
    float distActorActive = -1.0f;
    float distActiveBall = -1.0f;
    float distActorBallPred = -1.0f;

    uint32_t ballPower50 = 0;
    uint32_t ballPower80 = 0;
    uint32_t ballState84 = 0;
    uint32_t ballTimer88 = 0;

    char phaseGuess[96]{};
    char spatialIntent[96]{};
    char targetSummary[1024]{};
};

class BehaviorContext final
{
public:
    static BehaviorSnapshot Capture(uint32_t actor, const PushadRegisters* regs, uint32_t gameEsp,
                                    uint8_t actorTeam12, uint8_t actorIndex11);

    static void FormatSpatial(char* out, size_t outSize, const BehaviorSnapshot& snap);
    static void FormatTargets(char* out, size_t outSize, const BehaviorSnapshot& snap);

    static bool IsLikelyActorPointer(uint32_t ptr);
    static ActorMiniSnapshot ReadActorMini(uint32_t ptr);

private:
    static Vec3f ReadVec3(uint32_t base, uintptr_t xOff, uintptr_t yOff, uintptr_t zOff, bool enabled);
    static bool IsReasonableFloat(float v);
    static bool IsReasonableVec3(const Vec3f& v);
    static uint16_t ReadAnim30(uint32_t playerOrActor);
    static bool MatchesBallActorId(const ActorMiniSnapshot& mini, uint8_t ballActorId);
    static uint32_t ResolveBallOwnerPlayer(const PushadRegisters* regs, uint32_t gameEsp, uint32_t actor, uint8_t ballActorId, char* sourceOut, size_t sourceOutSize);
    static uint32_t ResolveActivePlayer(const PushadRegisters* regs, uint32_t gameEsp, uint32_t actor, uint8_t ballActorId, uint32_t globalActive, uint32_t ballOwner, char* sourceOut, size_t sourceOutSize);
    static void ConsiderCandidate(uint32_t value, const char* source, uint8_t ballActorId, uint32_t actor, uint32_t& bestOwner, char* bestOwnerSource, size_t bestOwnerSourceSize, uint32_t& firstActorCandidate, char* firstActorSource, size_t firstActorSourceSize);
    static float DistanceXZ(const Vec3f& a, const Vec3f& b);
    static void ResolvePointer(char* out, size_t outSize, const char* label, uint32_t value, const BehaviorSnapshot& snap);
    static void Append(char* out, size_t outSize, const char* text);
};
