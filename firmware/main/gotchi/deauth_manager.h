/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <freertos/FreeRTOS.h>

namespace gotchi {

class DeauthManager {
public:
    DeauthManager();
    
    bool isActive() const { return _active; }
    uint32_t getFrameCount() const { return _frameCount; }
    
    bool start();
    void stop();
    void update();

private:
    static void deauthTask(void* param);
    void sendDeauthFrame(const uint8_t* bssid, uint8_t reason);
    
    TaskHandle_t _taskHandle;
    bool _active;
    uint32_t _frameCount;
    uint32_t _lastDeauth;
};

DeauthManager& getDeauthManager();

}