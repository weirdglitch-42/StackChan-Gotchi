/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "gps.h"
#include <esp_log.h>
#include <esp_err.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <hal/hal.h>
#include <string.h>
#include <ctype.h>

static const char* TAG = "gotchi_gps";

namespace gotchi {

//=============================================================================
// GpsManager Class Implementation
//=============================================================================
GpsManager::GpsManager() : _initialized(false), _bufferPos(0) {}

void GpsManager::init() {
    if (_initialized) return;
    
    ESP_LOGI(TAG, "Initializing GPS...");
    
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(GPS_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART param config failed: %d", ret);
        return;
    }
    
    ret = uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART set pin failed: %d", ret);
        return;
    }
    
    ret = uart_driver_install(GPS_UART_NUM, GPS_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "UART driver install failed: %d", ret);
        return;
    }
    
    _initialized = true;
    ESP_LOGI(TAG, "GPS initialized");
}

void GpsManager::update() {
    if (!_initialized) return;
    
    uint8_t data[128];
    int len = uart_read_bytes(GPS_UART_NUM, data, sizeof(data) - 1, 0);
    
    for (int i = 0; i < len; i++) {
        if (data[i] == '\n') {
            _gpsBuffer[_bufferPos] = '\0';
            if (_bufferPos > 6 && _gpsBuffer[0] == '$') {
                parseNMEASentence((const char*)_gpsBuffer);
            }
            _bufferPos = 0;
        } else if (data[i] != '\r' && _bufferPos < GPS_BUF_SIZE - 1) {
            _gpsBuffer[_bufferPos++] = data[i];
        }
    }
}

GPSData GpsManager::getData() {
    return _gpsData;
}

bool GpsManager::hasFix() {
    return _gpsData.valid && _gpsData.fixQuality > 0;
}

void GpsManager::parseNMEASentence(const char* sentence) {
    if (!validateNMEA(sentence)) return;
    
    const char* fields[32];
    int fieldCount = 0;
    
    fields[fieldCount++] = sentence;
    for (const char* p = sentence; *p && fieldCount < 32; p++) {
        if (*p == ',') {
            fields[fieldCount++] = p + 1;
        }
    }
    
    if (fieldCount < 3) return;
    
    if (strncmp(sentence + 3, "GGA", 3) == 0) {
        parseGGA(fields, fieldCount);
    } else if (strncmp(sentence + 3, "RMC", 3) == 0) {
        parseRMC(fields, fieldCount);
    }
}

double GpsManager::parseNMEAfloat(const char* s) {
    if (!s || *s == '\0') return 0;
    return atof(s);
}

int GpsManager::parseNMEAint(const char* s) {
    if (!s || *s == '\0') return 0;
    return atoi(s);
}

uint8_t GpsManager::nmeaChecksum(const char* s) {
    uint8_t sum = 0;
    if (*s == '$') s++;
    while (*s && *s != '*') {
        sum ^= *s++;
    }
    return sum;
}

bool GpsManager::validateNMEA(const char* sentence) {
    const char* star = strchr(sentence, '*');
    if (!star) return false;
    
    char receivedChecksum[3] = {0};
    if (strlen(star) < 3) return false;
    receivedChecksum[0] = star[1];
    receivedChecksum[1] = star[2];
    
    char calculated[3];
    snprintf(calculated, sizeof(calculated), "%02X", nmeaChecksum(sentence));
    
    return (receivedChecksum[0] == calculated[0] && receivedChecksum[1] == calculated[1]);
}

void GpsManager::parseGGA(const char* fields[], int count) {
    if (count < 10) return;
    
    if (fields[6] && *fields[6] != '0') {
        _gpsData.fixQuality = parseNMEAint(fields[6]);
    }
    
    if (fields[7] && *fields[7]) {
        _gpsData.satellites = parseNMEAint(fields[7]);
    }
    
    if (fields[9] && *fields[9]) {
        _gpsData.altitude = parseNMEAfloat(fields[9]);
    }
    
    if (count >= 11 && fields[2] && *fields[2] && fields[3] && *fields[3]) {
        double lat = parseNMEAfloat(fields[2]);
        double latDir = (*fields[3] == 'S') ? -1.0 : 1.0;
        
        int deg = (int)(lat / 100);
        double min = lat - deg * 100;
        _gpsData.latitude = (deg + min / 60.0) * latDir;
    }
    
    if (count >= 13 && fields[4] && *fields[4] && fields[5] && *fields[5]) {
        double lon = parseNMEAfloat(fields[4]);
        double lonDir = (*fields[5] == 'W') ? -1.0 : 1.0;
        
        int deg = (int)(lon / 100);
        double min = lon - deg * 100;
        _gpsData.longitude = (deg + min / 60.0) * lonDir;
    }
    
    _gpsData.valid = (_gpsData.fixQuality > 0);
    _gpsData.timestamp = GetHAL().millis();
}

void GpsManager::parseRMC(const char* fields[], int count) {
    if (count < 10) return;
    
    if (fields[2] && *fields[2] == 'A') {
        _gpsData.valid = true;
    }
    
    if (fields[7] && *fields[7]) {
        float speedKnots = parseNMEAfloat(fields[7]);
        _gpsData.speed = speedKnots * 1.852f;
    }
}

//=============================================================================
// Global Instance
//=============================================================================
static GpsManager _gpsManager;

GpsManager& getGpsManager() {
    return _gpsManager;
}

}