/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <memory>
#include <mutex>
#include <gotchi/gotchi.h>
#include <gotchi/idle_dialogue.h>
#include <stackchan/modifiers/speaking.h>
#include <stackchan/modifiers/timed.h>
#include <uitk/short_namespace.hpp>

class AppGotchi : public mooncake::AppAbility {
public:
    AppGotchi();

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void initGotchi();
    void updateGotchi();
    void handleInput();
    void updateAvatar();
    void updateHeadAnimation();
    void updateNeonLights();
    void cycleMode();
    void cycleModeBackward();
    void renderUI();
    void renderIdleDialogue();
    void createSpectrumLabels();
    void updateSpectrumLabels();
    void destroySpectrumLabels();
    void createNetworkBars();
    void updateNetworkBars();
    void destroyNetworkBars();
    void createSignalBars();
    void updateSignalBars();
    void destroySignalBars();
    void createBLELabels();
    void updateBLELabels();
    void destroyBLELabels();
    void createHeaderBoxes();
    void updateHeaderBoxes();
    void destroyHeaderBoxes();

    std::mutex _mutex;
    bool _isRunning = false;
    uint32_t _lastUpdate = 0;
    uint32_t _lastHeadAnim = 0;
    uint32_t _lastModeChange = 0;
    uint32_t _pressStartTime = 0;
    int _pressX = 0;
    int _pressY = 0;
    
    gotchi::Mode _currentMode = gotchi::Mode::IDLE;
    gotchi::Mode _lastLoggedMode = gotchi::Mode::IDLE;
    int _headYawOffset = 0;
    int _headPitchOffset = 0;
    int16_t _lastTargetYaw = 0;
    int16_t _lastTargetPitch = 0;
    bool _touchTracking = false;

    std::unique_ptr<uitk::lvgl_cpp::Label> _statsLabel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _networkListLabel;
    
    // SPECTRUM mode channel heatmap labels (14 channels)
    static constexpr int NUM_CHANNELS = 14;
    std::unique_ptr<uitk::lvgl_cpp::Label> _spectrumLabels[NUM_CHANNELS];
    bool _spectrumLabelsCreated = false;
    
    // SCOUT/HUNT mode network signal bars (8 networks)
    static constexpr int MAX_NETWORK_BARS = 8;
    std::unique_ptr<uitk::lvgl_cpp::Label> _networkBars[MAX_NETWORK_BARS];
    bool _networkBarsCreated = false;
    
    // WARDIVE mode signal distribution (3 categories)
    static constexpr int NUM_SIGNAL_BARS = 3;
    std::unique_ptr<uitk::lvgl_cpp::Label> _signalBars[NUM_SIGNAL_BARS];
    bool _signalBarsCreated = false;
    
    // BLE_SCAN mode device list (10 devices)
    static constexpr int MAX_BLE_DEVICES = 10;
    std::unique_ptr<uitk::lvgl_cpp::Label> _bleLabels[MAX_BLE_DEVICES];
    bool _bleLabelsCreated = false;
    
    // Header boxes (4 boxes for each mode's header)
    static constexpr int NUM_HEADER_BOXES = 4;
    std::unique_ptr<uitk::lvgl_cpp::Label> _headerBoxes[NUM_HEADER_BOXES];
    bool _headerBoxesCreated = false;
    
    // WARDIVE signal cycling
    uint32_t _wardiveCycleTime = 0;
    int _wardiveCurrentNetworkIndex = 0;
    
    uint32_t _lastNetworkCount = 0;
    uint32_t _lastBLEDeviceCount = 0;
    uint32_t _lastIdleSpeak = 0;
    int _neonFlashCount = 0;
    gotchi::IdleDialogue _idleDialogue;
    int _lastExcitementLevel = 0;
};