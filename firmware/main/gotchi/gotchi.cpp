/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "gotchi.h"
#include "storage.h"
#include "gps.h"
#include "xp_system.h"
#include "achievement_system.h"
#include "mode.h"
#include "network_db.h"
#include "handshake_parser.h"
#include "wifi_scanner.h"
#include "deauth_manager.h"
#include "ble_scanner.h"
#include "mode_manager.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <hal/hal.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <string.h>

static const char* TAG = "gotchi";

namespace gotchi {

static bool _initialized = false;
static uint32_t _startTime = 0;
static uint32_t _handshakesCaptured = 0;
static uint32_t _sessionStartTime = 0;
static uint32_t _sessionStartXP = 0;
static int32_t _minHeapSession = 0;
static bool _huntDisclaimerShown = false;

static GotchiConfig _config;

const char* getModeName(Mode mode) {
    return getModeInfo(mode).name;
}

const char* getLevelTitle(int level) {
    return getXPSystem().getLevelTitle();
}

int getXPForLevel(int level) {
    return getXPSystem().getXPForLevel(level);
}

int getXPProgress(int32_t xp, int level) {
    return getXPSystem().getXPProgress();
}

static void loadFromNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved data, starting fresh");
        return;
    }
    
    int32_t savedXP = 0;
    if (nvs_get_i32(nvs, "xp", &savedXP) == ESP_OK) {
        getXPSystem().addXP(savedXP);
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded from NVS: XP=%d, Level=%d", 
        (int)getXPSystem().getXP(), (int)getXPSystem().getLevel());
    
    getXPSystem().loadFromNVS();
    getAchievementSystem().loadFromNVS();
    getNetworkDatabase().loadFromNVS();
}

static void saveToNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing");
        return;
    }
    
    nvs_set_i32(nvs, "xp", getXPSystem().getXP());
    nvs_set_i32(nvs, "level", getXPSystem().getLevel());
    nvs_commit(nvs);
    nvs_close(nvs);
    
    getXPSystem().saveToNVS();
    getAchievementSystem().saveToNVS();
    getNetworkDatabase().saveToNVS();
    
    ESP_LOGI(TAG, "Saved to NVS: XP=%d, Level=%d", 
        (int)getXPSystem().getXP(), (int)getXPSystem().getLevel());
}

void init() {
    if (_initialized) return;

    ESP_LOGI(TAG, "Initializing StackChan-Gotchi...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs recovery, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %d", ret);
    }

    loadFromNVS();
    
    getXPSystem().init();
    getAchievementSystem().init();
    getGpsManager().init();
    getWifiScanner().init();
    getBLEScanner().init();
    
    // Handshake capture handled via getHandshakes() in app_gotchi

    if (initStorage()) {
        if (loadConfig(_config)) {
            ESP_LOGI(TAG, "Config loaded");
        }
        saveConfig(_config);
    }

    _startTime = GetHAL().millis();
    _initialized = true;

    ESP_LOGI(TAG, "StackChan-Gotchi initialized");
}

void update() {
    if (!_initialized) return;

    getGpsManager().update();
    getModeManager().update();

    uint32_t now = GetHAL().millis();
    uint32_t uptime = (now - _startTime) / 1000;

    if (uptime % 3000 == 0 && uptime > 0) {
        addXP(1);
    }
    
    if (getNetworkDatabase().getNetworksFound() > 0 && (now % 60000) < 100) {
        addXP(1);
    }
}

void shutdown() {
    if (!_initialized) return;

    getModeManager().shutdown();
    saveToNVS();
    
    _initialized = false;

    ESP_LOGI(TAG, "StackChan-Gotchi shutdown");
}

void setMode(Mode mode) {
    getModeManager().setMode(mode);
    _sessionStartTime = GetHAL().millis();
    _sessionStartXP = getXPSystem().getXP();
}

Mode getCurrentMode() {
    return getModeManager().getCurrentMode();
}

void setMood(Mood mood) {
    getModeManager().setMood(mood);
}

Mood getCurrentMood() {
    return getModeManager().getCurrentMood();
}

Stats getStats() {
    Stats stats;
    stats.xp = getXPSystem().getXP();
    stats.level = getXPSystem().getLevel();
    stats.networksFound = getNetworkDatabase().getNetworksFound();
    stats.handshakesCaptured = _handshakesCaptured;
    stats.channelsScanned = getWifiScanner().getChannelsScanned();
    stats.uptimeSeconds = (GetHAL().millis() - _startTime) / 1000;
    
    stats.sessionNetworks = getNetworkDatabase().getNetworkCount();
    stats.sessionTimeSeconds = (GetHAL().millis() - _sessionStartTime) / 1000;
    stats.sessionStartTime = _sessionStartTime;
    stats.sessionXPGain = getXPSystem().getXP() - _sessionStartXP;
    stats.currentChannel = getWifiScanner().getCurrentChannel();
    
    stats.freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (_minHeapSession == 0 || stats.freeHeap < _minHeapSession) {
        _minHeapSession = stats.freeHeap;
    }
    stats.minHeap = _minHeapSession;
    
    GPSData gps = getGpsManager().getData();
    stats.gpsValid = gps.valid;
    stats.gpsSatellites = gps.satellites;
    stats.gpsLat = gps.latitude;
    stats.gpsLon = gps.longitude;
    
    return stats;
}

GotchiConfig getConfig() {
    return _config;
}

void addXP(int32_t amount) {
    if (!_initialized) return;
    if (amount <= 0) return;
    
    float multiplier = getModeInfo(getCurrentMode()).xpMultiplier;
    int32_t effectiveAmount = (int32_t)(amount * multiplier);
    getXPSystem().addXP(effectiveAmount);
}

std::vector<NetworkInfo> getNetworks() {
    return getNetworkDatabase().getNetworks();
}

int getNetworkCount() {
    return getNetworkDatabase().getNetworkCount();
}

std::vector<HandshakeInfo> getHandshakes() {
    return getHandshakeParser().getCapturedHandshakes();
}

int getHandshakeCount() {
    return getHandshakeParser().getCapturedCount();
}

bool hasCompleteHandshake(const uint8_t* bssid) {
    return getNetworkDatabase().hasCompleteHandshake(bssid);
}

std::vector<ChannelInfo> getChannelAnalysis() {
    return getNetworkDatabase().getChannelAnalysis();
}

void startSniff() {
    getWifiScanner().startSniff();
}

void stopSniff() {
    getWifiScanner().stopSniff();
}

void startScout() {
    getWifiScanner().startScan();
}

void stopScout() {
    getWifiScanner().stopScan();
}

void startRogue() {
    getModeManager().setMode(Mode::ROGUE);
}

void stopRogue() {
    if (getCurrentMode() == Mode::ROGUE) {
        getModeManager().setMode(Mode::SCOUT);
    }
}

bool isBeaconSpamming() {
    return getModeManager().isBeaconSpamming();
}

void startConfigMode() {
    getModeManager().setMode(Mode::CONFIG);
}

void stopConfigMode() {
    getModeManager().setMode(Mode::SCOUT);
}

bool isConfigMode() {
    return getModeManager().isConfigModeActive();
}

std::vector<BLEDeviceInfo> getBLEDevices() {
    return getNetworkDatabase().getBLEDevices();
}

void startBLEScan() {
    getBLEScanner().startScan();
}

void stopBLEScan() {
    getBLEScanner().stopScan();
}

int getBLEDeviceCount() {
    return getNetworkDatabase().getBLEDeviceCount();
}

bool isDeepThoughtUnlocked() {
    return getXPSystem().getLevel() >= 5;
}

uint8_t getPrestige() {
    return getXPSystem().getPrestige();
}

bool shouldShowHuntDisclaimer() {
    return !_huntDisclaimerShown;
}

void acknowledgeHuntDisclaimer() {
    _huntDisclaimerShown = true;
}

uint32_t getAchievementsBitmask() {
    return getAchievementSystem().getAchievementsBitmask();
}

bool getDailyChallenge(ChallengeInfo& challenge) {
    return getAchievementSystem().getDailyChallenge(challenge);
}

bool completeDailyChallenge() {
    return getAchievementSystem().completeDailyChallenge();
}

}