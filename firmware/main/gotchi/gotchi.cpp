/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "gotchi.h"
#include "storage.h"
#include "gps.h"
#include <esp_wifi.h>
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

static const char* TAG = "gotchi";

namespace gotchi {

static Mode _currentMode = Mode::IDLE;
static Mood _currentMood = Mood::NEUTRAL;
static int32_t _xp = 0;
static int32_t _level = 1;
static bool _initialized = false;
static bool _sniffing = false;
static bool _wifiInitialized = false;

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

// StackChan-Gotchi unique level titles - robot personality progression
static const char* LEVEL_TITLES[] = {
    "Unit",       // Lv1 - Freshly booted
    "Watcher",   // Lv2 - Observing networks
    "Scanner",   // Lv3 - Active scanning
    "Seeker",    // Lv4 - Finding targets
    "Prowler",   // Lv5 - Silent operation
    "Phantom",   // Lv6 - Undetected
    "Apex",       // Lv7 - Top predator
    "Omega"      // Lv8 - Network master
};

static const int XP_PER_LEVEL[] = {
    0, 50, 150, 350, 700, 1200, 2000, 3500
};

const char* getModeName(Mode mode) {
    switch (mode) {
        case Mode::IDLE: return "IDLE";
        case Mode::SNIFF: return "SNIFF";
        case Mode::SCOUT: return "SCOUT";
        case Mode::WARDIVE: return "WARDIVE";
        case Mode::SPECTRUM: return "SPECTRUM";
        case Mode::BLE_SNIFF: return "BLE-SNIFF";
        default: return "UNKNOWN";
    }
}

const char* getLevelTitle(int level) {
    if (level < 1) level = 1;
    if (level > 8) level = 8;
    return LEVEL_TITLES[level - 1];
}

int getXPForLevel(int level) {
    if (level < 1) level = 1;
    if (level > 8) level = 8;
    return XP_PER_LEVEL[level - 1];
}

int getXPProgress(int32_t xp, int level) {
    if (level < 1) level = 1;
    if (level > 8) return 100;  // Max level
    
    int currentLevelXP = XP_PER_LEVEL[level - 1];
    int nextLevelXP = (level < 8) ? XP_PER_LEVEL[level] : XP_PER_LEVEL[7];
    
    int xpInLevel = xp - currentLevelXP;
    int xpNeeded = nextLevelXP - currentLevelXP;
    
    if (xpNeeded <= 0) return 100;
    
    int progress = (xpInLevel * 100) / xpNeeded;
    if (progress > 100) progress = 100;
    if (progress < 0) progress = 0;
    
    return progress;
}

static void updateLevel() {
    for (int i = 7; i >= 0; i--) {
        if (_xp >= XP_PER_LEVEL[i]) {
            _level = i + 1;
            return;
        }
    }
    _level = 1;
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
        _xp = savedXP;
    }
    if (nvs_get_i32(nvs, "level", &savedLevel) == ESP_OK) {
        _level = savedLevel;
    }
    if (nvs_get_u32(nvs, "netsfound", &savedNetworks) == ESP_OK) {
        _networksFound = savedNetworks;
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded from NVS: XP=%d, Level=%d, Networks=%u", (int)_xp, (int)_level, (unsigned)_networksFound);
}

static void saveToNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing");
        return;
    }
    
    nvs_set_i32(nvs, "xp", _xp);
    nvs_set_i32(nvs, "level", _level);
    nvs_set_u32(nvs, "netsfound", _networksFound);
    nvs_set_u32(nvs, "netscnt", (uint32_t)_networks.size());
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Saved to NVS: XP=%d, Level=%d, Networks=%u", (int)_xp, (int)_level, (unsigned)_networks.size());
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

    // Initialize GPS
    initGPS();

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
    updateGPS();

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

    // Start appropriate mode
    switch (mode) {
        case Mode::SNIFF:
            ESP_LOGI(TAG, "Starting SNIFF mode (promiscuous)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xp;
            startSniff();
            break;
        case Mode::SCOUT:
            ESP_LOGI(TAG, "Starting SCOUT mode (active scan)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xp;
            startScout();
            break;
        case Mode::WARDIVE:
            ESP_LOGI(TAG, "Starting WARDIVE mode (passive)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xp;
            startSniff();
            break;
        case Mode::SPECTRUM:
            ESP_LOGI(TAG, "Starting SPECTRUM mode (passive)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xp;
            startSniff();
            break;
        case Mode::BLE_SNIFF:
            ESP_LOGI(TAG, "Starting BLE-SNIFF mode (BLE scan)");
            _sessionStartTime = GetHAL().millis();
            _sessionStartXP = _xp;
            startBLEScan();
            break;
        case Mode::IDLE:
            ESP_LOGI(TAG, "IDLE mode");
            break;
        default:
            break;
    }

    switch (mode) {
        case Mode::SNIFF:
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
        case Mode::BLE_SNIFF:
            _currentMood = Mood::FOCUSED;
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
    stats.xp = _xp;
    stats.level = _level;
    stats.networksFound = _networksFound;
    stats.handshakesCaptured = _handshakesCaptured;
    stats.channelsScanned = _channelsScanned;
    stats.uptimeSeconds = (GetHAL().millis() - _startTime) / 1000;
    
    // Session statistics
    stats.sessionNetworks = _networks.size();
    stats.sessionTimeSeconds = (GetHAL().millis() - _sessionStartTime) / 1000;
    stats.sessionStartTime = _sessionStartTime;
    stats.sessionXPGain = _xp - _sessionStartXP;
    stats.currentChannel = _currentChannel;
    
    // Heap monitoring
    stats.freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (_minHeapSession == 0 || stats.freeHeap < _minHeapSession) {
        _minHeapSession = stats.freeHeap;
    }
    stats.minHeap = _minHeapSession;
    
    // GPS data
    GPSData gps = getGPSData();
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
    // Apply mode-specific multipliers
    float multiplier = 1.0f;
    switch (_currentMode) {
        case Mode::WARDIVE:
            multiplier = 1.5f;  // Active wardriving = bonus XP
            break;
        case Mode::SPECTRUM:
            multiplier = 1.2f;  // Channel analysis is valuable
            break;
        case Mode::SCOUT:
            multiplier = 0.8f;  // Passive scanning = less effort
            break;
        case Mode::IDLE:
            multiplier = 0.0f;  // No XP in idle
            break;
        default:
            multiplier = 1.0f;
            break;
    }
    
    int32_t effectiveAmount = (int32_t)(amount * multiplier);
    int32_t oldXP = _xp;
    _xp += effectiveAmount;
    if (_xp < 0) _xp = 0;
    updateLevel();
    
    // Save to NVS when XP increases (throttled - max once per minute)
    static uint32_t lastSave = 0;
    uint32_t now = GetHAL().millis();
    if (_xp > oldXP && (now - lastSave) > 60000) {
        lastSave = now;
        saveToNVS();
    }
}

std::vector<NetworkInfo> getNetworks() {
    return _networks;
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

}