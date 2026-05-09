/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>

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

void initGPS();
void updateGPS();
GPSData getGPSData();
bool hasGPSFix();

}