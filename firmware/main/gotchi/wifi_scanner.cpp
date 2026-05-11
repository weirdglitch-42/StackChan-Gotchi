/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "wifi_scanner.h"
#include "network_db.h"
#include "handshake_parser.h"
#include <esp_wifi.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

static const char* TAG = "wifi_scanner";

namespace gotchi {

static const uint8_t CHANNEL_SEQ[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
static const int CHANNEL_SEQ_LEN = 13;

WifiScanner::WifiScanner() 
    : _initialized(false), _sniffing(false), _scanning(false),
      _currentChannel(1), _channelsScanned(0), _lastChannelHop(0),
      _hopIntervalMs(500), _channelHopEnabled(true), _hopIndex(0) {
}

bool WifiScanner::init() {
    if (_initialized) return true;
    
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %d", ret);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi mode set failed: %d", ret);
        esp_wifi_deinit();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    
    _initialized = true;
    ESP_LOGI(TAG, "WiFi initialized");
    return true;
}

int WifiScanner::ieee80211_hdrlen(uint16_t fc) {
    int hdrlen = 24;
    uint8_t type = (fc >> 2) & 0x3;
    if (type == 2) {
        if (fc & 0x0080) hdrlen += 2;
    }
    if (fc & 0x8000) hdrlen += 4;
    return hdrlen;
}

void WifiScanner::processBeaconFrame(uint8_t* payload, int len, int8_t rssi, uint8_t channel) {
    if (len < 36) return;
    
    int pos = 36;
    while (pos < len - 2) {
        uint8_t tag = payload[pos];
        uint8_t tag_len = payload[pos + 1];
        
        if (tag == 0x00) {
            if (tag_len > 0 && tag_len < 33) {
                uint8_t bssid[6];
                memcpy(bssid, &payload[10], 6);
                
                auto& db = getNetworkDatabase();
                auto* existing = db.findNetwork(bssid);
                if (existing) {
                    existing->rssi = rssi;
                    existing->lastSeen = GetHAL().millis();
                } else {
                    char ssid[33] = {0};
                    memcpy(ssid, &payload[pos + 2], tag_len);
                    db.addNetwork(ssid, bssid, rssi, channel);
                    ESP_LOGI(TAG, "Found: %.32s (ch:%d rssi:%d)", ssid, channel, rssi);
                }
            }
            break;
        }
        pos += tag_len + 2;
    }
}

void WifiScanner::processDataFrame(uint8_t* payload, int len, int8_t rssi) {
    if (len < 24) return;
    
    uint16_t fc = payload[0] | (payload[1] << 8);
    int hdrlen = ieee80211_hdrlen(fc);
    
    if (len < hdrlen + 8) return;
    
    bool toDs = (fc & 0x0100) != 0;
    bool fromDs = (fc & 0x0200) != 0;
    
    int offset = hdrlen;
    if (toDs && fromDs) offset += 6;
    
    uint8_t subtype = (fc >> 4) & 0x0F;
    bool isQoS = (subtype & 0x08) != 0;
    if (isQoS) offset += 2;
    
    if (isQoS && (payload[1] & 0x80)) offset += 4;
    
    if (offset + 8 > len) return;
    
    if (payload[offset] == 0xAA && payload[offset+1] == 0xAA &&
        payload[offset+2] == 0x03 && payload[offset+3] == 0x00 &&
        payload[offset+4] == 0x00 && payload[offset+5] == 0x00 &&
        payload[offset+6] == 0x88 && payload[offset+7] == 0x8E) {
        
        uint8_t srcMac[6], dstMac[6];
        memcpy(srcMac, &payload[10], 6);
        memcpy(dstMac, &payload[4], 6);
        
        uint8_t bssid[6];
        memcpy(bssid, &payload[16], 6);
        
        char ssid[33] = {0};
        auto& db = getNetworkDatabase();
        for (const auto& net : db.getNetworks()) {
            if (memcmp(net.bssid, bssid, 6) == 0) {
                strncpy(ssid, net.ssid, 32);
                break;
            }
        }
        
        getHandshakeParser().processEapolFrame(payload + offset + 8, len - offset - 8, 
                                                srcMac, dstMac, ssid, bssid, rssi);
    }
}

void WifiScanner::wifiSniffCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (buf == nullptr) return;
    
    auto& scanner = getWifiScanner();
    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    int8_t rssi = pkt->rx_ctrl.rssi;
    uint8_t channel = pkt->rx_ctrl.channel;
    
    if (len < 24) return;
    
    if (type == WIFI_PKT_MGMT) {
        if ((payload[0] & 0xFC) != 0x80) return;
        scanner.processBeaconFrame(payload, len, rssi, channel);
    }
    else if (type == WIFI_PKT_DATA) {
        scanner.processDataFrame(payload, len, rssi);
    }
}

bool WifiScanner::startSniff() {
    if (!_initialized) {
        if (!init()) return false;
    }
    
    if (_sniffing) return true;
    
    ESP_LOGI(TAG, "Starting sniff mode...");
    
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    esp_wifi_set_promiscuous_rx_cb(wifiSniffCallback);
    
    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous(true);
    
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    _lastChannelHop = GetHAL().millis();
    _hopIndex = 0;
    
    _sniffing = true;
    ESP_LOGI(TAG, "Sniff mode started");
    return true;
}

bool WifiScanner::stopSniff() {
    if (!_sniffing) return true;
    
    esp_wifi_set_promiscuous(false);
    _sniffing = false;
    ESP_LOGI(TAG, "Sniff mode stopped");
    return true;
}

bool WifiScanner::startScan() {
    if (!_initialized) {
        if (!init()) return false;
    }
    
    if (_scanning) return true;
    
    ESP_LOGI(TAG, "Starting active scan...");
    
    wifi_scan_config_t scan_config = {};
    scan_config.show_hidden = true;
    esp_wifi_scan_start(&scan_config, false);
    
    _scanning = true;
    _sniffing = true;
    return true;
}

bool WifiScanner::stopScan() {
    if (!_scanning) return true;
    
    esp_wifi_scan_stop();
    _scanning = false;
    _sniffing = false;
    ESP_LOGI(TAG, "Scan stopped");
    return true;
}

void WifiScanner::update() {
    if (!_sniffing || !_channelHopEnabled) return;
    
    uint32_t now = GetHAL().millis();
    if ((now - _lastChannelHop) > _hopIntervalMs) {
        _hopIndex = (_hopIndex + 1) % CHANNEL_SEQ_LEN;
        _currentChannel = CHANNEL_SEQ[_hopIndex];
        esp_wifi_set_channel(_currentChannel, WIFI_SECOND_CHAN_NONE);
        _channelsScanned++;
        _lastChannelHop = now;
    }
}

void WifiScanner::setHopInterval(uint32_t intervalMs) {
    _hopIntervalMs = intervalMs;
}

void WifiScanner::setChannelHopEnabled(bool enabled) {
    _channelHopEnabled = enabled;
}

static WifiScanner _wifiScanner;

WifiScanner& getWifiScanner() {
    return _wifiScanner;
}

}