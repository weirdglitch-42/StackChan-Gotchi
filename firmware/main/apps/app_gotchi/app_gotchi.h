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
    uint32_t _lastNetworkCount = 0;
    uint32_t _lastBLEDeviceCount = 0;
    uint32_t _lastIdleSpeak = 0;
    int _neonFlashCount = 0;
    gotchi::IdleDialogue _idleDialogue;
    int _lastExcitementLevel = 0;
};