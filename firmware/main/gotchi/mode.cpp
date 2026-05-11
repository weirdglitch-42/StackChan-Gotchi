/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "mode.h"

namespace gotchi {

static NeonColor getIdleNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool pulse = (now / 1000) % 2;
    return pulse ? NeonColor{0x00, 0xAA, 0x66} : NeonColor{0x00, 0x66, 0x33};
}

static NeonColor getHuntNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode;
    bool blinkOn = (now / 200) % 2;
    if (netCount >= 10) {
        return blinkOn ? NeonColor{0x00, 0xFF, 0x00} : NeonColor{0x00, 0x88, 0x00};
    } else if (netCount >= 5) {
        return blinkOn ? NeonColor{0x00, 0xFF, 0x88} : NeonColor{0x00, 0xAA, 0x55};
    } else {
        return blinkOn ? NeonColor{0x00, 0x88, 0x44} : NeonColor{0x00, 0x44, 0x22};
    }
}

static NeonColor getScoutNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 400) % 2;
    return blinkOn ? NeonColor{0x00, 0x66, 0xFF} : NeonColor{0x00, 0x33, 0x88};
}

static NeonColor getWardiveNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 300) % 2;
    return blinkOn ? NeonColor{0xFF, 0x66, 0x00} : NeonColor{0x88, 0x33, 0x00};
}

static NeonColor getSpectrumNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    int phase = (now / 200) % 6;
    return NeonColor{0xFF, (uint8_t)(0x00 << (phase * 2)), 0xFF};
}

static NeonColor getBleScanNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 250) % 2;
    return blinkOn ? NeonColor{0x00, 0x88, 0xFF} : NeonColor{0x00, 0x44, 0x88};
}

static NeonColor getRogueNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 150) % 2;
    return blinkOn ? NeonColor{0xAA, 0x00, 0xFF} : NeonColor{0x55, 0x00, 0x88};
}

static NeonColor getConfigNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 500) % 2;
    return blinkOn ? NeonColor{0xFF, 0xAA, 0x00} : NeonColor{0xAA, 0x55, 0x00};
}

static NeonColor getStatsNeon(Mode mode, int netCount, uint32_t now) {
    (void)mode; (void)netCount;
    bool blinkOn = (now / 500) % 2;
    return blinkOn ? NeonColor{0xAA, 0xFF, 0xFF} : NeonColor{0x55, 0xAA, 0xAA};
}

static const ModeInfo MODE_TABLE[] = {
    {Mode::IDLE,     "IDLE",     Mood::NEUTRAL,   false, false, false, false, false, 0.0f,  0,    false,                false,
     Mode::SCOUT,    Mode::STATS, 0,          3000, 0,
     {0x00, 0xFF, 0x88}, getIdleNeon, nullptr},
    {Mode::SCOUT,    "SCOUT",    Mood::NEUTRAL,   true,  false, true,  false, false, 0.8f,  200,  false,                true,
     Mode::HUNT,     Mode::IDLE,  800,        1500, 1,
     {0x00, 0x66, 0xFF}, getScoutNeon, nullptr},
    {Mode::HUNT,     "HUNT",     Mood::FOCUSED,   true,  false, true,  false, false, 1.0f,  200,  true,                 true,
     Mode::WARDIVE,  Mode::SCOUT, 600,        800,  4,
     {0x00, 0xFF, 0x00}, getHuntNeon, nullptr},
    {Mode::WARDIVE,  "WARDIVE",  Mood::EXCITED,   true,  false, true,  false, false, 1.5f,  200,  false,                true,
     Mode::SPECTRUM, Mode::HUNT,  1000,       600,  2,
     {0xFF, 0x66, 0x00}, getWardiveNeon, nullptr},
    {Mode::SPECTRUM, "SPECTRUM", Mood::FOCUSED,   true,  false, true,  false, false, 1.2f,  200,  false,                true,
     Mode::BLE_SCAN, Mode::WARDIVE, 1200,    1000, 4,
     {0xFF, 0x00, 0xFF}, getSpectrumNeon, nullptr},
    {Mode::BLE_SCAN, "BLE_SCAN", Mood::FOCUSED,   false, true,  false, false, false, 1.0f,  0,    false,                false,
     Mode::ROGUE,    Mode::SPECTRUM, 0,       600,  4,
     {0x00, 0x88, 0xFF}, getBleScanNeon, nullptr},
    {Mode::ROGUE,    "ROGUE",    Mood::EXCITED,   true,  false, false, true,  false, 1.0f,  0,    false,                false,
     Mode::STATS,    Mode::BLE_SCAN, 350,     500,  2,
     {0xAA, 0x00, 0xFF}, getRogueNeon, nullptr},
    {Mode::CONFIG,   "CONFIG",   Mood::NEUTRAL,   true,  false, false, false, true,  0.0f,  0,    false,                false,
     Mode::SCOUT,    Mode::SCOUT,  0,       3000, 0,
     {0xFF, 0xAA, 0x00}, getConfigNeon, nullptr},
    {Mode::STATS,    "STATS",    Mood::HAPPY,     false, false, false, false, false, 0.0f,  0,    false,                false,
     Mode::IDLE,     Mode::ROGUE,  0,       2000, 1,
     {0xAA, 0xFF, 0xFF}, getStatsNeon, nullptr},
};

const ModeInfo& getModeInfo(Mode mode) {
    int idx = static_cast<int>(mode);
    if (idx < 0 || idx >= 9) {
        idx = 0;
    }
    return MODE_TABLE[idx];
}

NeonColor getNeonColor(Mode mode, int networkCount, uint32_t now) {
    const ModeInfo& mi = getModeInfo(mode);
    if (mi.getDynamicNeon) {
        return mi.getDynamicNeon(mode, networkCount, now);
    }
    return mi.defaultNeon;
}

void onModeEnter(Mode mode) {
    const ModeInfo& mi = getModeInfo(mode);
    if (mi.onEnter) {
        mi.onEnter(mode);
    }
}

}