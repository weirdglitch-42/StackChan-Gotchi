/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <driver/uart.h>
#include <driver/gpio.h>

namespace gotchi {

struct GPSData {
    double latitude;
    double longitude;
    float speed;
    float altitude;
    uint8_t satellites;
    uint8_t fixQuality;
    bool valid;
    uint32_t timestamp;
    
    GPSData() : latitude(0), longitude(0), speed(0), altitude(0),
                satellites(0), fixQuality(0), valid(false), timestamp(0) {}
};

class GpsManager {
public:
    GpsManager();
    
    void init();
    void update();
    GPSData getData();
    bool hasFix();

private:
    void parseNMEASentence(const char* sentence);
    double parseNMEAfloat(const char* s);
    int parseNMEAint(const char* s);
    uint8_t nmeaChecksum(const char* s);
    bool validateNMEA(const char* sentence);
    void parseGGA(const char* fields[], int count);
    void parseRMC(const char* fields[], int count);
    
    static const uart_port_t GPS_UART_NUM = UART_NUM_2;
    static const int GPS_TX_PIN = GPIO_NUM_14;
    static const int GPS_RX_PIN = GPIO_NUM_13;
    static const int GPS_BUF_SIZE = 512;
    
    GPSData _gpsData;
    bool _initialized;
    uint8_t _gpsBuffer[GPS_BUF_SIZE];
    int _bufferPos;
};

// Global instance for easy access
GpsManager& getGpsManager();

}