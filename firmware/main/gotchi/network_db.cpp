/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "network_db.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <string.h>

static const char* TAG = "network_db";

namespace gotchi {

NetworkDatabase::NetworkDatabase() : _networksFound(0) {
}

void NetworkDatabase::clear() {
    _networks.clear();
}

void NetworkDatabase::clearHandshakes() {
    _handshakes.clear();
}

void NetworkDatabase::clearBLEDevices() {
    _bleDevices.clear();
}

NetworkInfo* NetworkDatabase::findNetwork(const uint8_t* bssid) {
    for (auto& net : _networks) {
        if (memcmp(net.bssid, bssid, 6) == 0) {
            return &net;
        }
    }
    return nullptr;
}

NetworkInfo* NetworkDatabase::addNetwork(const char* ssid, const uint8_t* bssid, int8_t rssi, uint8_t channel) {
    if (_networks.size() >= MAX_NETWORKS) {
        return nullptr;
    }
    
    if (channel < 1 || channel > 14) {
        channel = 1;
    }
    
    NetworkInfo net;
    memset(net.ssid, 0, 33);
    if (ssid) {
        strncpy(net.ssid, ssid, 32);
    }
    memcpy(net.bssid, bssid, 6);
    net.rssi = rssi;
    net.channel = channel;
    net.isHidden = false;
    net.hasCapture = false;
    net.lastSeen = 0;
    
    _networks.push_back(net);
    _networksFound++;
    
    return &_networks.back();
}

void NetworkDatabase::updateNetworkRSSI(const uint8_t* bssid, int8_t rssi) {
    NetworkInfo* net = findNetwork(bssid);
    if (net) {
        net->rssi = rssi;
    }
}

bool NetworkDatabase::hasCompleteHandshake(const uint8_t* bssid) const {
    for (const auto& hs : _handshakes) {
        if (hs.isComplete && memcmp(hs.bssid, bssid, 6) == 0) {
            return true;
        }
    }
    return false;
}

void NetworkDatabase::addHandshake(const HandshakeInfo& hs) {
    if (_handshakes.size() >= MAX_HANDSHAKES) {
        _handshakes.erase(_handshakes.begin());
    }
    _handshakes.push_back(hs);
}

int NetworkDatabase::getHandshakeCount() const {
    int count = 0;
    for (const auto& hs : _handshakes) {
        if (hs.isComplete) count++;
    }
    return count;
}

void NetworkDatabase::markNetworkHasCapture(const uint8_t* bssid) {
    for (auto& net : _networks) {
        if (memcmp(net.bssid, bssid, 6) == 0) {
            net.hasCapture = true;
            break;
        }
    }
}

BLEDeviceInfo* NetworkDatabase::findBLEDevice(const uint8_t* mac) {
    for (auto& dev : _bleDevices) {
        if (memcmp(dev.mac, mac, 6) == 0) {
            return &dev;
        }
    }
    return nullptr;
}

BLEDeviceInfo* NetworkDatabase::addBLEDevice(const uint8_t* mac, int8_t rssi, uint8_t advType) {
    if (_bleDevices.size() >= MAX_BLE_DEVICES) {
        return nullptr;
    }
    
    BLEDeviceInfo dev = {};
    memcpy(dev.mac, mac, 6);
    dev.rssi = rssi;
    dev.advType = advType;
    dev.lastSeen = 0;
    dev.name[0] = '\0';
    
    _bleDevices.push_back(dev);
    
    return &_bleDevices.back();
}

std::vector<ChannelInfo> NetworkDatabase::getChannelAnalysis() const {
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

void NetworkDatabase::loadFromNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved network data in NVS");
        return;
    }
    
    uint32_t savedNetworks = 0;
    if (nvs_get_u32(nvs, "netsfound", &savedNetworks) == ESP_OK) {
        _networksFound = savedNetworks;
    }
    
    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded from NVS: NetworksFound=%u", (unsigned)_networksFound);
}

void NetworkDatabase::saveToNVS() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("gotchi", NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for writing network data");
        return;
    }
    
    nvs_set_u32(nvs, "netsfound", _networksFound);
    nvs_set_u32(nvs, "netscnt", (uint32_t)_networks.size());
    nvs_commit(nvs);
    nvs_close(nvs);
    
    ESP_LOGI(TAG, "Saved to NVS: NetworksFound=%u, Networks=%u", 
        (unsigned)_networksFound, (unsigned)_networks.size());
}

static NetworkDatabase _networkDatabase;

NetworkDatabase& getNetworkDatabase() {
    return _networkDatabase;
}

}