#include "pch.h"
#include "PassPowerConfig.h"

PassPowerConfig g_passConfig = {
    // inertia limits
    { 2500, 5000, 7250 },

    // lowBoostTable (ZERO, VERY_LOW, LOW_MID, CARRY, HIGH)
    {
        { 0, 0, 0, 0, 0 },                                          // dist0_2
        { 0x0100, 0x0200, 0x0180, 0x0100, 0x0180 },                 // dist3_4
        { 0x0280, 0x0480, 0x0380, 0x0200, 0x0380 },                 // dist5_6
        { 0x0700, 0x0B00, 0x0900, 0x0500, 0x0700 }                  // dist7_plus
    },

    // midBoostTable (ZERO, VERY_LOW, LOW_MID, CARRY, HIGH)
    {
        { 0, 0, 0, 0, 0 },                                          // dist0_2
        { 0, 0, 0, 0, 0 },                                          // dist3_4
        { 0x0100, 0x0200, 0x0180, 0x0100, 0x0180 },                 // dist5_6
        { 0x0280, 0x0500, 0x0400, 0x0200, 0x0380 }                  // dist7_plus
    },

    // bases
    0x0480, // baseLowBoost
    0x0180, // baseMidBoost

    // umbrales
    0x5800, // ediThresholdLow
    0x6400, // ediThresholdIgnore
    0x6800, // ediMaxCap

    // No context soft boost
    { 0x0400, 0x0600, 0x0500, 0x0400, 0x0400 }, // noContextLowBaseBoost
    0x0180, // noContextMidBaseBoost
    0x6000, // noContextEdiCap

    // Rescate LOW
    0x4A00, // rescueLowEdiThreshold
    5,      // rescueLowDistThreshold
    0x0400, // rescueLowExtraHighInertia
    0x0300, // rescueLowExtraOtherInertia

    // Rescate Corto LOW
    0x3C00, // shortRescueLowEdiThreshold
    3,      // shortRescueLowDistMin
    4,      // shortRescueLowDistMax
    0x0280, // shortRescueLowExtraHighInertia
    0x0200, // shortRescueLowExtraOtherInertia

    // Pisos blandos
    0x4F00, // softFloorLowDist5_6
    0x5300, // softFloorLowDist7_plus
    0x4200  // softFloorLowDist3_4
};
