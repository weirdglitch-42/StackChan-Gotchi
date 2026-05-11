/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "achievement_system.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <time.h>
#include <algorithm>

static const char* TAG = "gotchi_achievements";

namespace gotchi {

const char* AchievementSystem::DAILY_CHALLENGES[][3] = {
    {"Network Hunter", "Find 5 networks", "5"},
    {"Scanner", "Scan for 10 minutes", "10"},
    {"Signal Seeker", "Find a network with RSSI > -50", "-50"},
    {"First Catch", "Capture 1 handshake", "1"},
    {"Channel Surfer", "Visit 3 different channels", "3"},
    {"BLE Explorer", "Find 3 BLE devices", "3"},
    {"Persistence", "Run for 30 minutes", "30"},
    {"Multi-Channel", "Scan 5 different channels", "5"},
};

AchievementSystem::AchievementSystem() 
    : _achievementsBitmask(0), _achievementCount(0),
      _dailyChallengeSeed(0), _dailyChallengeCompleted(false), 
      _initialized(false) {}

void AchievementSystem::init() {
    if (_initialized) return;
    loadFromNVS();
    _initialized = true;
    ESP_LOGI(TAG, "Achievement System initialized: %u achievements", _achievementCount);
}

void AchievementSystem::update(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config) {
    uint32_t oldAchievements = _achievementsBitmask;
    
    checkUnlockAchievements(networksFound, handshakes, uptimeHours, mode, rogue, config);
    
    if (_achievementsBitmask != oldAchievements) {
        _achievementCount = __builtin_popcount(_achievementsBitmask);
        saveToNVS();
        ESP_LOGI(TAG, "Achievement unlocked! Total: %u", _achievementCount);
    }
}

void AchievementSystem::checkUnlockAchievements(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config) {
    if (networksFound >= 1) _achievementsBitmask |= (1 << 0);
    if (networksFound >= 10) _achievementsBitmask |= (1 << 1);
    if (networksFound >= 50) _achievementsBitmask |= (1 << 2);
    if (networksFound >= 100) _achievementsBitmask |= (1 << 3);
    if (networksFound >= 500) _achievementsBitmask |= (1 << 4);
    
    if (handshakes >= 1) _achievementsBitmask |= (1 << 5);
    if (handshakes >= 5) _achievementsBitmask |= (1 << 6);
    if (handshakes >= 10) _achievementsBitmask |= (1 << 7);
    if (handshakes >= 25) _achievementsBitmask |= (1 << 8);
    
    if (uptimeHours >= 1) _achievementsBitmask |= (1 << 9);
    if (uptimeHours >= 24) _achievementsBitmask |= (1 << 10);
    if (uptimeHours >= 168) _achievementsBitmask |= (1 << 11);
    
    if (mode == Mode::SCOUT) _achievementsBitmask |= (1 << 12);
    if (mode == Mode::HUNT) _achievementsBitmask |= (1 << 13);
    if (mode == Mode::WARDIVE) _achievementsBitmask |= (1 << 14);
    if (mode == Mode::BLE_SCAN) _achievementsBitmask |= (1 << 15);
    
    if (rogue) _achievementsBitmask |= (1 << 16);
    if (config) _achievementsBitmask |= (1 << 17);
}

bool AchievementSystem::getDailyChallenge(ChallengeInfo& challenge) {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    int dayOfYear = tm_info->tm_yday;
    
    _dailyChallengeSeed = dayOfYear;
    
    int challengeIndex = dayOfYear % 8;
    challenge.name = DAILY_CHALLENGES[challengeIndex][0];
    challenge.description = DAILY_CHALLENGES[challengeIndex][1];
    challenge.xpReward = atoi(DAILY_CHALLENGES[challengeIndex][2]) * 5;
    challenge.isDaily = true;
    challenge.isOneTime = false;
    
    return true;
}

bool AchievementSystem::completeDailyChallenge() {
    if (_dailyChallengeCompleted) return false;
    
    _dailyChallengeCompleted = true;
    saveToNVS();
    
    ChallengeInfo challenge;
    if (getDailyChallenge(challenge)) {
        ESP_LOGI(TAG, "Daily challenge completed! +%d XP", (int)challenge.xpReward);
    }
    
    return true;
}

void AchievementSystem::loadFromNVS() {
    nvs_handle_t nvs;
    if (nvs_open("gotchi", NVS_READONLY, &nvs) != ESP_OK) return;
    
    uint32_t val = 0;
    if (nvs_get_u32(nvs, "achievements", &val) == ESP_OK) {
        _achievementsBitmask = val;
        _achievementCount = __builtin_popcount(val);
    }
    if (nvs_get_u32(nvs, "dailyseed", &val) == ESP_OK) {
        _dailyChallengeSeed = val;
    }
    if (nvs_get_u32(nvs, "dailydone", &val) == ESP_OK) {
        _dailyChallengeCompleted = (val == 1);
    }
    
    nvs_close(nvs);
}

void AchievementSystem::saveToNVS() {
    nvs_handle_t nvs;
    if (nvs_open("gotchi", NVS_READWRITE, &nvs) != ESP_OK) return;
    
    nvs_set_u32(nvs, "achievements", _achievementsBitmask);
    nvs_set_u32(nvs, "dailyseed", _dailyChallengeSeed);
    nvs_set_u32(nvs, "dailydone", _dailyChallengeCompleted ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static AchievementSystem _achievementSystem;

AchievementSystem& getAchievementSystem() {
    return _achievementSystem;
}

}