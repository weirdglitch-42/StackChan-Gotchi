/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "handshake_parser.h"
#include "network_db.h"
#include <esp_log.h>
#include <hal/hal.h>
#include <string.h>

static const char* TAG = "handshake_parser";

namespace gotchi {

HandshakeParser::HandshakeParser() {
}

void HandshakeParser::clear() {
    _pendingHandshakes.clear();
    _capturedHandshakes.clear();
}

int HandshakeParser::findPendingHandshake(const uint8_t* bssid, const uint8_t* clientMac) {
    for (int i = 0; i < (int)_pendingHandshakes.size(); i++) {
        if (memcmp(_pendingHandshakes[i].bssid, bssid, 6) == 0) {
            if (clientMac == nullptr || memcmp(_pendingHandshakes[i].clientMac, clientMac, 6) == 0) {
                return i;
            }
        }
    }
    return -1;
}

void HandshakeParser::completeHandshake(int idx) {
    if (idx < 0 || idx >= (int)_pendingHandshakes.size()) return;
    
    const auto& pending = _pendingHandshakes[idx];
    
    HandshakeInfo finalHs = {};
    strncpy(finalHs.ssid, pending.ssid, 32);
    memcpy(finalHs.bssid, pending.bssid, 6);
    memcpy(finalHs.clientMac, pending.clientMac, 6);
    finalHs.timestamp = GetHAL().millis();
    finalHs.isComplete = true;
    finalHs.messagesGot = 0x07;
    
    if (_capturedHandshakes.size() >= MAX_CAPTURED) {
        _capturedHandshakes.erase(_capturedHandshakes.begin());
    }
    _capturedHandshakes.push_back(finalHs);
    
    ESP_LOGI(TAG, "Complete handshake captured for %s!", finalHs.ssid);
    
    if (_onHandshakeCaptured) {
        _onHandshakeCaptured(finalHs);
    }
    
    _pendingHandshakes.erase(_pendingHandshakes.begin() + idx);
}

void HandshakeParser::processEapolFrame(uint8_t* payload, int len, const uint8_t* srcMac, const uint8_t* dstMac,
                                       const char* ssid, const uint8_t* bssid, int8_t rssi) {
    if (len < 26) return;
    
    if (payload[0] != 0xAA || payload[1] != 0xAA || payload[2] != 0x03) return;
    if (payload[3] != 0x88 || payload[4] != 0x01) return;
    
    uint8_t* eapol = payload + 4;
    if (eapol[1] != 0x02 && eapol[1] != 0x01) return;
    
    uint16_t keyInfo = (eapol[3] << 8) | eapol[4];
    bool isKeyFrame = (keyInfo & 0x01) != 0;
    bool hasKeyMic = (keyInfo & 0x100) != 0;
    bool hasKeyData = (keyInfo & 0x200) != 0;
    bool isFromAP = (keyInfo & 0x0400) != 0;
    
    if (!isKeyFrame) return;
    
    uint16_t keyDataLen = (eapol[6] << 8) | eapol[7];
    if (keyDataLen > len - 8) return;
    
    char targetSSID[33] = {0};
    if (ssid && strlen(ssid) > 0) {
        strncpy(targetSSID, ssid, 32);
    } else {
        auto& db = getNetworkDatabase();
        for (const auto& net : db.getNetworks()) {
            if (memcmp(net.bssid, bssid, 6) == 0) {
                strncpy(targetSSID, net.ssid, 32);
                break;
            }
        }
    }
    
    PendingHandshake hs = {};
    memcpy(hs.bssid, bssid, 6);
    memcpy(hs.clientMac, isFromAP ? dstMac : srcMac, 6);
    strncpy(hs.ssid, targetSSID, 32);
    hs.lastSeen = GetHAL().millis();
    hs.hasM1 = false;
    hs.hasM2 = false;
    hs.hasM3 = false;
    hs.hasM4 = false;
    
    uint8_t* keyData = eapol + 8;
    
    bool hasPMKID = false;
    if (keyDataLen >= 4 && keyData[0] == 0xDD && keyData[1] == 0x16) {
        for (int i = 0; i < keyDataLen - 8; i++) {
            if (keyData[i] == 0xDD && keyData[i+1] == 0x14) {
                hasPMKID = true;
                ESP_LOGI(TAG, "Detected PMKID for %s!", targetSSID);
                break;
            }
        }
    }
    
    if (!isFromAP && !hasKeyMic && hasKeyData) {
        hs.hasM1 = true;
        if (keyDataLen >= 32) {
            memcpy(hs.anonce, keyData + 2, 32);
        }
    } else if (isFromAP && hasKeyMic && hasKeyData) {
        hs.hasM3 = true;
        hs.hasM1 = true;
    } else if (!isFromAP && hasKeyMic && !hasKeyData) {
        if (hasPMKID) {
            hs.hasM3 = true;
        }
    }
    
    int idx = findPendingHandshake(bssid, hs.clientMac);
    if (idx >= 0) {
        if (hs.hasM1) _pendingHandshakes[idx].hasM1 = true;
        if (hs.hasM2) _pendingHandshakes[idx].hasM2 = true;
        if (hs.hasM3) _pendingHandshakes[idx].hasM3 = true;
        if (hs.hasM4) _pendingHandshakes[idx].hasM4 = true;
        _pendingHandshakes[idx].lastSeen = GetHAL().millis();
    } else if (_pendingHandshakes.size() < MAX_PENDING) {
        _pendingHandshakes.push_back(hs);
        idx = _pendingHandshakes.size() - 1;
    }
    
    if (idx >= 0 && _pendingHandshakes[idx].hasM1 && 
        (_pendingHandshakes[idx].hasM2 || _pendingHandshakes[idx].hasM3)) {
        getNetworkDatabase().markNetworkHasCapture(bssid);
        completeHandshake(idx);
    }
}

static HandshakeParser _handshakeParser;

HandshakeParser& getHandshakeParser() {
    return _handshakeParser;
}

}