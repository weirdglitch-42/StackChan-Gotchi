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
    "Unit", "Watcher", "Scanner", "Seeker", 
    "Prowler", "Phantom", "Apex", "Omega"
};

const int XPSystem::XP_PER_LEVEL[] = {
    0, 50, 150, 350, 700, 1200, 2000, 3500
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
    for (int i = 7; i >= 0; i--) {
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
    if (lvl > 8) lvl = 8;
    return LEVEL_TITLES[lvl - 1];
}

int XPSystem::getXPForLevel(int level) const {
    if (level < 1) level = 1;
    if (level > 8) level = 8;
    return XP_PER_LEVEL[level - 1];
}

int XPSystem::getXPProgress() const {
    if (_level >= 8) return 100;
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

void XPSystem::prestigeReset() {
    if (_level < 8) return;
    
    _prestige++;
    _xp = 0;
    _level = 1;
    saveToNVS();
    
    ESP_LOGI(TAG, "Prestige! Now at prestige level %u", (unsigned)_prestige);
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

}