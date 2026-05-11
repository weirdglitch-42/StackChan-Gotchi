/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <gotchi/gotchi.h>
#include <cstdint>

namespace gotchi {

enum class Mode;

struct NeonColor {
    uint8_t r, g, b;
};

typedef void (*ModeEnterCallback)(Mode mode);
typedef NeonColor (*NeonColorCallback)(Mode mode, int networkCount, uint32_t now);

struct ModeInfo {
    Mode mode;
    const char* name;
    Mood mood;
    bool needsWiFi;
    bool needsBLE;
    bool isScanning;
    bool isBeaconSpam;
    bool isConfigMode;
    float xpMultiplier;
    uint32_t hopIntervalMs;
    bool enableDeauth;
    bool enableChannelHop;
    
    Mode nextMode;
    Mode prevMode;
    uint32_t toneFreq;
    uint32_t headMoveIntervalMs;
    int avatarEmotion;
    NeonColor defaultNeon;
    NeonColorCallback getDynamicNeon;
    ModeEnterCallback onEnter;
};

inline const uint8_t* getChannelSequence() {
    static const uint8_t seq[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    return seq;
}
inline constexpr int getChannelSequenceLength() { return 13; }

const ModeInfo& getModeInfo(Mode mode);
NeonColor getNeonColor(Mode mode, int networkCount, uint32_t now);
void onModeEnter(Mode mode);

inline int getModeAvatarEmotion(Mode mode) { return getModeInfo(mode).avatarEmotion; }
inline uint32_t getModeHeadMoveInterval(Mode mode) { return getModeInfo(mode).headMoveIntervalMs; }

}