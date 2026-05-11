/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "ble_scanner.h"
#include "network_db.h"
#include <esp_log.h>
#include <hal/hal.h>
#include <host/ble_gap.h>

static const char* TAG = "ble_scanner";

static int ble_gap_event_cb(ble_gap_event* event, void* arg);

namespace gotchi {

BLEScanner::BLEScanner() : _scanning(false), _initialized(false) {
}

void BLEScanner::init() {
    if (_initialized) return;
    
    ESP_LOGI(TAG, "Initializing BLE system...");
    GetHAL().startBleServer();
    _initialized = true;
    ESP_LOGI(TAG, "BLE system ready");
}

bool BLEScanner::startScan() {
    if (_scanning) return true;
    
    if (!_initialized) {
        init();
    }
    
    ESP_LOGI(TAG, "Starting BLE scan...");
    
    struct ble_gap_disc_params discParams = {
        .itvl = 0x0010,
        .window = 0x0010,
        .filter_policy = BLE_HCI_SCAN_FILT_NO_WL,
        .passive = 0,
        .filter_duplicates = 1
    };
    
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 30000, &discParams, ble_gap_event_cb, NULL);
    
    if (rc == 0) {
        _scanning = true;
        ESP_LOGI(TAG, "BLE scan started successfully");
        return true;
    } else {
        ESP_LOGE(TAG, "BLE scan failed to start: %d", rc);
        return false;
    }
}

bool BLEScanner::stopScan() {
    if (!_scanning) return true;
    
    ble_gap_disc_cancel();
    _scanning = false;
    ESP_LOGI(TAG, "BLE scan stopped, found %d devices", getNetworkDatabase().getBLEDeviceCount());
    return true;
}

static BLEScanner _bleScanner;

BLEScanner& getBLEScanner() {
    return _bleScanner;
}

}

static int ble_gap_event_cb(ble_gap_event* event, void* arg) {
    (void)arg;
    
    if (event->type == BLE_GAP_EVENT_DISC) {
        const ble_gap_disc_desc* desc = &event->disc;
        
        auto& db = gotchi::getNetworkDatabase();
        
        auto* existing = db.findBLEDevice(desc->addr.val);
        if (existing) {
            existing->rssi = desc->rssi;
            existing->lastSeen = GetHAL().millis();
            
            for (int i = 0; i < desc->length_data; i++) {
                if (desc->data[i] == 0x09 || desc->data[i] == 0x08) {
                    int nameLen = desc->data[i + 1];
                    if (nameLen > 32) nameLen = 32;
                    memcpy(existing->name, &desc->data[i + 2], nameLen);
                    existing->name[nameLen] = '\0';
                    break;
                }
            }
            return 0;
        }
        
        gotchi::BLEDeviceInfo device = {};
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
                bool validName = true;
                for (int j = 0; j < nameLen; j++) {
                    unsigned char c = device.name[j];
                    if (c < 32 || c > 126) { validName = false; break; }
                }
                if (validName) { hasName = true; break; }
            }
        }
        
        if (!hasName) {
            snprintf(device.name, 33, "Unknown_%02X%02X", device.mac[4], device.mac[5]);
        }
        
        db.addBLEDevice(device.mac, device.rssi, device.advType);
        ESP_LOGI(TAG, "BLE: %s RSSI:%d", device.name, device.rssi);
    }
    return 0;
}