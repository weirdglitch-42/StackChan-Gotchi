/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <gotchi/gotchi.h>
#include <cstdint>
#include <esp_err.h>
#include <esp_netif.h>

namespace gotchi {

class ModeManager {
public:
    ModeManager();
    
    Mode getCurrentMode() const { return _currentMode; }
    Mood getCurrentMood() const { return _currentMood; }
    bool isBeaconSpamming() const { return _beaconSpamming; }
    bool isConfigModeActive() const { return _configModeActive; }
    
    void setMode(Mode mode);
    void setMood(Mood mood);
    
    void update();
    void shutdown();

private:
    void startMode(Mode mode);
    void stopMode(Mode mode);
    
    void startRogueMode();
    void stopRogueMode();
    void startConfigMode();
    void stopConfigMode();
    
    void initAPNetif();
    void deinitWiFi();
    
    Mode _currentMode;
    Mood _currentMood;
    bool _beaconSpamming;
    bool _configModeActive;
    TaskHandle_t _configTaskHandle;
    static bool _netifInitialized;
    static esp_netif_t* _apNetifHandle;
};

ModeManager& getModeManager();

}