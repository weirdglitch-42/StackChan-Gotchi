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
    int headYawPattern;
    int16_t headYawRange;
    int16_t headPitch;
    int avatarEmotion;
    bool enableDialogue;
    uint32_t dialogueIntervalMs;
    int dialogueCategory;
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
inline int16_t getModeHeadPitch(Mode mode) { return getModeInfo(mode).headPitch; }
inline bool isDialogueEnabled(Mode mode) { return getModeInfo(mode).enableDialogue; }
inline uint32_t getModeDialogueInterval(Mode mode) { return getModeInfo(mode).dialogueIntervalMs; }
inline int getModeDialogueCategory(Mode mode) { return getModeInfo(mode).dialogueCategory; }

inline int16_t getModeHeadYaw(Mode mode, uint32_t now, uint32_t interval) {
    const ModeInfo& mi = getModeInfo(mode);
    switch (mi.headYawPattern) {
        case 1: return (now / interval) % 2 ? mi.headYawRange : -mi.headYawRange;
        case 2: return (int16_t)((now / interval) % 6 - 3) * (mi.headYawRange / 3);
        case 3: return (int16_t)((now / 250 % 4) - 2) * (mi.headYawRange / 3);
        default: return 0;
    }
}

}