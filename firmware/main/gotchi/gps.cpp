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

static GPSData _gpsData;
static bool _gpsInitialized = false;

static const uart_port_t GPS_UART_NUM = UART_NUM_2;
static const int GPS_TX_PIN = GPIO_NUM_14;
static const int GPS_RX_PIN = GPIO_NUM_13;
static const int GPS_BUF_SIZE = 512;

static uint8_t _gpsBuffer[GPS_BUF_SIZE];
static int _bufferPos = 0;

static double parseNMEAfloat(const char* s) {
    if (!s || *s == '\0') return 0;
    return atof(s);
}

static int parseNMEAint(const char* s) {
    if (!s || *s == '\0') return 0;
    return atoi(s);
}

static uint8_t nmeaChecksum(const char* s) {
    uint8_t sum = 0;
    if (*s == '$') s++;
    while (*s && *s != '*') {
        sum ^= *s++;
    }
    return sum;
}

static bool validateNMEA(const char* sentence) {
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

static void parseGGA(const char* fields[], int count) {
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
        
        int latDeg = (int)(lat / 100);
        double latMin = lat - latDeg * 100;
        _gpsData.latitude = (latDeg + latMin / 60.0) * latDir;
    }
    
    if (count >= 13 && fields[4] && *fields[4] && fields[5] && *fields[5]) {
        double lon = parseNMEAfloat(fields[4]);
        double lonDir = (*fields[5] == 'W') ? -1.0 : 1.0;
        
        int lonDeg = (int)(lon / 100);
        double lonMin = lon - lonDeg * 100;
        _gpsData.longitude = (lonDeg + lonMin / 60.0) * lonDir;
    }
    
    if (_gpsData.fixQuality > 0 && _gpsData.latitude != 0 && _gpsData.longitude != 0) {
        _gpsData.valid = true;
        _gpsData.timestamp = GetHAL().millis();
    }
}

static void parseRMC(const char* fields[], int count) {
    if (count < 10) return;
    
    if (fields[2] && *fields[2] == 'A') {
        _gpsData.valid = true;
    }
    
    if (fields[7] && *fields[7]) {
        float speedKnots = parseNMEAfloat(fields[7]);
        _gpsData.speed = speedKnots * 0.514444f;
    }
    
    if (fields[8] && *fields[8]) {
        // Course (heading) - available for future use
        // float course = parseNMEAfloat(fields[8]);
    }
}

static void parseNMEASentence(const char* sentence) {
    if (!validateNMEA(sentence)) return;
    
    static const int MAX_FIELDS = 20;
    const char* fields[MAX_FIELDS];
    int fieldCount = 0;
    
    fields[fieldCount++] = sentence + 1;
    
    for (int i = 1; i < MAX_FIELDS - 1 && fieldCount < MAX_FIELDS; i++) {
        const char* comma = strchr(fields[i-1], ',');
        if (!comma) break;
        fields[i] = comma + 1;
        fieldCount = i + 1;
    }
    
    const char* type = fields[0];
    if (strncmp(type, "GGA", 3) == 0) {
        parseGGA(fields, fieldCount);
    } else if (strncmp(type, "RMC", 3) == 0) {
        parseRMC(fields, fieldCount);
    }
}

static void processGPSChar(char c) {
    if (c == '\n' || c == '\r') {
        if (_bufferPos > 0) {
            _gpsBuffer[_bufferPos] = '\0';
            if (_gpsBuffer[0] == '$') {
                parseNMEASentence((const char*)_gpsBuffer);
            }
            _bufferPos = 0;
        }
    } else if (_bufferPos < GPS_BUF_SIZE - 1) {
        _gpsBuffer[_bufferPos++] = c;
    }
}

void initGPS() {
    if (_gpsInitialized) return;
    
    ESP_LOGI(TAG, "Initializing GPS on UART2...");
    
    uart_config_t uartConfig = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(GPS_UART_NUM, &uartConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART config failed: %d", ret);
        return;
    }
    
    ret = uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART pin config failed: %d", ret);
        return;
    }
    
    ret = uart_driver_install(GPS_UART_NUM, GPS_BUF_SIZE * 2, GPS_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %d", ret);
        return;
    }
    
    _gpsInitialized = true;
    ESP_LOGI(TAG, "GPS initialized successfully");
}

void updateGPS() {
    if (!_gpsInitialized) return;
    
    uint8_t data[64];
    int len = uart_read_bytes(GPS_UART_NUM, data, sizeof(data) - 1, 0);
    
    for (int i = 0; i < len; i++) {
        processGPSChar((char)data[i]);
    }
}

GPSData getGPSData() {
    return _gpsData;
}

bool hasGPSFix() {
    return _gpsData.valid && _gpsData.fixQuality > 0;
}

}