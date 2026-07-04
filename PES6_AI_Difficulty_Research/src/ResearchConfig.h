#pragma once

#include <cstdint>

namespace ResearchConfig
{
    // fix8c is still a neutral research logger. These switches control log volume and context only.
    constexpr bool kInstallNoisy65F21Probe = false;     // off by default: too noisy in fix6/fix7
    constexpr bool kEnableProbeLog = true;
    constexpr bool kEnableDecisionLog = true;
    constexpr bool kEnableFieldChangeLog = true;
    constexpr bool kEnableFocusLog = true;
    constexpr bool kEnableTargetResolver = true;
    constexpr bool kEnableSpatialLog = true;
    constexpr bool kEnableMotionLog = true;

    // Experimental direct hook for the known positive commit site. Disabled by default because
    // the existing 11C253 probe already observes the commit safely. Enable only after confirming
    // bytes around pes6.exe+11F172 match the documented pattern.
    constexpr bool kInstallExperimental11F172PositiveHook = false;

    // Open/write/close per line. This lets you rename/delete D:/pes/IA/logs.txt while PES stays open;
    // the next logged event recreates logs.txt for the next micro-test.
    constexpr bool kOpenCloseLogPerLine = true;

    // Known global pointers / globals.
    constexpr uintptr_t RVA_ACTIVE_PLAYER_PTR = 0x37E0AA0;
    constexpr uintptr_t RVA_BALL_PTR          = 0x7CCE94;
    constexpr uintptr_t RVA_BALL_ACTOR_ID     = 0x37E09CC;

    // Ball context discovered in the pass/physics/BoostMode investigations.
    constexpr uintptr_t BALL_POWER_CURRENT_OFFSET = 0x50;
    constexpr uintptr_t BALL_POWER_PREV_OFFSET    = 0x80;
    constexpr uintptr_t BALL_STATE_OFFSET         = 0x84;
    constexpr uintptr_t BALL_TOUCH_TIMER_OFFSET   = 0x88;

    // Current physical ball coordinates confirmed by CE under [pes6.exe+7CCE94].
    constexpr uintptr_t BALL_POS_X_OFFSET         = 0x20;
    constexpr uintptr_t BALL_POS_Y_OFFSET         = 0x24; // height / vertical axis
    constexpr uintptr_t BALL_POS_Z_OFFSET         = 0x28;

    // Prediction / destination-ish coordinates discovered in earlier pass investigations.
    constexpr uintptr_t BALL_PRED_X_OFFSET        = 0x1454;
    constexpr uintptr_t BALL_PRED_Y_OFFSET        = 0x1458;
    constexpr uintptr_t BALL_PRED_Z_OFFSET        = 0x145C;

    // Player/actor coordinate candidates.
    // +D0/+D4/+D8 came from CE access to mov eax,[esi+000000D0] and is now primary.
    // +E0/+E4/+E8 was used previously by PassContext as real geometry, so log it too.
    constexpr bool ACTOR_POSITION_OFFSETS_KNOWN  = true;
    constexpr bool PLAYER_POSITION_OFFSETS_KNOWN = true;

    constexpr uintptr_t ACTOR_POS_X_OFFSET = 0x00D0;
    constexpr uintptr_t ACTOR_POS_Y_OFFSET = 0x00D4; // height / vertical axis
    constexpr uintptr_t ACTOR_POS_Z_OFFSET = 0x00D8;

    constexpr uintptr_t PLAYER_POS_X_OFFSET = 0x00D0;
    constexpr uintptr_t PLAYER_POS_Y_OFFSET = 0x00D4; // height / vertical axis
    constexpr uintptr_t PLAYER_POS_Z_OFFSET = 0x00D8;

    constexpr uintptr_t ACTOR_ALT_POS_X_OFFSET = 0x00E0;
    constexpr uintptr_t ACTOR_ALT_POS_Y_OFFSET = 0x00E4;
    constexpr uintptr_t ACTOR_ALT_POS_Z_OFFSET = 0x00E8;

    constexpr uintptr_t PLAYER_ALT_POS_X_OFFSET = 0x00E0;
    constexpr uintptr_t PLAYER_ALT_POS_Y_OFFSET = 0x00E4;
    constexpr uintptr_t PLAYER_ALT_POS_Z_OFFSET = 0x00E8;

    // Useful player fields from the BoostMode/ProBoost research.
    constexpr uintptr_t PLAYER_ID_OFFSET     = 0x00;
    constexpr uintptr_t PLAYER_ANIM_PTR      = 0x04;
    constexpr uintptr_t PLAYER_STATE16       = 0x16;
    constexpr uintptr_t PLAYER_SUB18         = 0x18;
    constexpr uintptr_t PLAYER_B0_OFFSET     = 0xB0;
    constexpr uintptr_t PLAYER_P114_OFFSET   = 0x114;
    constexpr uintptr_t ANIM30_OFFSET        = 0x30;

    constexpr uint32_t R1_MASK    = 0x01000800;
    constexpr uint32_t R2_MASK    = 0x02000200;
    constexpr uint32_t L2_MASK    = 0x00000100;
    constexpr uint32_t CROSS_MASK = 0x00122000;
    constexpr uint32_t SHOT_MASK  = 0x00488000;

    // Heuristic thresholds in PES internal distance units. They are only used to name context.
    constexpr float NEAR_DISTANCE  = 600.0f;
    constexpr float CLOSE_DISTANCE = 250.0f;

    // Movement/motion heuristics. These are diagnostic only.
    constexpr float MOVING_TOWARD_DELTA = 25.0f; // internal units closer since previous sample

    // Logging filters.
    constexpr uint32_t PROBE_FIRST_HITS_ALWAYS = 3;
}
