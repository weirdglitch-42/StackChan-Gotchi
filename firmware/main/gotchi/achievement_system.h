/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <gotchi/gotchi.h>

namespace gotchi {

class AchievementSystem {
public:
    AchievementSystem();
    
    void init();
    void update(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config);
    
    uint32_t getAchievementCount() const { return _achievementCount; }
    uint32_t getAchievementsBitmask() const { return _achievementsBitmask; }
    
    bool getDailyChallenge(ChallengeInfo& challenge);
    bool completeDailyChallenge();
    
    void loadFromNVS();
    void saveToNVS();

private:
    void checkUnlockAchievements(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config);
    
    static const char* DAILY_CHALLENGES[][3];
    
    uint32_t _achievementsBitmask;
    uint32_t _achievementCount;
    uint32_t _dailyChallengeSeed;
    bool _dailyChallengeCompleted;
    bool _initialized;
};

AchievementSystem& getAchievementSystem();

}