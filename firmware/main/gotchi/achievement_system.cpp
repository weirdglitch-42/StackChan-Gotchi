/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "achievement_system.h"
#include "xp_system.h"
#include "wifi_scanner.h"
#include "network_db.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <time.h>
#include <algorithm>

static const char* TAG = "gotchi_achievements";

namespace gotchi {

const AchievementDef AchievementSystem::ACHIEVEMENTS[] = {
    // Network discovery (8 achievements)
    {"First Contact", "Discover your first WiFi network", 10, true},
    {"Curious", "Discover 10 networks", 25, true},
    {"Popular", "Discover 50 networks", 50, true},
    {"Crowd", "Discover 100 networks", 75, true},
    {"Hub", "Discover 250 networks", 100, true},
    {"Nexus", "Discover 500 networks", 150, true},
    {"Grid", "Discover 1000 networks", 200, true},
    {"Omni", "Discover 2000 networks", 200, true},
    
    // Handshake capture (6 achievements)
    {"First Handshake", "Capture your first handshake", 25, true},
    {"Key Collector", "Capture 5 handshakes", 50, true},
    {"Crypto Hunter", "Capture 10 handshakes", 75, true},
    {"Handshake Pro", "Capture 25 handshakes", 100, true},
    {"Password Collector", "Capture 50 handshakes", 150, true},
    {"Vault", "Capture 100 handshakes", 200, true},
    
    // BLE discovery (5 achievements)
    {"BLE Spotted", "Discover your first BLE device", 10, true},
    {"Bluetooth Hunter", "Discover 5 BLE devices", 25, true},
    {"BLE Scanner", "Discover 25 BLE devices", 50, true},
    {"Low Energy Master", "Discover 50 BLE devices", 75, true},
    {"Radio Wave", "Discover 100 BLE devices", 100, true},
    
    // Channel exploration (4 achievements)
    {"Channel Surfer", "Visit 3 different channels", 15, true},
    {"Frequency Jumper", "Visit 6 different channels", 25, true},
    {"Spectrum Explorer", "Visit 10 different channels", 50, true},
    {"All Channels", "Visit all 13 channels", 100, true},
    
    // Time-based (4 achievements)
    {"1 Hour", "Run for 1 hour", 10, true},
    {"Dedicated", "Run for 12 hours", 25, true},
    {"Week Warrior", "Run for 7 days", 75, true},
    {"Loyal", "Run for 30 days", 150, true},
    
    // Mode usage (6 achievements)
    {"Scout", "Use SCOUT mode", 10, true},
    {"Hunter", "Use HUNT mode", 10, true},
    {"War Driver", "Use WARDIVE mode", 10, true},
    {"Spectrum Analyzer", "Use SPECTRUM mode", 10, true},
    {"BLE Enumerator", "Use BLE_SCAN mode", 10, true},
    {"Config Master", "Use CONFIG mode", 10, true},
    
    // Special (4 achievements)
    {"First Prestige", "Reach prestige level 1", 100, true},
    {"Omega", "Reach prestige level 5", 200, false},
    {"Deep Thought", "Reach level 42", 200, true},
    {"Enigma", "Reach level 40 (hints at secrets)", 50, true},
};

const char* AchievementSystem::DAILY_CHALLENGES[] = {
    "Network Hunter|Discover 5 networks|50",
    "Scanner|Scan for 10 minutes|50",
    "Signal Seeker|Find a network with RSSI > -50|50",
    "First Catch|Capture 1 handshake|75",
    "Channel Surfer|Visit 3 different channels|50",
    "BLE Explorer|Find 3 BLE devices|50",
    "Persistence|Run for 30 minutes|50",
    "Multi-Channel|Scan 5 different channels|50",
};

AchievementSystem::AchievementSystem() 
    : _achievementsBitmask(0), _achievementCount(0), _achievementXP(0),
      _dailyChallengeSeed(0), _dailyChallengeCompleted(false), 
      _dailyStreak(0), _initialized(false) {}

void AchievementSystem::init() {
    if (_initialized) return;
    loadFromNVS();
    _initialized = true;
    ESP_LOGI(TAG, "Achievement System initialized: %u achievements, %d XP", _achievementCount, (int)_achievementXP);
}

void AchievementSystem::update(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config) {
    uint64_t oldAchievements = _achievementsBitmask;
    
    checkUnlockAchievements(networksFound, handshakes, uptimeHours, mode, rogue, config);
    
    if (_achievementsBitmask != oldAchievements) {
        uint64_t newlyUnlocked = _achievementsBitmask & ~oldAchievements;
        
        // Calculate XP from newly unlocked achievements
        for (int i = 0; i < 37; i++) {
            if (newlyUnlocked & (1ULL << i)) {
                if (ACHIEVEMENTS[i].grantsXP) {
                    _achievementXP += ACHIEVEMENTS[i].xpReward;
                    // Award the XP
                    addXP(ACHIEVEMENTS[i].xpReward);
                    ESP_LOGI(TAG, "Achievement '%s' unlocked! +%d XP", 
                             ACHIEVEMENTS[i].name, ACHIEVEMENTS[i].xpReward);
                }
            }
        }
        
        _achievementCount = __builtin_popcountll(_achievementsBitmask);
        saveToNVS();
        ESP_LOGI(TAG, "Achievement(s) unlocked! Total: %u, Total XP: %d", 
                 _achievementCount, (int)_achievementXP);
    }
}

void AchievementSystem::unlockAchievement(int index) {
    if (index < 0 || index >= 37) return;
    _achievementsBitmask |= (1ULL << index);
}

void AchievementSystem::checkUnlockAchievements(uint32_t networksFound, uint32_t handshakes, uint32_t uptimeHours, Mode mode, bool rogue, bool config) {
    // Network discovery (0-7)
    if (networksFound >= 1) unlockAchievement(0);
    if (networksFound >= 10) unlockAchievement(1);
    if (networksFound >= 50) unlockAchievement(2);
    if (networksFound >= 100) unlockAchievement(3);
    if (networksFound >= 250) unlockAchievement(4);
    if (networksFound >= 500) unlockAchievement(5);
    if (networksFound >= 1000) unlockAchievement(6);
    if (networksFound >= 2000) unlockAchievement(7);
    
    // Handshake capture (8-13)
    if (handshakes >= 1) unlockAchievement(8);
    if (handshakes >= 5) unlockAchievement(9);
    if (handshakes >= 10) unlockAchievement(10);
    if (handshakes >= 25) unlockAchievement(11);
    if (handshakes >= 50) unlockAchievement(12);
    if (handshakes >= 100) unlockAchievement(13);
    
    // BLE discovery (14-18)
    int bleCount = getBLEDeviceCount();
    if (bleCount >= 1) unlockAchievement(14);
    if (bleCount >= 5) unlockAchievement(15);
    if (bleCount >= 25) unlockAchievement(16);
    if (bleCount >= 50) unlockAchievement(17);
    if (bleCount >= 100) unlockAchievement(18);
    
    // Channel exploration (19-22)
    uint16_t channelsVisited = getWifiScanner().getChannelsVisitedMask();
    int channelCount = __builtin_popcount(channelsVisited);
    if (channelCount >= 3) unlockAchievement(19);
    if (channelCount >= 6) unlockAchievement(20);
    if (channelCount >= 10) unlockAchievement(21);
    if (channelCount >= 13) unlockAchievement(22);
    
    // Time-based (23-26)
    if (uptimeHours >= 1) unlockAchievement(23);
    if (uptimeHours >= 12) unlockAchievement(24);
    if (uptimeHours >= 168) unlockAchievement(25);
    if (uptimeHours >= 720) unlockAchievement(26);
    
    // Mode usage (27-32)
    if (mode == Mode::SCOUT) unlockAchievement(27);
    if (mode == Mode::HUNT) unlockAchievement(28);
    if (mode == Mode::WARDIVE) unlockAchievement(29);
    if (mode == Mode::SPECTRUM) unlockAchievement(30);
    if (mode == Mode::BLE_SCAN) unlockAchievement(31);
    if (mode == Mode::CONFIG) unlockAchievement(32);
    
    // Special (33-36)
    uint8_t prestige = getPrestige();
    if (prestige >= 1) unlockAchievement(33);
    if (prestige >= 5) unlockAchievement(34);
    
    int32_t level = getXPSystem().getLevel();
    if (level >= 40) unlockAchievement(35);  // Enigma
    if (level >= 42) unlockAchievement(36);  // Deep Thought
}

bool AchievementSystem::getDailyChallenge(ChallengeInfo& challenge) {
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    int dayOfYear = tm_info->tm_yday;
    
    _dailyChallengeSeed = dayOfYear;
    
    int challengeIndex = dayOfYear % 8;
    
    // Parse challenge string: "name|description|xp"
    const char* challengeStr = DAILY_CHALLENGES[challengeIndex];
    static char name[32], desc[64], xpStr[8];
    
    // Simple parsing - find the | separators
    const char* p = challengeStr;
    const char* start = p;
    int pos = 0;
    while (*p && pos < 3) {
        if (*p == '|') {
            if (pos == 0) {
                memcpy(name, start, p - start);
                name[p - start] = '\0';
            } else if (pos == 1) {
                memcpy(desc, start, p - start);
                desc[p - start] = '\0';
            }
            start = p + 1;
            pos++;
        }
        p++;
    }
    if (pos == 2) {
        strcpy(xpStr, start);
    }
    
    challenge.name = name;
    challenge.description = desc;
    challenge.xpReward = atoi(xpStr);
    challenge.isDaily = true;
    challenge.isOneTime = false;
    
    return true;
}

bool AchievementSystem::completeDailyChallenge() {
    if (_dailyChallengeCompleted) return false;
    
    _dailyChallengeCompleted = true;
    _dailyStreak++;
    
    // Award XP for completing the challenge
    ChallengeInfo challenge;
    if (getDailyChallenge(challenge)) {
        addXP(challenge.xpReward);
        ESP_LOGI(TAG, "Daily challenge completed! +%d XP (streak: %u)", 
                 (int)challenge.xpReward, _dailyStreak);
    }
    
    saveToNVS();
    return true;
}

void AchievementSystem::loadFromNVS() {
    nvs_handle_t nvs;
    if (nvs_open("gotchi", NVS_READONLY, &nvs) != ESP_OK) return;
    
    uint64_t val64 = 0;
    uint32_t val32 = 0;
    int32_t valSigned = 0;
    
    if (nvs_get_u64(nvs, "achievements", &val64) == ESP_OK) {
        _achievementsBitmask = val64;
        _achievementCount = __builtin_popcountll(val64);
    }
    if (nvs_get_i32(nvs, "achxp", &valSigned) == ESP_OK) {
        _achievementXP = valSigned;
    }
    if (nvs_get_u32(nvs, "dailyseed", &val32) == ESP_OK) {
        _dailyChallengeSeed = val32;
    }
    if (nvs_get_u32(nvs, "dailydone", &val32) == ESP_OK) {
        _dailyChallengeCompleted = (val32 == 1);
    }
    if (nvs_get_u32(nvs, "dailystreak", &val32) == ESP_OK) {
        _dailyStreak = val32;
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded achievements: %u, XP: %d", _achievementCount, (int)_achievementXP);
}

void AchievementSystem::saveToNVS() {
    nvs_handle_t nvs;
    if (nvs_open("gotchi", NVS_READWRITE, &nvs) != ESP_OK) return;
    
    nvs_set_u64(nvs, "achievements", _achievementsBitmask);
    nvs_set_i32(nvs, "achxp", _achievementXP);
    nvs_set_u32(nvs, "dailyseed", _dailyChallengeSeed);
    nvs_set_u32(nvs, "dailydone", _dailyChallengeCompleted ? 1 : 0);
    nvs_set_u32(nvs, "dailystreak", _dailyStreak);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static AchievementSystem _achievementSystem;

AchievementSystem& getAchievementSystem() {
    return _achievementSystem;
}

}