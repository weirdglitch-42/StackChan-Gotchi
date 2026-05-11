/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <cstdint>
#include <gotchi/gotchi.h>

namespace gotchi {

class BLEScanner {
public:
    BLEScanner();
    
    bool isScanning() const { return _scanning; }
    
    bool startScan();
    bool stopScan();
    
    void init();

private:
    bool _scanning;
    bool _initialized;
};

BLEScanner& getBLEScanner();

}