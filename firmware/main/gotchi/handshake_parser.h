/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <gotchi/gotchi.h>
#include <vector>
#include <cstdint>
#include <functional>

namespace gotchi {

class HandshakeParser {
public:
    HandshakeParser();
    
    void clear();
    
    using HandshakeCallback = std::function<void(const HandshakeInfo& hs)>;
    void setOnHandshakeCaptured(HandshakeCallback cb) { _onHandshakeCaptured = cb; }
    
    void processEapolFrame(uint8_t* payload, int len, const uint8_t* srcMac, const uint8_t* dstMac,
                           const char* ssid, const uint8_t* bssid, int8_t rssi);
    
    const std::vector<HandshakeInfo>& getCapturedHandshakes() const { return _capturedHandshakes; }
    int getCapturedCount() const { return (int)_capturedHandshakes.size(); }

private:
    struct PendingHandshake {
        uint8_t bssid[6];
        uint8_t clientMac[6];
        char ssid[33];
        bool hasM1;
        bool hasM2;
        bool hasM3;
        bool hasM4;
        uint8_t anonce[32];
        uint8_t snonce[32];
        uint8_t mic[16];
        uint32_t lastSeen;
    };
    
    int findPendingHandshake(const uint8_t* bssid, const uint8_t* clientMac);
    void completeHandshake(int idx);
    
    static const int MAX_PENDING = 50;
    static const int MAX_CAPTURED = 50;
    
    std::vector<PendingHandshake> _pendingHandshakes;
    std::vector<HandshakeInfo> _capturedHandshakes;
    HandshakeCallback _onHandshakeCaptured;
};

HandshakeParser& getHandshakeParser();

}