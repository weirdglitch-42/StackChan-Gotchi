/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <gotchi/gotchi.h>

namespace gotchi {

struct AchievementDef {
    const char* name;
    const char* description;
    uint8_t xpReward;
    bool grantsXP;
};

class AchievementSystem {
public:
    AchievementSystem();
    
    void init();
    void update(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config);
    
    uint32_t getAchievementCount() const { return _achievementCount; }
    uint32_t getAchievementsBitmask() const { return _achievementsBitmask; }
    int32_t getAchievementXP() const { return _achievementXP; }
    
    bool getDailyChallenge(ChallengeInfo& challenge);
    bool completeDailyChallenge();
    uint32_t getDailyStreak() const { return _dailyStreak; }
    
    void loadFromNVS();
    void saveToNVS();

private:
    void checkUnlockAchievements(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config);
    void unlockAchievement(int index);
    
    static const AchievementDef ACHIEVEMENTS[];
    static const char* DAILY_CHALLENGES[];
    
    uint64_t _achievementsBitmask;
    uint32_t _achievementCount;
    int32_t _achievementXP;
    uint32_t _dailyChallengeSeed;
    bool _dailyChallengeCompleted;
    uint32_t _dailyStreak;
    bool _initialized;
};

AchievementSystem& getAchievementSystem();

}