#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstddef>
#include "BranchId.h"
#include "FileLogSink.h"

struct BehaviorSnapshot;

// Layout produced by PUSHAD on x86, where ESP points to saved EDI.
// [esp+00]=EDI, [esp+04]=ESI, [esp+08]=EBP, [esp+0C]=original ESP,
// [esp+10]=EBX, [esp+14]=EDX, [esp+18]=ECX, [esp+1C]=EAX.
struct PushadRegisters
{
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

struct MotionMetrics
{
    bool valid = false;
    uint32_t dtMs = 0;
    float actorSpeed = -1.0f;
    float ballSpeed = -1.0f;
    float activeSpeed = -1.0f;
    float predSpeed = -1.0f;
    float prevDistActorBall = -1.0f;
    float prevDistActorActive = -1.0f;
    float prevDistActiveBall = -1.0f;
    float prevDistActorBallPred = -1.0f;
    float deltaActorBall = 0.0f;
    float deltaActorActive = 0.0f;
    float deltaActiveBall = 0.0f;
    float deltaActorBallPred = 0.0f;
    bool actorMovingTowardBall = false;
    bool actorMovingTowardActive = false;
    bool actorMovingTowardPred = false;
};

class AiDifficultyLogger final
{
public:
    static AiDifficultyLogger& Instance();

    bool Initialize();
    void Shutdown();
    void LogFromHook(BranchId branch, const PushadRegisters* regs);

private:
    AiDifficultyLogger();
    ~AiDifficultyLogger() = default;

    AiDifficultyLogger(const AiDifficultyLogger&) = delete;
    AiDifficultyLogger& operator=(const AiDifficultyLogger&) = delete;

    void LogHeader();
    bool IsLikelyActor(uint32_t actor, uint8_t index11, uint8_t team12) const;
    bool UpdateActorStateCache(uint32_t actor, uint8_t state16, uint16_t sub18, BranchId branch, ULONGLONG tick,
                               uint8_t f48, uint8_t f4A, uint8_t f62, uint32_t f98, uint32_t timer114, uint32_t f124,
                               uint8_t& prevState16, uint16_t& prevSub18, BranchId& prevBranch, ULONGLONG& prevTick,
                               bool& fieldChanged, uint32_t& samplesInState, ULONGLONG& stateAge);
    bool ShouldLogProbe(uint32_t actor, BranchId branch, uint8_t difficulty, uint8_t state16,
                        uint16_t sub18, uint8_t actor4A, uint8_t actor48, uint32_t timer114,
                        uint32_t eaxBucket, uint32_t ebxBucket, uint32_t ecxBucket, uint32_t edxBucket);
    uint32_t IncrementHitCount(BranchId branch);
    void UpdateMotionCache(uint32_t actor, ULONGLONG tick, const BehaviorSnapshot& behavior, MotionMetrics& metrics);
    static void FormatMotion(char* out, size_t outSize, const MotionMetrics& metrics);

    static constexpr int kProbeCacheSize = 192;
    static constexpr int kActorStateCacheSize = 512;

    struct ProbeSampleCache
    {
        uint32_t actor = 0;
        BranchId branch = BranchId::NONE;
        uint8_t difficulty = 0xFF;
        uint8_t state16 = 0xFF;
        uint16_t sub18 = 0xFFFF;
        uint8_t actor4A = 0xFF;
        uint8_t actor48 = 0xFF;
        uint32_t timerBucket = 0xFFFFFFFF;
        uint32_t eaxBucket = 0xFFFFFFFF;
        uint32_t ebxBucket = 0xFFFFFFFF;
        uint32_t ecxBucket = 0xFFFFFFFF;
        uint32_t edxBucket = 0xFFFFFFFF;
    };



    struct ActorStateCache
    {
        uint32_t actor = 0;
        uint8_t state16 = 0xFF;
        uint16_t sub18 = 0xFFFF;
        BranchId lastBranch = BranchId::NONE;
        ULONGLONG lastTick = 0;
        uint8_t f48 = 0xFF;
        uint8_t f4A = 0xFF;
        uint8_t f62 = 0xFF;
        uint32_t f98 = 0xFFFFFFFF;
        uint32_t timer114 = 0xFFFFFFFF;
        uint32_t f124 = 0xFFFFFFFF;
        ULONGLONG stateEnterTick = 0;
        uint32_t samplesInState = 0;
        bool initialized = false;
        bool motionInitialized = false;
        ULONGLONG motionTick = 0;
        float lastActorX = 0.0f;
        float lastActorZ = 0.0f;
        float lastBallX = 0.0f;
        float lastBallZ = 0.0f;
        float lastActiveX = 0.0f;
        float lastActiveZ = 0.0f;
        float lastPredX = 0.0f;
        float lastPredZ = 0.0f;
        bool lastActorPosValid = false;
        bool lastBallPosValid = false;
        bool lastActivePosValid = false;
        bool lastPredPosValid = false;
        float lastDistActorBall = -1.0f;
        float lastDistActorActive = -1.0f;
        float lastDistActiveBall = -1.0f;
        float lastDistActorBallPred = -1.0f;
    };

    FileLogSink sink_;
    bool initialized_;
    CRITICAL_SECTION cs_;
    ProbeSampleCache probeCache_[kProbeCacheSize];
    ActorStateCache actorStateCache_[kActorStateCacheSize];
    uint32_t branchHits_[256];
};

extern "C" void __cdecl AiLog_Branch(uint32_t branchId, PushadRegisters* regs);
