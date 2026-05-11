/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <gotchi/gotchi.h>
#include <vector>
#include <cstdint>

namespace gotchi {

class NetworkDatabase {
public:
    NetworkDatabase();
    
    void clear();
    void clearHandshakes();
    void clearBLEDevices();
    
    // Network operations
    NetworkInfo* findNetwork(const uint8_t* bssid);
    NetworkInfo* addNetwork(const char* ssid, const uint8_t* bssid, int8_t rssi, uint8_t channel);
    void updateNetworkRSSI(const uint8_t* bssid, int8_t rssi);
    const std::vector<NetworkInfo>& getNetworks() const { return _networks; }
    int getNetworkCount() const { return (int)_networks.size(); }
    uint32_t getNetworksFound() const { return _networksFound; }
    void incrementNetworksFound() { _networksFound++; }
    
    // Handshake operations
    bool hasCompleteHandshake(const uint8_t* bssid) const;
    void addHandshake(const HandshakeInfo& hs);
    const std::vector<HandshakeInfo>& getHandshakes() const { return _handshakes; }
    int getHandshakeCount() const;
    void markNetworkHasCapture(const uint8_t* bssid);
    
    // BLE operations
    BLEDeviceInfo* findBLEDevice(const uint8_t* mac);
    BLEDeviceInfo* addBLEDevice(const uint8_t* mac, int8_t rssi, uint8_t advType);
    const std::vector<BLEDeviceInfo>& getBLEDevices() const { return _bleDevices; }
    int getBLEDeviceCount() const { return (int)_bleDevices.size(); }
    
    // Channel analysis
    std::vector<ChannelInfo> getChannelAnalysis() const;
    
    // Persistence
    void loadFromNVS();
    void saveToNVS();

private:
    static const int MAX_NETWORKS = 200;
    static const int MAX_HANDSHAKES = 50;
    static const int MAX_BLE_DEVICES = 100;
    
    std::vector<NetworkInfo> _networks;
    std::vector<HandshakeInfo> _handshakes;
    std::vector<BLEDeviceInfo> _bleDevices;
    uint32_t _networksFound;
};

NetworkDatabase& getNetworkDatabase();

}