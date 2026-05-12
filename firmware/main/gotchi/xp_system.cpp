/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "xp_system.h"
#include <nvs_flash.h>
#include <esp_log.h>
#include <hal/hal.h>

static const char* TAG = "gotchi_xp";

namespace gotchi {

const char* XPSystem::LEVEL_TITLES[] = {
    // Levels 1-8 (original)
    "Unit", "Watcher", "Scanner", "Seeker", 
    "Prowler", "Phantom", "Apex", "Omega",
    // Levels 9-16 (extended scanner roles)
    "Observer", "Probe", "Analyst", "Decoder", "Tracker", "Hunter", "Crawler", "Synth",
    // Levels 17-24 (advanced roles)
    "Cortex", "Nexus", "Matrix", "Quantum", "Singularity", "Hyperion", "Archon", "Titan",
    // Levels 25-32 (cosmic/tech)
    "Prime", "Alpha", "Omega Prime", "Supreme", "Transcendent", "Paramount", "Glorious", "Eternal",
    // Levels 33-39 (ultimate tiers)
    "Legendary", "Mythic", "Omnipotent", "Infinite", "Absolute", "Ultimate", "Paramount",
    // Level 40 (Enigma - hints at secrets)
    "Enigma",
    // Levels 41-42 (secret - require special unlock)
    "Marvin", "Deep Thought"
};

const int XPSystem::XP_PER_LEVEL[] = {
    0, 50, 150, 350, 700, 1200, 2000, 3500,
    5000, 6500, 8500, 11000, 14000, 17500, 21500, 26000,
    31000, 36500, 42500, 49000, 56000, 63500, 71500, 80000,
    89000, 98500, 108500, 119000, 130000, 141500, 153500, 166000,
    179000, 192500, 206500, 221000, 236000, 251500, 267500,
    290000,
    320000, 420000
};

const int XPSystem::LEVEL_SECRET_UNLOCK[] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0,
    0,  // Level 40 - Enigma (not secret, but hints at secrets)
    1,  // Level 41 - Marvin (secret unlock)
    1   // Level 42 - Deep Thought (secret unlock)
};

XPSystem::XPSystem() : _xp(0), _level(1), _prestige(0), _initialized(false) {}

void XPSystem::init() {
    if (_initialized) return;
    loadFromNVS();
    _initialized = true;
    ESP_LOGI(TAG, "XP System initialized: XP=%d, Level=%d, Prestige=%u", (int)_xp, (int)_level, _prestige);
}

void XPSystem::addXP(int32_t amount) {
    if (amount <= 0) return;
    _xp += amount;
    updateLevel();
    
    static uint32_t lastSave = 0;
    uint32_t now = GetHAL().millis();
    if ((now - lastSave) > 60000) {
        lastSave = now;
        saveToNVS();
    }
}

void XPSystem::updateLevel() {
    for (int i = 41; i >= 0; i--) {
        if (_xp >= XP_PER_LEVEL[i]) {
            _level = i + 1;
            return;
        }
    }
    _level = 1;
}

const char* XPSystem::getLevelTitle() const {
    int lvl = _level;
    if (lvl < 1) lvl = 1;
    if (lvl > 42) lvl = 42;
    return LEVEL_TITLES[lvl - 1];
}

int XPSystem::getXPForLevel(int level) const {
    if (level < 1) level = 1;
    if (level > 42) level = 42;
    return XP_PER_LEVEL[level - 1];
}

int XPSystem::getXPProgress() const {
    if (_level >= 42) return 100;
    if (_level < 1) return 0;
    
    int currentLevelXP = XP_PER_LEVEL[_level - 1];
    int nextLevelXP = XP_PER_LEVEL[_level];
    int xpInLevel = _xp - currentLevelXP;
    int xpNeeded = nextLevelXP - currentLevelXP;
    
    if (xpNeeded <= 0) return 100;
    
    int progress = (xpInLevel * 100) / xpNeeded;
    if (progress > 100) progress = 100;
    if (progress < 0) progress = 0;
    return progress;
}

int XPSystem::getXPToNextLevel() const {
    if (_level >= 42) return 0;
    return XP_PER_LEVEL[_level] - _xp;
}

int XPSystem::getXPToMaxLevel() const {
    return XP_PER_LEVEL[41] - _xp;
}

bool XPSystem::isLevelSecret(int level) const {
    if (level < 1 || level > 42) return false;
    return LEVEL_SECRET_UNLOCK[level - 1] == 1;
}

bool XPSystem::isLevelUnlocked(int level) const {
    if (level < 1 || level > 42) return false;
    if (!isLevelSecret(level)) return true;
    
    if (level == 41) {
        return _xp >= XP_PER_LEVEL[39] && _prestige >= 1;
    }
    if (level == 42) {
        return _xp >= XP_PER_LEVEL[40] && _prestige >= 2;
    }
    return false;
}

void XPSystem::prestigeReset() {
    if (_level < 42) return;
    
    _prestige++;
    _xp = 0;
    _level = 1;
    saveToNVS();
    
    ESP_LOGI(TAG, "Prestige! Now at prestige level %u", (unsigned)_prestige);
}

float XPSystem::getXPMultiplier() const {
    return 1.0f + (_prestige * 0.1f);
}

void XPSystem::loadFromNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved XP data");
        return;
    }
    
    int32_t savedXP = 0;
    int32_t savedLevel = 1;
    uint8_t savedPrestige = 0;
    
    if (nvs_get_i32(nvs, "xp", &savedXP) == ESP_OK) {
        _xp = savedXP;
    }
    if (nvs_get_i32(nvs, "level", &savedLevel) == ESP_OK) {
        _level = savedLevel;
    }
    if (nvs_get_u8(nvs, "prestige", &savedPrestige) == ESP_OK) {
        _prestige = savedPrestige;
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded: XP=%d, Level=%d, Prestige=%u", (int)_xp, (int)_level, _prestige);
}

void XPSystem::saveToNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return;
    
    nvs_set_i32(nvs, "xp", _xp);
    nvs_set_i32(nvs, "level", _level);
    nvs_set_u8(nvs, "prestige", _prestige);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static XPSystem _xpSystem;

XPSystem& getXPSystem() {
    return _xpSystem;
}

}