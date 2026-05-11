/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <gotchi/gotchi.h>

namespace gotchi {

struct RogueTarget {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    bool valid;
};

class RogueManager {
public:
    RogueManager();
    ~RogueManager();

    void start();
    void stop();
    bool isActive() const { return _beaconSpamming; }

    void setTargetNetwork(const char* ssid, const uint8_t* bssid, uint8_t channel);
    void autoSelectStrongest(const std::vector<NetworkInfo>& networks);
    void setAutoMode(bool autoMode) { _autoMode = autoMode; }
    bool isAutoMode() const { return _autoMode; }

    const RogueTarget& getTarget() const { return _target; }
    const char* getTargetSSID() const { return _target.valid ? _target.ssid : "None"; }
    uint8_t getTargetChannel() const { return _target.channel; }

    void loadFromNVS();
    void saveToNVS();

private:
    void sendBeaconFrame();
    static void beaconTask(void* param);

    TaskHandle_t _taskHandle;
    bool _beaconSpamming;
    RogueTarget _target;
    bool _autoMode;
};

RogueManager& getRogueManager();

}