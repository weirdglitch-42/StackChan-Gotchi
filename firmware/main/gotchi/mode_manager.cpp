/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "mode_manager.h"
#include "network_db.h"
#include "wifi_scanner.h"
#include "deauth_manager.h"
#include "ble_scanner.h"
#include "rogue_manager.h"
#include "web_manager.h"
#include "mode.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <hal/hal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "mode_manager";

namespace gotchi {

bool ModeManager::_netifInitialized = false;
esp_netif_t* ModeManager::_apNetifHandle = nullptr;

ModeManager::ModeManager() 
    : _currentMode(Mode::IDLE), _currentMood(Mood::NEUTRAL),
      _beaconSpamming(false), _configModeActive(false), _configTaskHandle(nullptr) {
}

void ModeManager::initAPNetif() {
    if (_netifInitialized) return;
    
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_netif_init failed: %d", err);
        return;
    }
    
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "esp_event_loop_create_default failed: %d", err);
        return;
    }
    
    _netifInitialized = true;
}

void ModeManager::deinitWiFi() {
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void ModeManager::setMode(Mode mode) {
    if (_currentMode == mode) return;
    
    ESP_LOGI(TAG, "Switching from %s to %s", 
             getModeName(_currentMode), getModeName(mode));
    
    stopMode(_currentMode);
    _currentMode = mode;
    _currentMood = getModeInfo(mode).mood;
    startMode(mode);
}

void ModeManager::setMood(Mood mood) {
    _currentMood = mood;
}

void ModeManager::stopMode(Mode mode) {
    switch (mode) {
        case Mode::HUNT:
            getWifiScanner().stopSniff();
            getDeauthManager().stop();
            break;
        case Mode::SCOUT:
            getWifiScanner().stopSniff();
            break;
        case Mode::WARDIVE:
        case Mode::SPECTRUM:
            getWifiScanner().stopSniff();
            break;
        case Mode::BLE_SCAN:
            getBLEScanner().stopScan();
            break;
        case Mode::ROGUE:
            stopRogueMode();
            break;
        case Mode::CONFIG:
            stopConfigMode();
            break;
        default:
            break;
    }
}

void ModeManager::startMode(Mode mode) {
    switch (mode) {
        case Mode::HUNT:
            ESP_LOGI(TAG, "Starting HUNT mode");
            getWifiScanner().startSniff();
            getDeauthManager().start();
            break;
        case Mode::SCOUT:
            ESP_LOGI(TAG, "Starting SCOUT mode");
            getWifiScanner().startSniff();
            break;
        case Mode::WARDIVE:
            ESP_LOGI(TAG, "Starting WARDIVE mode");
            getWifiScanner().startSniff();
            break;
        case Mode::SPECTRUM:
            ESP_LOGI(TAG, "Starting SPECTRUM mode");
            getWifiScanner().startSniff();
            break;
        case Mode::BLE_SCAN:
            ESP_LOGI(TAG, "Starting BLE-SCAN mode");
            getBLEScanner().startScan();
            break;
        case Mode::ROGUE:
            ESP_LOGI(TAG, "Starting ROGUE mode");
            startRogueMode();
            break;
        case Mode::CONFIG:
            ESP_LOGI(TAG, "Starting CONFIG mode");
            startConfigMode();
            break;
        case Mode::IDLE:
            ESP_LOGI(TAG, "IDLE mode");
            break;
        default:
            break;
    }
}

void ModeManager::startRogueMode() {
    if (_beaconSpamming) return;
    
    ESP_LOGI(TAG, "Starting ROGUE mode - beacon spam");
    ESP_LOGW(TAG, "WARNING: Educational use only!");
    
    deinitWiFi();
    
    initAPNetif();
    
    if (!_apNetifHandle) {
        _apNetifHandle = esp_netif_create_default_wifi_ap();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %d", ret);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi AP mode failed: %d", ret);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    wifi_config_t ap_config = {0};
    ap_config.ap.channel = 6;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 0;
    ap_config.ap.beacon_interval = 100;
    ap_config.ap.ssid[0] = 0;
    ap_config.ap.ssid_len = 0;
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    RogueManager& rogue = getRogueManager();
    rogue.loadFromNVS();
    
    if (!rogue.getTarget().valid) {
        const auto& networks = getNetworkDatabase().getNetworks();
        if (!networks.empty()) {
            rogue.autoSelectStrongest(networks);
        }
    }
    
    rogue.start();
    _beaconSpamming = true;
}

void ModeManager::stopRogueMode() {
    if (!_beaconSpamming) return;
    
    getRogueManager().stop();
    _beaconSpamming = false;
    
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "ROGUE mode stopped");
}

static void configModeTask(void* param) {
    (void)param;
    ESP_LOGI(TAG, "Config mode task starting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    getWebManager().start();
    ESP_LOGI(TAG, "Config mode ready - visit http://192.168.4.1");
    
    while (getModeManager().isConfigModeActive()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void ModeManager::startConfigMode() {
    if (_configModeActive) return;
    
    ESP_LOGI(TAG, "Starting CONFIG mode");
    
    deinitWiFi();
    
    initAPNetif();
    
    if (!_apNetifHandle) {
        _apNetifHandle = esp_netif_create_default_wifi_ap();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %d", ret);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi AP mode failed: %d", ret);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    
    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "StackChan-Config");
    ap_config.ap.ssid_len = 16;
    ap_config.ap.channel = 6;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    
    esp_wifi_start();
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    _configModeActive = true;
    xTaskCreate(configModeTask, "config_task", 4096, NULL, 5, &_configTaskHandle);
    
    ESP_LOGI(TAG, "CONFIG mode active");
}

void ModeManager::stopConfigMode() {
    if (!_configModeActive) return;
    
    _configModeActive = false;
    
    if (_configTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _configTaskHandle = nullptr;
    }
    
    getWebManager().stop();
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "CONFIG mode stopped");
}

void ModeManager::update() {
    if (_currentMode == Mode::HUNT || _currentMode == Mode::WARDIVE ||
        _currentMode == Mode::SPECTRUM || _currentMode == Mode::SCOUT) {
        getWifiScanner().update();
    }
}

void ModeManager::shutdown() {
    stopMode(_currentMode);
    _currentMode = Mode::IDLE;
    _currentMood = Mood::NEUTRAL;
    ESP_LOGI(TAG, "ModeManager shutdown");
}

static ModeManager _modeManager;

ModeManager& getModeManager() {
    return _modeManager;
}

}