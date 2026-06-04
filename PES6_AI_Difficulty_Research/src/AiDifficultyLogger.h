#pragma once

#include <Windows.h>
#include <cstdint>
#include "BranchId.h"
#include "FileLogSink.h"

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
                               uint8_t& prevState16, uint16_t& prevSub18, BranchId& prevBranch, ULONGLONG& prevTick);
    bool ShouldLogProbe(uint32_t actor, BranchId branch, uint8_t difficulty, uint8_t state16,
                        uint16_t sub18, uint8_t actor4A, uint8_t actor48, uint32_t timer114,
                        uint32_t eaxBucket, uint32_t ebxBucket, uint32_t ecxBucket, uint32_t edxBucket);
    uint32_t IncrementHitCount(BranchId branch);

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
        bool initialized = false;
    };

    FileLogSink sink_;
    bool initialized_;
    CRITICAL_SECTION cs_;
    ProbeSampleCache probeCache_[kProbeCacheSize];
    ActorStateCache actorStateCache_[kActorStateCacheSize];
    uint32_t branchHits_[256];
};

extern "C" void __cdecl AiLog_Branch(uint32_t branchId, PushadRegisters* regs);
