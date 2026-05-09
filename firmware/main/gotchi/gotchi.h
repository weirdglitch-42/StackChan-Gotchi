/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

// Forward declaration (avoid circular include with storage.h)
namespace gotchi {
struct GotchiConfig;
}

namespace gotchi {

enum class Mode {
    IDLE,
    SNIFF,
    SCOUT,
    WARDIVE,
    SPECTRUM,
    BLE_SNIFF
};

enum class Mood {
    NEUTRAL,
    HAPPY,
    EXCITED,
    SLEEPY,
    FOCUSED,
    SAD
};

struct NetworkInfo {
    char ssid[33];  // Fixed size to avoid heap in WiFi callback
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    bool isHidden;
    bool hasCapture;
    uint32_t lastSeen;
};

struct HandshakeInfo {
    char ssid[33];
    uint8_t bssid[6];
    uint8_t clientMac[6];
    uint32_t timestamp;
    uint8_t messagesGot;      // Which EAPOL messages we've captured (bitmask: M1=1, M2=2, M3=4, M4=8)
    bool isComplete;         // Full 4-way handshake captured
    uint8_t snonce[32];     // Server nonce (ANonce)
    uint8_t cnonce[32];     // Client nonce (SNonce)
    uint8_t mic[16];        // Message integrity code
};

struct BLEDeviceInfo {
    char name[33];           // Device name or "Unknown"
    uint8_t mac[6];          // BLE MAC address
    int8_t rssi;             // Signal strength
    uint8_t advType;         // Advertising type
    uint32_t lastSeen;       // Timestamp
};

void addHandshake(const HandshakeInfo& hs);
std::vector<HandshakeInfo> getHandshakes();
int getHandshakeCount();
bool hasCompleteHandshake(const uint8_t* bssid);

struct Stats {
    int32_t xp;
    int32_t level;
    uint32_t networksFound;
    uint32_t handshakesCaptured;
    uint32_t channelsScanned;
    uint32_t uptimeSeconds;
    
    // Session statistics (reset on reboot)
    uint32_t sessionNetworks;      // Networks found this session
    uint32_t sessionTimeSeconds;   // Time in current mode
    uint32_t sessionStartTime;     // When current mode started
    uint32_t sessionXPGain;        // XP earned this session
    uint8_t currentChannel;        // Current WiFi channel
    int32_t freeHeap;              // Current heap in bytes
    int32_t minHeap;               // Minimum heap this session
    
    // GPS data
    bool gpsValid;
    uint8_t gpsSatellites;
    double gpsLat;
    double gpsLon;
};

struct ChannelInfo {
    uint8_t channel;
    uint8_t networkCount;
    int8_t maxRssi;
    int8_t avgRssi;
};

std::vector<ChannelInfo> getChannelAnalysis();

bool hasStorage();

const char* getModeName(Mode mode);
const char* getLevelTitle(int level);
int getXPForLevel(int level);
int getXPProgress(int32_t xp, int level);  // Returns progress to next level as percentage (0-100)

void init();
void update();
void shutdown();

void setMode(Mode mode);
Mode getCurrentMode();

void setMood(Mood mood);
Mood getCurrentMood();

Stats getStats();
GotchiConfig getConfig();
void addXP(int32_t amount);

std::vector<NetworkInfo> getNetworks();

void startSniff();
void stopSniff();
void startScout();
void stopScout();

bool isSniffing();

std::vector<BLEDeviceInfo> getBLEDevices();
void startBLEScan();
void stopBLEScan();
int getBLEDeviceCount();

}