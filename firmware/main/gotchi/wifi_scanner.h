/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <esp_err.h>
#include <esp_wifi.h>
#include <hal/hal.h>

namespace gotchi {

class WifiScanner {
public:
    WifiScanner();
    
    bool init();
    bool isInitialized() const { return _initialized; }
    bool isSniffing() const { return _sniffing; }
    uint8_t getCurrentChannel() const { return _currentChannel; }
    uint32_t getChannelsScanned() const { return _channelsScanned; }
    
    bool startSniff();
    bool stopSniff();
    
    bool startScan();
    bool stopScan();
    
    void update();
    void setHopInterval(uint32_t intervalMs);
    void setChannelHopEnabled(bool enabled);

private:
    static void wifiSniffCallback(void* buf, wifi_promiscuous_pkt_type_t type);
    static int ieee80211_hdrlen(uint16_t fc);
    void processBeaconFrame(uint8_t* payload, int len, int8_t rssi, uint8_t channel);
    void processDataFrame(uint8_t* payload, int len, int8_t rssi);
    
    bool _initialized;
    bool _sniffing;
    bool _scanning;
    uint8_t _currentChannel;
    uint32_t _channelsScanned;
    uint32_t _lastChannelHop;
    uint32_t _hopIntervalMs;
    bool _channelHopEnabled;
    uint8_t _hopIndex;
};

WifiScanner& getWifiScanner();

}