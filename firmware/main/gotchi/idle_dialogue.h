/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <gotchi/gotchi.h>

namespace gotchi {

enum class IdleMood {
    OBSERVING,   // Watching networks
    EXCITED,    // Many networks found
    FOCUSED,    // Deep in scanning
    CURIOUS,    // Noticed something interesting
    IDLE_BOT    // Just being a robot
};

struct IdlePhrase {
    const char* text;
    IdleMood mood;
    int excitement;  // 0-10, affects head movement speed
    bool needsAvatar;  // Requires avatar to be present
};

class IdleDialogue {
public:
    IdleDialogue();

    // Get a random phrase based on current state
    const char* getRandomPhrase(const std::vector<NetworkInfo>& networks, 
                                 int xp, int level, bool hasAvatar);

    // Get phrase tied to specific event
    const char* getNetworkFoundPhrase(const char* ssid);
    const char* getBLEDeviceFoundPhrase(const char* name);
    const char* getMilestonePhrase(int level);
    const char* getModeChangePhrase(Mode mode);

    // Check if should speak now (with cooldown)
    bool shouldSpeak(uint32_t now);

private:
    uint32_t _lastSpeakTime = 0;
    uint32_t _cooldownMs = 4000;  // 4 seconds between idle phrases
    int _lastPhraseIndex = -1;

    const char* _getObservingPhrase();
    const char* _getExcitedPhrase();
    const char* _getFocusedPhrase();
    const char* _getCuriousPhrase();
    const char* _getRobotPhrase();
    const char* _getNetworkFoundPhraseInternal();
    const char* _getModePhrase(Mode mode);

    // StackChan-specific quotes (robot personality)
    static const IdlePhrase _observingPhrases[];    // Watching, waiting
    static const IdlePhrase _excitedPhrases[];       // Lots of networks
    static const IdlePhrase _focusedPhrases[];       // Deep in scan mode
    static const IdlePhrase _curiousPhrases[];        // Interesting discovery
    static const IdlePhrase _robotPhrases[];         // Robot-centric musings
    static const IdlePhrase _networkFoundPhrases[];  // When finding network
    static const IdlePhrase _bleFoundPhrases[];      // When finding BLE device
    static const IdlePhrase _milestonePhrases[];     // Level up
    static const IdlePhrase _modePhrases[];          // Mode changes
};

}  // namespace gotchi