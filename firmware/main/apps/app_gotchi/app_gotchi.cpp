/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "app_gotchi.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <hal/hal.h>
#include <hal/board/hal_bridge.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>
#include <stackchan/avatar/skins/default/default.h>
#include <apps/common/common.h>
#include <assets/assets.h>
#include <gotchi/gotchi.h>
#include <gotchi/storage.h>
#include <gotchi/mode.h>

using namespace mooncake;
using namespace stackchan;

AppGotchi::AppGotchi() {
    setAppInfo().name = "GOTCHI";
    static auto icon = assets::get_image("icon_dance.bin");
    setAppInfo().icon = (void*)&icon;
    static uint32_t theme_color = 0x00FF88;
    setAppInfo().userData = (void*)&theme_color;
}

void AppGotchi::onCreate() {
    mclog::tagInfo(getAppInfo().name, "on create");
}

void AppGotchi::onOpen() {
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;

    std::unique_ptr<view::LoadingPage> loading_page;
    loading_page = std::make_unique<view::LoadingPage>(0x00FF88, 0x003320);
    loading_page->setMessage("Waking up\nGotchi...");

    initGotchi();

    loading_page.reset();

    auto avatar = std::make_unique<avatar::DefaultAvatar>();
    avatar->init(lv_screen_active());
    GetStackChan().attachAvatar(std::move(avatar));

    view::create_home_indicator([&]() { close(); }, 0x00FF88, 0x003320);
    view::create_status_bar(0x00FF88, 0x003320);

    _statsLabel = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
    _statsLabel->setBgColor(lv_color_hex(0x003320));
    _statsLabel->setRadius(8);
    _statsLabel->setTextFont(&lv_font_montserrat_14);
    _statsLabel->setTextColor(lv_color_hex(0x00FF88));
    _statsLabel->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _statsLabel->setSize(300, 75);  // Taller for 3-line display
    _statsLabel->align(LV_ALIGN_TOP_LEFT, 5, 5);
    _statsLabel->setText("Nets:0 XP:0 Lvl:1 | Scanning...");

    // Network list display (bottom of screen)
    _networkListLabel = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
    _networkListLabel->setBgColor(lv_color_hex(0x001a00));
    _networkListLabel->setRadius(6);
    _networkListLabel->setTextFont(&lv_font_montserrat_14);
    _networkListLabel->setTextColor(lv_color_hex(0x88FF88));
    _networkListLabel->setTextAlign(LV_TEXT_ALIGN_LEFT);
    _networkListLabel->setSize(300, 50);
    _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
    _networkListLabel->setText("Nearby networks will appear here...");

    _isRunning = true;
    _lastModeChange = GetHAL().millis();
    
    // Load config and set default mode
    gotchi::GotchiConfig config = gotchi::getConfig();
    gotchi::Mode defaultMode = gotchi::Mode::SCOUT;
    
    if (strlen(config.defaultMode) > 0) {
        if (strcmp(config.defaultMode, "HUNT") == 0) defaultMode = gotchi::Mode::HUNT;
        else if (strcmp(config.defaultMode, "SCOUT") == 0) defaultMode = gotchi::Mode::SCOUT;
        else if (strcmp(config.defaultMode, "WARDIVE") == 0) defaultMode = gotchi::Mode::WARDIVE;
        else if (strcmp(config.defaultMode, "SPECTRUM") == 0) defaultMode = gotchi::Mode::SPECTRUM;
        else if (strcmp(config.defaultMode, "BLE_SCAN") == 0) defaultMode = gotchi::Mode::BLE_SCAN;
        else if (strcmp(config.defaultMode, "ROGUE") == 0) defaultMode = gotchi::Mode::ROGUE;
        else if (strcmp(config.defaultMode, "STATS") == 0) defaultMode = gotchi::Mode::STATS;
        else if (strcmp(config.defaultMode, "IDLE") == 0) defaultMode = gotchi::Mode::IDLE;
    }
    
    _currentMode = defaultMode;
    gotchi::setMode(defaultMode);
    
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().setSpeech("Scanning...");
        GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(2000, 180, true));
    }
    
    _headYawOffset = 0;
    _headPitchOffset = 0;
    _touchTracking = false;

    GetStackChan().leftNeonLight().setColor(0x00, 0xFF, 0x88);
    GetStackChan().rightNeonLight().setColor(0x00, 0xFF, 0x88);

    if (GetStackChan().hasMotion()) {
        mclog::tagInfo(getAppInfo().name, "Moving head to home position...");
        GetStackChan().motion().goHome(400);
    }

    mclog::tagInfo(getAppInfo().name, "Gotchi awake!");
}

void AppGotchi::onRunning() {
    LvglLockGuard lock;

    updateGotchi();
    handleInput();
    updateAvatar();
    updateHeadAnimation();
    updateNeonLights();
    renderUI();

    GetStackChan().update();

    view::update_home_indicator();
    view::update_status_bar();
}

void AppGotchi::onClose() {
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    GetStackChan().resetAvatar();

    GetStackChan().leftNeonLight().setColor(0, 0, 0);
    GetStackChan().rightNeonLight().setColor(0, 0, 0);
    
    if (GetStackChan().hasMotion()) {
        GetStackChan().motion().moveWithSpeed(0, 200, 600);
    }

    view::destroy_home_indicator();
    view::destroy_status_bar();

    gotchi::shutdown();
}

void AppGotchi::initGotchi() {
    gotchi::init();
    gotchi::initStorage();
    
    // Check storage and show warning if not present
    if (!gotchi::hasStorage()) {
        mclog::tagInfo(getAppInfo().name, "WARNING: No storage - limited functionality!");
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().setSpeech("No storage!");
            GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(3000, 180, true));
        }
    }
}

void AppGotchi::updateGotchi() {
    if (!_isRunning) return;

    uint32_t now = GetHAL().millis();
    if (now - _lastUpdate < 50) return;
    _lastUpdate = now;

    gotchi::update();
}

void AppGotchi::handleInput() {
    auto touch = GetHAL().lvTouchpad;
    if (!touch) return;

    lv_indev_state_t state = lv_indev_get_state(touch);
    lv_point_t point;
    lv_indev_get_point(touch, &point);

    // Screen dimensions: 320x240
    // Left side (x < 160) = cycle forward
    // Right side (x >= 160) = cycle backward  
    // Long press at top (y < 50) = enter CONFIG mode
    
    // If in CONFIG mode, any tap exits to SCOUT
    if (_currentMode == gotchi::Mode::CONFIG) {
        if (state == LV_INDEV_STATE_PRESSED && _pressStartTime == 0) {
            _pressStartTime = GetHAL().millis();
        }
        if (state == LV_INDEV_STATE_RELEASED && _pressStartTime > 0) {
            if (GetHAL().millis() - _pressStartTime < 500) {
                // Quick tap exits CONFIG mode
                _currentMode = gotchi::Mode::SCOUT;
                gotchi::setMode(_currentMode);
                _lastModeChange = GetHAL().millis();
                _pressStartTime = 0;
                
                if (GetStackChan().hasAvatar()) {
                    GetStackChan().avatar().setSpeech("SCOUT");
                }
                return;
            }
            _pressStartTime = 0;
        }
        return;  // Don't process other input in CONFIG mode
    }
    
    if (state == LV_INDEV_STATE_PRESSED) {
        if (_pressStartTime == 0) {
            // Pause motion when screen is touched
            if (GetStackChan().hasMotion()) {
                GetStackChan().motion().setTorqueEnabled(false);
            }
            _pressStartTime = GetHAL().millis();
            _pressX = point.x;
            _pressY = point.y;
        }
        
        // Check for long press at top of screen to enter CONFIG mode
        if (point.y < 50) {
            // Require 1 second hold in top area to enter CONFIG
            if (GetHAL().millis() - _pressStartTime > 1000 &&
                GetHAL().millis() - _lastModeChange > 2000) {
                
                // Enter CONFIG mode
                _currentMode = gotchi::Mode::CONFIG;
                gotchi::setMode(_currentMode);
                _lastModeChange = GetHAL().millis();
                _pressStartTime = 0;
                
                if (GetStackChan().hasAvatar()) {
                    GetStackChan().avatar().setSpeech("CONFIG");
                }
            }
        }
    } else {
        // Touch released - check if it was a tap
        if (_pressStartTime > 0 && GetHAL().millis() - _pressStartTime < 300) {
            // Left side tap = cycle forward
            if (_pressX < 160) {
                if (GetHAL().millis() - _lastModeChange > 500) {
                    _lastModeChange = GetHAL().millis();
                    cycleMode();
                }
            }
            // Right side tap = cycle backward
            else {
                if (GetHAL().millis() - _lastModeChange > 500) {
                    _lastModeChange = GetHAL().millis();
                    cycleModeBackward();
                }
            }
        }
        // Resume motion when screen is released
        if (GetStackChan().hasMotion()) {
            GetStackChan().motion().setTorqueEnabled(true);
        }
        _pressStartTime = 0;
    }
}

void AppGotchi::cycleMode() {
    _currentMode = gotchi::getModeInfo(_currentMode).nextMode;
    
    gotchi::setMode(_currentMode);
    gotchi::onModeEnter(_currentMode);
    
    const char* modeName = gotchi::getModeName(_currentMode);
    bool showedSpecialMessage = false;

    // Show "OK" disclaimer when first entering HUNT mode
    if (_currentMode == gotchi::Mode::HUNT && gotchi::shouldShowHuntDisclaimer()) {
        gotchi::acknowledgeHuntDisclaimer();
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().setSpeech("HUNT Mode!\nSends deauth frames.\nOK to proceed?");
            GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(4000, 180, true));
        }
        showedSpecialMessage = true;
    }

    // Show educational warning for ROGUE mode (only if not already showing HUNT disclaimer)
    if (!showedSpecialMessage && _currentMode == gotchi::Mode::ROGUE) {
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().setSpeech("EDUCATIONAL!\nOwn networks only!");
            GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(3000, 180, true));
        }
        showedSpecialMessage = true;
    }

    // Play different tone for each mode (bypasses xiaozhi AudioService to avoid WiFi conflict)
    uint16_t tone_freq = gotchi::getModeInfo(_currentMode).toneFreq;
    if (tone_freq > 0) {
        hal_bridge::app_play_tone(tone_freq, 100);
    }

    // Only show mode name if no special message was shown
    if (!showedSpecialMessage && GetStackChan().hasAvatar()) {
        GetStackChan().avatar().setSpeech(modeName);
        GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(1500, 180, true));
    }
}

void AppGotchi::cycleModeBackward() {
    _currentMode = gotchi::getModeInfo(_currentMode).prevMode;
    
    gotchi::setMode(_currentMode);
    gotchi::onModeEnter(_currentMode);
    
    const char* modeName = gotchi::getModeName(_currentMode);
    if (GetStackChan().hasAvatar()) {
        GetStackChan().avatar().setSpeech(modeName);
        GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(1500, 180, true));
    }
}

void AppGotchi::updateAvatar() {
    if (!GetStackChan().hasAvatar()) return;

    auto mood = gotchi::getCurrentMood();
    auto& avatar = GetStackChan().avatar();

    // Mode-specific base emotion with mood override
    avatar::Emotion baseEmotion = (avatar::Emotion)gotchi::getModeAvatarEmotion(_currentMode);
    
    // Mood override
    switch (mood) {
        case gotchi::Mood::HAPPY:
        case gotchi::Mood::EXCITED:
            baseEmotion = avatar::Emotion::Happy;
            break;
        case gotchi::Mood::SAD:
            baseEmotion = avatar::Emotion::Sad;
            break;
        case gotchi::Mood::SLEEPY:
            baseEmotion = avatar::Emotion::Sleepy;
            break;
        case gotchi::Mood::FOCUSED:
            baseEmotion = avatar::Emotion::Doubt;
            break;
        default:
            break;
    }
    
    avatar.setEmotion(baseEmotion);
}

void AppGotchi::updateHeadAnimation() {
    if (!GetStackChan().hasMotion()) return;

    uint32_t now = GetHAL().millis();
    auto& motion = GetStackChan().motion();

    uint32_t interval = gotchi::getModeHeadMoveInterval(_currentMode);
    int16_t baseYaw = gotchi::getModeHeadYaw(_currentMode, now, interval);
    int16_t basePitch = gotchi::getModeHeadPitch(_currentMode);

    bool targetChanged = false;

    if (now - _lastHeadAnim > interval) {
        _lastHeadAnim = now;
        targetChanged = true;

        if (_currentMode == gotchi::Mode::IDLE || _currentMode == gotchi::Mode::STATS) {
            static bool idleDir = false;
            _headYawOffset = idleDir ? 150 : -150;
            idleDir = !idleDir;
            _headPitchOffset = (now / 4000 % 2) ? 30 : -30;
        } else if (_currentMode == gotchi::Mode::HUNT) {
            // Reset label to default before applying mode-specific styling
            _networkListLabel->setSize(300, 50);
            _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
            _networkListLabel->setBgColor(lv_color_hex(0x001a00));
            _networkListLabel->setTextColor(lv_color_hex(0x00FF00));
            
            auto networks = gotchi::getNetworks();
            int netCount = networks.size();
            
            int yawRange = (netCount >= 10) ? 200 : (netCount >= 5) ? 150 : 100;
            _headYawOffset = (int16_t)(sin(now / 500.0) * yawRange);
            
            if (netCount >= 10) {
                _headPitchOffset = (now / 1000 % 3 - 1) * 40;
            } else if (netCount >= 5) {
                _headPitchOffset = (now / 1500 % 2) * 25 - 25;
            } else {
                _headPitchOffset = (now / 2000 % 2) * 15 - 15;
            }
        } else if (_currentMode == gotchi::Mode::SCOUT) {
            _headYawOffset = (int16_t)((now / 500 % 4) - 2) * 100;
            _headPitchOffset = (int16_t)((now / 800 % 3) - 1) * 30;
        } else if (_currentMode == gotchi::Mode::BLE_SCAN) {
            auto devices = gotchi::getBLEDevices();
            int devCount = devices.size();
            
            int yawRange = (devCount >= 10) ? 180 : (devCount >= 5) ? 150 : 100;
            _headYawOffset = (int16_t)(sin(now / 600.0) * yawRange);
            _headPitchOffset = (now / 1200 % 2) ? 20 : -20;
        } else if (_currentMode == gotchi::Mode::ROGUE) {
            // Fast sweeping head during beacon spam
            _headYawOffset = (int16_t)((now / 200 % 8) - 4) * 80;
            _headPitchOffset = (now / 400 % 2) ? 15 : -15;
        }
    }

    if (!_touchTracking) {
        int16_t targetYaw = baseYaw + _headYawOffset;
        int16_t targetPitch = basePitch + _headPitchOffset;
        
        if (targetChanged || targetYaw != _lastTargetYaw || targetPitch != _lastTargetPitch) {
            _lastTargetYaw = targetYaw;
            _lastTargetPitch = targetPitch;
            
            motion.moveWithSpeed(targetYaw, targetPitch, 400);
        }
    }
}

void AppGotchi::updateNeonLights() {
    auto& leftLight = GetStackChan().leftNeonLight();
    auto& rightLight = GetStackChan().rightNeonLight();

    uint32_t now = GetHAL().millis();
    auto networks = gotchi::getNetworks();
    int netCount = networks.size();
    
    // Flash effect when new network found
    if (_neonFlashCount > 0) {
        bool flashOn = (now / 100) % 2;
        if (flashOn) {
            leftLight.setColor(0xFF, 0xFF, 0x00);  // Bright yellow flash
            rightLight.setColor(0xFF, 0xFF, 0x00);
        } else {
            leftLight.setColor(0x00, 0xFF, 0x00);  // Green
            rightLight.setColor(0x00, 0xFF, 0x00);
        }
        if ((now / 200) % 2 == 0) {
            _neonFlashCount--;
        }
        return;
    }
    
// Use centralized mode-based neon color system
    gotchi::NeonColor nc = gotchi::getNeonColor(_currentMode, netCount, now);
    leftLight.setColor(nc.r, nc.g, nc.b);
    rightLight.setColor(nc.r, nc.g, nc.b);
}

static const char* getSignalBars(int rssi) {
    if (rssi > -50) return "++++";
    if (rssi > -60) return "+++";
    if (rssi > -70) return "++";
    if (rssi > -80) return "+";
    return "_";
}

void AppGotchi::renderUI() {
    auto stats = gotchi::getStats();
    auto networks = gotchi::getNetworks();
    
    // Update on-screen stats with enhanced visual info
    char statsText[128];
    
    // Mode indicator - removed, now shows network count instead
    
    // Find best signal
    int bestRssi = -100;
    const char* bestSsid = "none";
    for (const auto& net : networks) {
        if (net.rssi > bestRssi) {
            bestRssi = net.rssi;
            bestSsid = net.ssid;
        }
    }
    
    // Get handshake count (for potential future display)
    int hsCount = gotchi::getHandshakeCount();
    
    // GPS info display - show coordinates in WARDIVE mode, satellite count otherwise
    const char* gpsDisplay = "No GPS";
    if (stats.gpsValid && stats.gpsSatellites > 0) {
        static char gpsBuf[48];
        if (_currentMode == gotchi::Mode::WARDIVE && stats.gpsLat != 0 && stats.gpsLon != 0) {
            int latDeg = (int)abs(stats.gpsLat);
            double latMin = (abs(stats.gpsLat) - latDeg) * 60;
            char latDir = stats.gpsLat >= 0 ? 'N' : 'S';
            int lonDeg = (int)abs(stats.gpsLon);
            double lonMin = (abs(stats.gpsLon) - lonDeg) * 60;
            char lonDir = stats.gpsLon >= 0 ? 'E' : 'W';
            snprintf(gpsBuf, sizeof(gpsBuf), "%02d.%04lf%c %03d.%04lf%c", 
                     latDeg, latMin, latDir, lonDeg, lonMin, lonDir);
        } else {
            snprintf(gpsBuf, sizeof(gpsBuf), "[G%02d]", (int)stats.gpsSatellites);
        }
        gpsDisplay = gpsBuf;
    }
    
    // Build stats text - different format for SPECTRUM mode
    if (_currentMode == gotchi::Mode::SPECTRUM) {
        // Show channel analysis in SPECTRUM mode
        auto channels = gotchi::getChannelAnalysis();
        
        // Find busiest and best (least busy) channels
        int busiestCh = 1;
        int busiestCount = 0;
        int bestCh = 1;
        int bestCount = 999;  // High initial value
        int totalNets = 0;
        char channelDisplay[48] = "";
        char* p = channelDisplay;
        
        for (const auto& ch : channels) {
            if (ch.channel >= 1 && ch.channel <= 13) {
                if (ch.networkCount > busiestCount) {
                    busiestCount = ch.networkCount;
                    busiestCh = ch.channel;
                }
                // Track best (least busy) non-zero channel
                if (ch.networkCount > 0 && ch.networkCount < bestCount) {
                    bestCount = ch.networkCount;
                    bestCh = ch.channel;
                }
                totalNets += ch.networkCount;
                
                // Show channels with activity - use simple ASCII for display
                if (ch.networkCount > 0) {
                    const char* bar = ch.networkCount >= 5 ? "#" : 
                                     ch.networkCount >= 3 ? "+" :
                                     ch.networkCount >= 1 ? "-" : ".";
                    int written = snprintf(p, sizeof(channelDisplay) - (p - channelDisplay), "%d%s ", 
                                          ch.channel, bar);
                    p += written;
                }
            }
        }
        
        int progress = gotchi::getXPProgress(stats.xp, stats.level);
        
        if (totalNets > 0) {
            // Show: Freq | Best:CH Busy:CH | CurrentCH | Level% | Channel visual
            snprintf(statsText, sizeof(statsText), "%.1fMHz|B:%d|Bsy:%d|CH%d|Lv%d %d%%|%s",
                     2.4 + (stats.currentChannel - 1) * 0.016,
                     bestCh,  // Best channel (recommended)
                     busiestCh,  // Busiest channel
                     stats.currentChannel,
                     (int)stats.level, progress,
                     channelDisplay);
        } else {
            snprintf(statsText, sizeof(statsText), "SPECT:%.0fMHz|Scanning...|CH%d|Lv:%d %d%%",
                     2.4 + (stats.currentChannel - 1) * 0.016,
                     stats.currentChannel, (int)stats.level, progress);
        }
    }
    
    // Cleanup all grid labels when leaving their respective modes
    if (_spectrumLabelsCreated && _currentMode != gotchi::Mode::SPECTRUM) {
        destroySpectrumLabels();
    }
    if (_networkBarsCreated && _currentMode != gotchi::Mode::SCOUT && _currentMode != gotchi::Mode::HUNT) {
        destroyNetworkBars();
    }
    if (_signalBarsCreated && _currentMode != gotchi::Mode::WARDIVE) {
        destroySignalBars();
    }
    if (_bleLabelsCreated && _currentMode != gotchi::Mode::BLE_SCAN) {
        destroyBLELabels();
    }
    if (_headerBoxesCreated) {
        destroyHeaderBoxes();
    }
    
    // Create and update header boxes for all modes
    createHeaderBoxes();
    updateHeaderBoxes();
    
    // Hide old stats label since we're using header boxes now
    _statsLabel->setSize(0, 0);
    _statsLabel->setText("");
    
    // IDLE mode - rest state with clean UI
    if (_currentMode == gotchi::Mode::IDLE) {
        // Header boxes already created/updated above
        _networkListLabel->setSize(300, 50);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x001a00));  // Dark green bg
        _networkListLabel->setTextColor(lv_color_hex(0x66FF66));  // Green text
        _networkListLabel->setText("Robot is at rest.");
        return;
    }
    // SCOUT mode - passive scanning (blue theme)
    else if (_currentMode == gotchi::Mode::SCOUT) {
        _networkListLabel->setSize(300, 60);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x000D22));  // Dark blue bg
        _networkListLabel->setTextColor(lv_color_hex(0x66AAFF));  // Blue text
        
        // Show top 3 networks by signal strength
        char netList[200] = {0};
        int count = 0;
        bool isNew = (networks.size() > _lastNetworkCount);
        
        // Sort by signal strength (highest first)
        std::vector<gotchi::NetworkInfo> sorted = networks;
        std::sort(sorted.begin(), sorted.end(), [](const gotchi::NetworkInfo& a, const gotchi::NetworkInfo& b) {
            return a.rssi > b.rssi;
        });
        
        for (const auto& net : sorted) {
            if (count >= 3) break;
            char line[64];
            const char* newStr = (count == 0 && isNew) ? " NEW" : "";
            snprintf(line, sizeof(line), "%-12s %4ddBm CH%d%s\n", 
                     net.ssid, (int)net.rssi, net.channel, newStr);
            strncat(netList, line, sizeof(netList) - strlen(netList) - 1);
            count++;
        }
        if (networks.empty()) {
            snprintf(netList, sizeof(netList), "Scanning...\n");
        }
        _networkListLabel->setText(netList);
        
        _lastNetworkCount = networks.size();
        return;
    }
    // HUNT mode - active scanning (green theme with action)
    else if (_currentMode == gotchi::Mode::HUNT) {
        _networkListLabel->setSize(300, 60);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x001A00));  // Dark green bg
        _networkListLabel->setTextColor(lv_color_hex(0x66FF66));  // Green text
        
        // Show top 3 networks by signal strength
        char netList[200] = {0};
        int count = 0;
        bool isNew = (networks.size() > _lastNetworkCount);
        
        std::vector<gotchi::NetworkInfo> sorted = networks;
        std::sort(sorted.begin(), sorted.end(), [](const gotchi::NetworkInfo& a, const gotchi::NetworkInfo& b) {
            return a.rssi > b.rssi;
        });
        
        for (const auto& net : sorted) {
            if (count >= 3) break;
            char line[64];
            const char* newStr = (count == 0 && isNew) ? " NEW" : "";
            snprintf(line, sizeof(line), "%-12s %4ddBm CH%d%s\n", 
                     net.ssid, (int)net.rssi, net.channel, newStr);
            strncat(netList, line, sizeof(netList) - strlen(netList) - 1);
            count++;
        }
        if (networks.empty()) {
            snprintf(netList, sizeof(netList), "Scanning...\n");
        }
        _networkListLabel->setText(netList);
        
        _lastNetworkCount = networks.size();
        return;
    }
    // WARDIVE mode - GPS wardriving (orange theme)
    else if (_currentMode == gotchi::Mode::WARDIVE) {
        _networkListLabel->setSize(300, 110);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x221100));  // Dark orange bg
        _networkListLabel->setTextColor(lv_color_hex(0xFFaa44));  // Orange text
        
        createSignalBars();
        updateSignalBars();
        _networkListLabel->setText("");
        return;
    }
    // BLE_SCAN mode - formatted device list
    else if (_currentMode == gotchi::Mode::BLE_SCAN) {
        _networkListLabel->setSize(300, 70);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x001522));  // Dark cyan bg
        _networkListLabel->setTextColor(lv_color_hex(0x44DDDD));  // Cyan text
        
        // Show top 5 devices by signal strength with full MAC
        auto devices = gotchi::getBLEDevices();
        char bleList[300] = {0};
        int count = 0;
        
        // Sort by signal strength (strongest first)
        std::vector<gotchi::BLEDeviceInfo> sorted = devices;
        std::sort(sorted.begin(), sorted.end(), [](const gotchi::BLEDeviceInfo& a, const gotchi::BLEDeviceInfo& b) {
            return a.rssi > b.rssi;
        });
        
        strncat(bleList, "Device Name    MAC Address         Signal\n", sizeof(bleList) - 1);
        strncat(bleList, "------------------------------------------\n", sizeof(bleList) - 1);
        
        for (const auto& dev : sorted) {
            if (count >= 5) break;
            char line[48];
            char macStr[18];
            snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     dev.mac[0], dev.mac[1], dev.mac[2], dev.mac[3], dev.mac[4], dev.mac[5]);
            snprintf(line, sizeof(line), "%-14s %-17s %ddBm\n",
                     dev.name, macStr, (int)dev.rssi);
            strncat(bleList, line, sizeof(bleList) - strlen(bleList) - 1);
            count++;
        }
        if (devices.empty()) {
            strncat(bleList, "Scanning for BLE devices...\n", sizeof(bleList) - 1);
        }
        _networkListLabel->setText(bleList);
        return;
    }
    // SPECTRUM mode - channel analysis (purple theme with heatmap)
    else if (_currentMode == gotchi::Mode::SPECTRUM) {
        _networkListLabel->setSize(300, 110);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x150022));  // Dark purple bg
        _networkListLabel->setTextColor(lv_color_hex(0xDD66DD));  // Purple text
        
        createSpectrumLabels();
        updateSpectrumLabels();
        _networkListLabel->setText("");
        return;
    }
    // ROGUE mode - enhanced display with real data
    else if (_currentMode == gotchi::Mode::ROGUE) {
        _networkListLabel->setSize(320, 180);
        _networkListLabel->align(LV_ALIGN_BOTTOM_MID, 0, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x1A0A00));  // Dark orange bg
        _networkListLabel->setTextColor(lv_color_hex(0xFFCC66));  // Orange text
        
        // Get real data from rogue manager
        auto& rogue = gotchi::getRogueManager();
        const char* targetSSID = rogue.getTargetSSID();
        uint8_t targetCh = rogue.getTargetChannel();
        bool isRunning = rogue.isActive();
        
        char rogueDisplay[400];
        const char* statusStr = isRunning ? "● Broadcasting" : "○ Stopped";
        
        snprintf(rogueDisplay, sizeof(rogueDisplay),
            "═══════════════════════════════════════════\n"
            "   ⚠️  WARNING - EDUCATIONAL USE ONLY  ⚠️  \n"
            "═══════════════════════════════════════════\n"
            "Target SSID:  %s\n"
            "Target CH:    %d\n"
            "Status:       %s\n"
            "──────────────────────────────────────────\n"
            "Demo of rogue AP / evil twin attack vectors\n"
            "   Only test on networks YOU own!\n"
            "═══════════════════════════════════════════",
            targetSSID, (int)targetCh, statusStr);
        
        _networkListLabel->setText(rogueDisplay);
        
        if (GetStackChan().hasAvatar()) {
            if (isRunning) {
                GetStackChan().avatar().setSpeech("ROGUE\nBroadcasting!\nBe careful!");
            } else {
                GetStackChan().avatar().setSpeech("ROGUE\nNot running.\nSet target first.");
            }
        }
        return;
    }
    // CONFIG mode - show config UI
    else if (_currentMode == gotchi::Mode::CONFIG) {
        _networkListLabel->setSize(300, 50);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x002222));
        _networkListLabel->setTextColor(lv_color_hex(0x88AAAA));
        _networkListLabel->setText("CONFIG MODE\n"
                                    "AP: StackChan-Config\n"
                                    "Join then visit http://192.168.4.1\n"
                                    "Or edit /sd/config.json on SD card\n"
                                    "\n"
                                    "Tap anywhere to exit");
        return;
    }
    // STATS mode - full screen stats display
    else if (_currentMode == gotchi::Mode::STATS) {
        // Full screen stats display
        _networkListLabel->setSize(320, 220);
        _networkListLabel->align(LV_ALIGN_BOTTOM_MID, 0, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x1A0A1A));  // Dark purple bg
        _networkListLabel->setTextColor(lv_color_hex(0xDD88DD));  // Purple text
        
        // Build full stats display
        char statsDisplay[500];
        const char* prestigeStr = stats.prestige > 0 ? "+P" : "";
        
        int hours = stats.uptimeSeconds / 3600;
        int mins = (stats.uptimeSeconds % 3600) / 60;
        int secs = stats.uptimeSeconds % 60;
        
        int sessHours = stats.sessionTimeSeconds / 3600;
        int sessMins = (stats.sessionTimeSeconds % 3600) / 60;
        
        snprintf(statsDisplay, sizeof(statsDisplay),
            "=========== STATS ===========\n"
            "LEVEL:  Lv%d%s  |  XP: %d\n"
            "PROGRESS: %d%% to next level\n"
            "--------------------------------\n"
            "ACHIEVEMENTS: %u/17 unlocked\n"
            "--------------------------------\n"
            "TOTAL NETWORKS: %u discovered\n"
            "TOTAL HANDSHAKES: %u captured\n"
            "--------------------------------\n"
            "SESSION STATS:\n"
            "  Networks: %u | Time: %dh%dm\n"
            "  XP Gained: +%u this session\n"
            "--------------------------------\n"
            "SYSTEM:\n"
            "  Uptime: %dh%dm%ds\n"
            "  Heap: %d bytes (min: %d)\n"
            "  GPS: %s (%d satellites)",
            (int)stats.level, prestigeStr, (int)stats.xp,
            gotchi::getXPProgress(stats.xp, stats.level),
            (unsigned)stats.achievementCount,
            (unsigned)stats.networksFound,
            (unsigned)stats.handshakesCaptured,
            (unsigned)stats.sessionNetworks, sessHours, sessMins,
            (unsigned)stats.sessionXPGain,
            hours, mins, secs,
            (int)stats.freeHeap, (int)stats.minHeap,
            stats.gpsValid ? "Valid" : "No signal",
            (int)stats.gpsSatellites);
        
        _networkListLabel->setText(statsDisplay);
        
        if (GetStackChan().hasAvatar()) {
            GetStackChan().avatar().setSpeech("");
        }
        return;
    }
    // Fallback for any other modes (shouldn't reach here)
    // Networks and text fallback - this code should NOT run for any defined mode
    // since all modes now have explicit handling above with returns
    
    // Legacy fallback kept for safety - shows scanning status
    if (networks.size() > 0) {
        // 3-line format: Line1=Network, Line2=Level/XP, Line3=Other
        int progress = gotchi::getXPProgress(stats.xp, stats.level);
        snprintf(statsText, sizeof(statsText), 
                 "Nets:%d Ch%d %s\n"        // Line 1: Networks, channel, best SSID
                 "Lv:%d XP:%d %d%%\n"       // Line 2: Level, XP, progress
                 "HS:%d GPS:%s",            // Line 3: Handshakes, GPS
                 (int)networks.size(), 
                 stats.currentChannel, bestSsid,
                 (int)stats.level, (int)stats.xp, progress,
                 hsCount, gpsDisplay);
    } else {
        // Reset label positions and colors for other modes (no networks found)
        _statsLabel->setSize(300, 75);
        _statsLabel->align(LV_ALIGN_TOP_LEFT, 5, 5);
        _statsLabel->setBgColor(lv_color_hex(0x003320));  // Reset to green
        _statsLabel->setTextColor(lv_color_hex(0x00FF88));  // Reset text color
        _networkListLabel->setSize(300, 50);
        _networkListLabel->align(LV_ALIGN_BOTTOM_LEFT, 5, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x001a00));  // Reset network list bg
        _networkListLabel->setTextColor(lv_color_hex(0x88FF88));  // Reset network list text
        
        // 3-line format for no networks state
        int progress = gotchi::getXPProgress(stats.xp, stats.level);
        snprintf(statsText, sizeof(statsText), 
                 "Nets:0 Ch%d Scanning\n"       // Line 1
                 "Lv:%d XP:%d %d%%\n"           // Line 2
                 "HS:%d GPS:%s",               // Line 3
                 stats.currentChannel,
                 (int)stats.level, (int)stats.xp, progress,
                 hsCount, gpsDisplay);
    }
    _statsLabel->setText(statsText);
    
    // Announce new networks found in HUNT mode
    if (_currentMode == gotchi::Mode::HUNT && networks.size() > _lastNetworkCount) {
        _lastNetworkCount = networks.size();
        
        const char* latestSSID = networks.back().ssid;
        
        if (GetStackChan().hasAvatar()) {
            // Show excited emotion when finding new networks
            GetStackChan().avatar().setEmotion(avatar::Emotion::Happy);
            
            // Get quirky phrase from dialogue system
            const char* phrase = _idleDialogue.getNetworkFoundPhrase(latestSSID);
            GetStackChan().avatar().setSpeech(phrase);
            GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(2000, 180, true));
        }
        
        // Flash neon lights briefly when network found
        _neonFlashCount = 3;
    }
    
    // Announce new BLE devices found in BLE_SCAN mode
    if (_currentMode == gotchi::Mode::BLE_SCAN) {
        auto devices = gotchi::getBLEDevices();
        if (devices.size() > _lastBLEDeviceCount && devices.size() > 0) {
            _lastBLEDeviceCount = devices.size();
            
            if (GetStackChan().hasAvatar()) {
                // Show excited emotion when finding new BLE devices
                GetStackChan().avatar().setEmotion(avatar::Emotion::Happy);
                
                // Get quirky phrase from dialogue system
                const char* latestName = devices.back().name[0] != '\0' ? 
                    devices.back().name : "Unknown Device";
                const char* phrase = _idleDialogue.getBLEDeviceFoundPhrase(latestName);
                GetStackChan().avatar().setSpeech(phrase);
                GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(1500, 180, true));
            }
            
            // Flash neon lights briefly when BLE device found
            _neonFlashCount = 2;
        }
    }
    
    // Update network list display (show up to 4 networks)
    // Or show channel analysis in SPECTRUM mode
    if (_currentMode == gotchi::Mode::SPECTRUM) {
        auto channels = gotchi::getChannelAnalysis();
        char spectrumText[256] = "Channel Usage:\n";
        
        int maxCount = 0;
        for (const auto& ch : channels) {
            if (ch.networkCount > maxCount) maxCount = ch.networkCount;
        }
        
        size_t spectrumPos = strlen(spectrumText);
        for (int i = 0; i < 14; i++) {
            char line[32];
            
            if (channels[i].networkCount > 0) {
                snprintf(line, sizeof(line), "CH%d:%d ", channels[i].channel, channels[i].networkCount);
            } else {
                snprintf(line, sizeof(line), "CH%d:- ", channels[i].channel);
            }
            size_t len = strlen(line);
            if (spectrumPos + len < sizeof(spectrumText) - 1) {
                strcat(spectrumText, line);
                spectrumPos += len;
            }
            if (i == 6 && spectrumPos + 1 < sizeof(spectrumText) - 1) {
                strcat(spectrumText, "\n");
                spectrumPos += 1;
            }
        }
        _networkListLabel->setText(spectrumText);
    } else if (_currentMode == gotchi::Mode::WARDIVE) {
        // WARDIVE - show signal strength distribution with visual bars
        char wardiveText[256] = "Signal Strength:\n";
        int strong = 0, medium = 0, weak = 0;
        for (const auto& net : networks) {
            if (net.rssi > -60) strong++;
            else if (net.rssi > -80) medium++;
            else weak++;
        }
        snprintf(wardiveText, sizeof(wardiveText), "+++ %d Strong | ++ %d Med | + %d Weak\n* %s %s",
                 strong, medium, weak, getSignalBars(bestRssi), bestSsid);
        _networkListLabel->setText(wardiveText);
    } else if (_currentMode == gotchi::Mode::IDLE) {
        // IDLE - show rest state message
        _networkListLabel->setText("Robot is at rest.\n"
                                    "Touch sides to cycle modes.\n"
                                    "Up: Next mode\n"
                                    "Down: Previous mode");
    } else if (_currentMode == gotchi::Mode::SCOUT) {
        // SCOUT - show detailed network list with signal bars
        char scoutText[512] = "";
        size_t scoutPos = 0;
        int count = std::min((int)networks.size(), 6);
        for (int i = networks.size() - count; i < (int)networks.size(); i++) {
            char line[80];
            const char* sec = "OPN";
            // Note: beacon frames don't include security info, this is placeholder
            snprintf(line, sizeof(line), "%s %s\n  CH%d %ddBm %s\n", 
                     getSignalBars(networks[i].rssi), networks[i].ssid, 
                     networks[i].channel, (int)networks[i].rssi, sec);
            size_t len = strlen(line);
            if (scoutPos + len < sizeof(scoutText) - 1) {
                strcat(scoutText, line);
                scoutPos += len;
            }
        }
        if (networks.empty()) {
            snprintf(scoutText, sizeof(scoutText), "No networks found.\nTry HUNT mode first.");
        }
        _networkListLabel->setText(scoutText);
    } else if (_currentMode == gotchi::Mode::BLE_SCAN) {
        // BLE_SCAN - show discovered BLE devices
        auto devices = gotchi::getBLEDevices();
        char bleText[512] = "";
        size_t blePos = 0;
        
        if (!devices.empty()) {
            int count = std::min((int)devices.size(), 6);
            for (int i = devices.size() - count; i < (int)devices.size(); i++) {
                char line[80];
                snprintf(line, sizeof(line), "%s %s\n  RSSI: %ddBm\n", 
                         getSignalBars(devices[i].rssi), devices[i].name, (int)devices[i].rssi);
                size_t len = strlen(line);
                if (blePos + len < sizeof(bleText) - 1) {
                    strcat(bleText, line);
                    blePos += len;
                }
            }
        } else {
            snprintf(bleText, sizeof(bleText), "No BLE devices found.\nScanning for nearby Bluetooth...");
        }
        _networkListLabel->setText(bleText);
    } else if (_currentMode == gotchi::Mode::STATS) {
        // STATS mode - full detailed stats page (check BEFORE networks)
        _networkListLabel->setSize(320, 185);
        _networkListLabel->align(LV_ALIGN_BOTTOM_MID, 0, -5);
        _networkListLabel->setBgColor(lv_color_hex(0x220033));  // Dark purple
        _networkListLabel->setTextColor(lv_color_hex(0xFF88FF));
        
        // Build comprehensive stats display
        char statsDetails[500];
        
        // Get achievement bitmask to show which are unlocked
        uint32_t achMask = gotchi::getAchievementsBitmask();
        
        // Get daily challenge
        gotchi::ChallengeInfo dailyChallenge;
        bool hasDaily = gotchi::getDailyChallenge(dailyChallenge);
        
        // Format: Networks, Handshakes, Prestige
        // Achievement list (abbreviated: N=Network, K=Key, T=Time, M=Mode)
        snprintf(statsDetails, sizeof(statsDetails),
            "--- STATS ---\n"
            "Nets:%u HS:%u P:%u\n"
            "--- DAILY ---\n"
            "%s\n"
            "+%dXP\n"
            "--- ACHIEVEMENTS ---\n"
            "N:%c%c%c%c%c K:%c%c%c%c T:%c%c%c M:%c%c%c%c B:%c%c",
            (unsigned)stats.networksFound,
            (unsigned)stats.handshakesCaptured,
            (unsigned)stats.prestige,
            hasDaily ? dailyChallenge.name : "None",
            (int)dailyChallenge.xpReward,
            // Network achievements (0-4)
            (achMask & (1<<0)) ? '1' : '.',
            (achMask & (1<<1)) ? '1' : '.',
            (achMask & (1<<2)) ? '2' : '.',
            (achMask & (1<<3)) ? '5' : '.',
            (achMask & (1<<4)) ? 'X' : '.',
            // Key/Handshake achievements (5-8)
            (achMask & (1<<5)) ? '1' : '.',
            (achMask & (1<<6)) ? '5' : '.',
            (achMask & (1<<7)) ? 'X' : '.',
            (achMask & (1<<8)) ? 'X' : '.',
            // Time achievements (9-11)
            (achMask & (1<<9)) ? '1' : '.',
            (achMask & (1<<10)) ? '2' : '.',
            (achMask & (1<<11)) ? '7' : '.',
            // Mode achievements (12-15)
            (achMask & (1<<12)) ? 'S' : '.',
            (achMask & (1<<13)) ? 'C' : '.',
            (achMask & (1<<14)) ? 'W' : '.',
            (achMask & (1<<15)) ? 'B' : '.',
            // Special achievements (16-17)
            (achMask & (1<<16)) ? 'H' : '.',
            (achMask & (1<<17)) ? 'B' : '.');
        _networkListLabel->setText(statsDetails);
    } else if (networks.size() > 0) {
        char networkList[256] = "";
        size_t listPos = 0;
        int count = std::min((int)networks.size(), 4);
        for (int i = networks.size() - count; i < (int)networks.size(); i++) {
            char line[64];
            snprintf(line, sizeof(line), "%s %s (%ddBm)\n", 
                     getSignalBars(networks[i].rssi), networks[i].ssid, (int)networks[i].rssi);
            size_t len = strlen(line);
            if (listPos + len < sizeof(networkList) - 1) {
                strcat(networkList, line);
                listPos += len;
            }
        }
        _networkListLabel->setText(networkList);
    } else {
        _networkListLabel->setText("[W] Scanning for networks...");
    }
    
    // Idle dialogue - use mode-specific phrases based on ModeInfo
    if (gotchi::isDialogueEnabled(_currentMode)) {
        uint32_t interval = gotchi::getModeDialogueInterval(_currentMode);
        uint32_t now = GetHAL().millis();
        if (interval > 0 && now - _lastIdleSpeak > interval && _idleDialogue.shouldSpeak(now)) {
            _lastIdleSpeak = now;
            if (GetStackChan().hasAvatar()) {
                const char* phrase = _idleDialogue.getModeSpecificPhrase(_currentMode);
                GetStackChan().avatar().setSpeech(phrase);
                GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(2500, 180, true));
            }
        }
    }
    
    // Update last count when leaving sniff mode
    if (_currentMode != gotchi::Mode::HUNT) {
        _lastNetworkCount = 0;
    }
    if (_currentMode != gotchi::Mode::BLE_SCAN) {
        _lastBLEDeviceCount = 0;
    }
    
    if (_currentMode != _lastLoggedMode) {
        _lastLoggedMode = _currentMode;
        mclog::tagInfo(getAppInfo().name, "Mode: {} | XP: {} | Lvl: {} | Networks: {}", 
                       gotchi::getModeName(_currentMode), (int)stats.xp, (int)stats.level, (int)networks.size());
    }
}

lv_color_t getHeatmapColor(int networkCount) {
    if (networkCount == 0) return lv_color_hex(0x444444);
    if (networkCount <= 2) return lv_color_hex(0x00FF00);
    if (networkCount <= 5) return lv_color_hex(0xFFFF00);
    if (networkCount <= 9) return lv_color_hex(0xFF8800);
    return lv_color_hex(0xFF0000);
}

lv_color_t getHeatmapBgColor(int networkCount) {
    if (networkCount == 0) return lv_color_hex(0x222222);
    if (networkCount <= 2) return lv_color_hex(0x002200);
    if (networkCount <= 5) return lv_color_hex(0x222200);
    if (networkCount <= 9) return lv_color_hex(0x221100);
    return lv_color_hex(0x220000);
}

void AppGotchi::createSpectrumLabels() {
    if (_spectrumLabelsCreated) return;
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
        _spectrumLabels[i] = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
        _spectrumLabels[i]->setSize(42, 38);
        _spectrumLabels[i]->setRadius(4);
        _spectrumLabels[i]->setTextFont(&lv_font_montserrat_14);
        _spectrumLabels[i]->setTextAlign(LV_TEXT_ALIGN_CENTER);
        
        int col = i % 7;
        int row = i / 7;
        int x = 5 + col * 47;
        int y = 165 + row * 42;
        _spectrumLabels[i]->align(LV_ALIGN_TOP_LEFT, x, y);
        
        char chText[16];
        snprintf(chText, sizeof(chText), "CH%d\n-", i + 1);
        _spectrumLabels[i]->setText(chText);
        _spectrumLabels[i]->setBgColor(lv_color_hex(0x222222));
        _spectrumLabels[i]->setTextColor(lv_color_hex(0x888888));
    }
    _spectrumLabelsCreated = true;
}

void AppGotchi::updateSpectrumLabels() {
    if (!_spectrumLabelsCreated) return;
    
    auto channels = gotchi::getChannelAnalysis();
    
    for (int i = 0; i < NUM_CHANNELS && i < (int)channels.size(); i++) {
        int count = channels[i].networkCount;
        
        _spectrumLabels[i]->setBgColor(getHeatmapBgColor(count));
        _spectrumLabels[i]->setTextColor(getHeatmapColor(count));
        
        char chText[16];
        snprintf(chText, sizeof(chText), "CH%d\n%d", i + 1, count);
        _spectrumLabels[i]->setText(chText);
    }
}

void AppGotchi::destroySpectrumLabels() {
    if (!_spectrumLabelsCreated) return;
    
    for (int i = 0; i < NUM_CHANNELS; i++) {
        _spectrumLabels[i].reset();
    }
    _spectrumLabelsCreated = false;
}

lv_color_t getSignalColor(int8_t rssi) {
    if (rssi > -60) return lv_color_hex(0x00FF00);  // Strong - green
    if (rssi > -80) return lv_color_hex(0xFFFF00);  // Medium - yellow
    return lv_color_hex(0xFF4400);  // Weak - orange/red
}

lv_color_t getSignalBgColor(int8_t rssi) {
    if (rssi > -60) return lv_color_hex(0x002200);
    if (rssi > -80) return lv_color_hex(0x222200);
    return lv_color_hex(0x220800);
}

void AppGotchi::createNetworkBars() {
    if (_networkBarsCreated) return;
    
    for (int i = 0; i < MAX_NETWORK_BARS; i++) {
        _networkBars[i] = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
        _networkBars[i]->setSize(68, 28);
        _networkBars[i]->setRadius(3);
        _networkBars[i]->setTextFont(&lv_font_montserrat_14);
        _networkBars[i]->setTextAlign(LV_TEXT_ALIGN_LEFT);
        
        int col = i % 4;
        int row = i / 4;
        int x = 5 + col * 75;
        int y = 160 + row * 32;
        _networkBars[i]->align(LV_ALIGN_TOP_LEFT, x, y);
        
        _networkBars[i]->setText("");
        _networkBars[i]->setBgColor(lv_color_hex(0x111111));
        _networkBars[i]->setTextColor(lv_color_hex(0x888888));
    }
    _networkBarsCreated = true;
}

void AppGotchi::updateNetworkBars() {
    if (!_networkBarsCreated) return;
    
    auto networks = gotchi::getNetworks();
    
    for (int i = 0; i < MAX_NETWORK_BARS; i++) {
        if (i < (int)networks.size()) {
            int idx = networks.size() - MAX_NETWORK_BARS + i;
            if (idx < 0) idx = 0;
            
            auto& net = networks[idx];
            int8_t rssi = net.rssi;
            
            _networkBars[i]->setBgColor(getSignalBgColor(rssi));
            _networkBars[i]->setTextColor(getSignalColor(rssi));
            
            char barText[32];
            snprintf(barText, sizeof(barText), "CH%d %ddBm", net.channel, (int)rssi);
            _networkBars[i]->setText(barText);
        } else {
            _networkBars[i]->setBgColor(lv_color_hex(0x111111));
            _networkBars[i]->setTextColor(lv_color_hex(0x444444));
            _networkBars[i]->setText("-");
        }
    }
}

void AppGotchi::destroyNetworkBars() {
    if (!_networkBarsCreated) return;
    
    for (int i = 0; i < MAX_NETWORK_BARS; i++) {
        _networkBars[i].reset();
    }
    _networkBarsCreated = false;
}

void AppGotchi::createSignalBars() {
    if (_signalBarsCreated) return;
    
    const char* labels[3] = {"STRONG", "MEDIUM", "WEAK"};
    int bgColors[3] = {0x002200, 0x222200, 0x220800};
    int fgColors[3] = {0x00FF00, 0xFFFF00, 0xFF6600};
    
    for (int i = 0; i < NUM_SIGNAL_BARS; i++) {
        _signalBars[i] = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
        _signalBars[i]->setSize(90, 45);
        _signalBars[i]->setRadius(4);
        _signalBars[i]->setTextFont(&lv_font_montserrat_14);
        _signalBars[i]->setTextAlign(LV_TEXT_ALIGN_CENTER);
        
        int x = 15 + i * 100;
        int y = 160;
        _signalBars[i]->align(LV_ALIGN_TOP_LEFT, x, y);
        
        _signalBars[i]->setBgColor(lv_color_hex(bgColors[i]));
        _signalBars[i]->setTextColor(lv_color_hex(fgColors[i]));
        _signalBars[i]->setText(labels[i]);
    }
    _signalBarsCreated = true;
}

void AppGotchi::updateSignalBars() {
    if (!_signalBarsCreated) return;
    
    auto networks = gotchi::getNetworks();
    int strong = 0, medium = 0, weak = 0;
    
    for (const auto& net : networks) {
        if (net.rssi > -60) strong++;
        else if (net.rssi > -80) medium++;
        else weak++;
    }
    
    char countText[3][20];
    snprintf(countText[0], sizeof(countText[0]), "STRONG\n%d", strong);
    snprintf(countText[1], sizeof(countText[1]), "MEDIUM\n%d", medium);
    snprintf(countText[2], sizeof(countText[2]), "WEAK\n%d", weak);
    
    for (int i = 0; i < NUM_SIGNAL_BARS; i++) {
        _signalBars[i]->setText(countText[i]);
    }
}

void AppGotchi::destroySignalBars() {
    if (!_signalBarsCreated) return;
    
    for (int i = 0; i < NUM_SIGNAL_BARS; i++) {
        _signalBars[i].reset();
    }
    _signalBarsCreated = false;
}

void AppGotchi::createBLELabels() {
    if (_bleLabelsCreated) return;
    
    for (int i = 0; i < MAX_BLE_DEVICES; i++) {
        _bleLabels[i] = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
        _bleLabels[i]->setSize(145, 22);
        _bleLabels[i]->setRadius(2);
        _bleLabels[i]->setTextFont(&lv_font_montserrat_14);
        _bleLabels[i]->setTextAlign(LV_TEXT_ALIGN_LEFT);
        
        int col = i % 2;
        int row = i / 2;
        int x = 5 + col * 155;
        int y = 160 + row * 26;
        _bleLabels[i]->align(LV_ALIGN_TOP_LEFT, x, y);
        
        _bleLabels[i]->setBgColor(lv_color_hex(0x111122));
        _bleLabels[i]->setTextColor(lv_color_hex(0x8888FF));
        _bleLabels[i]->setText("");
    }
    _bleLabelsCreated = true;
}

void AppGotchi::updateBLELabels() {
    if (!_bleLabelsCreated) return;
    
    auto devices = gotchi::getBLEDevices();
    
    for (int i = 0; i < MAX_BLE_DEVICES; i++) {
        if (i < (int)devices.size()) {
            int idx = devices.size() - MAX_BLE_DEVICES + i;
            if (idx < 0) idx = 0;
            
            auto& dev = devices[idx];
            
            _bleLabels[i]->setBgColor(lv_color_hex(0x111122));
            _bleLabels[i]->setTextColor(lv_color_hex(0x66DDDD));
            
            char bleText[36];
            snprintf(bleText, sizeof(bleText), "%s", dev.name);
            _bleLabels[i]->setText(bleText);
        } else {
            _bleLabels[i]->setBgColor(lv_color_hex(0x111122));
            _bleLabels[i]->setTextColor(lv_color_hex(0x444466));
            _bleLabels[i]->setText("-");
        }
    }
}

void AppGotchi::destroyBLELabels() {
    if (!_bleLabelsCreated) return;
    
    for (int i = 0; i < MAX_BLE_DEVICES; i++) {
        _bleLabels[i].reset();
    }
    _bleLabelsCreated = false;
}

void AppGotchi::createHeaderBoxes() {
    if (_headerBoxesCreated) return;
    
    for (int i = 0; i < NUM_HEADER_BOXES; i++) {
        _headerBoxes[i] = std::make_unique<uitk::lvgl_cpp::Label>(lv_screen_active());
        _headerBoxes[i]->setSize(70, 28);
        _headerBoxes[i]->setRadius(3);
        _headerBoxes[i]->setTextFont(&lv_font_montserrat_14);
        _headerBoxes[i]->setTextAlign(LV_TEXT_ALIGN_CENTER);
        
        int x = 10 + i * 75;
        int y = 8;
        _headerBoxes[i]->align(LV_ALIGN_TOP_LEFT, x, y);
        
        _headerBoxes[i]->setText("-");
        _headerBoxes[i]->setBgColor(lv_color_hex(0x111111));
        _headerBoxes[i]->setTextColor(lv_color_hex(0x888888));
    }
    _headerBoxesCreated = true;
}

void AppGotchi::updateHeaderBoxes() {
    if (!_headerBoxesCreated) return;
    
    auto stats = gotchi::getStats();
    auto networks = gotchi::getNetworks();
    auto devices = gotchi::getBLEDevices();
    int progress = gotchi::getXPProgress(stats.xp, stats.level);
    
    char boxText[4][20];
    lv_color_t boxBgColor, boxTextColor;
    
    // Set up box colors based on mode
    switch (_currentMode) {
        case gotchi::Mode::IDLE:
            boxBgColor = lv_color_hex(0x003300);
            boxTextColor = lv_color_hex(0x88FF88);
            snprintf(boxText[0], 20, "IDLE");
            snprintf(boxText[1], 20, "Lv:%d", (int)stats.level);
            snprintf(boxText[2], 20, "XP:%d", (int)stats.xp);
            snprintf(boxText[3], 20, "Nets:%d", (int)networks.size());
            break;
        case gotchi::Mode::SCOUT:
            boxBgColor = lv_color_hex(0x001133);
            boxTextColor = lv_color_hex(0x88CCFF);
            {
                int bestRssi = -100;
                for (const auto& n : networks) if (n.rssi > bestRssi) bestRssi = n.rssi;
                snprintf(boxText[0], 20, "SCOUT");
                snprintf(boxText[1], 20, "Nets:%d", (int)networks.size());
                snprintf(boxText[2], 20, "%ddBm", bestRssi > -100 ? bestRssi : 0);
                snprintf(boxText[3], 20, "Lv%d %d%%", (int)stats.level, progress);
            }
            break;
        case gotchi::Mode::HUNT:
            boxBgColor = lv_color_hex(0x003300);
            boxTextColor = lv_color_hex(0x88FF88);
            {
                int bestRssi = -100;
                for (const auto& n : networks) if (n.rssi > bestRssi) bestRssi = n.rssi;
                int hsCount = gotchi::getHandshakeCount();
                snprintf(boxText[0], 20, "HUNT");
                snprintf(boxText[1], 20, "Nets:%d", (int)networks.size());
                snprintf(boxText[2], 20, "HS:%d", hsCount);
                snprintf(boxText[3], 20, "Lv%d %d%%", (int)stats.level, progress);
            }
            break;
        case gotchi::Mode::WARDIVE:
            boxBgColor = lv_color_hex(0x331A00);
            boxTextColor = lv_color_hex(0xFFCC66);
            {
                // Cycle through networks every 2 seconds
                uint32_t now = GetHAL().millis();
                if (now - _wardiveCycleTime > 2000) {
                    _wardiveCycleTime = now;
                    if (networks.size() > 0) {
                        _wardiveCurrentNetworkIndex = (_wardiveCurrentNetworkIndex + 1) % networks.size();
                    }
                }
                const char* gpsStr = stats.gpsValid ? "On" : "Off";
                if (_wardiveCurrentNetworkIndex < (int)networks.size()) {
                    auto& net = networks[_wardiveCurrentNetworkIndex];
                    snprintf(boxText[2], 20, "%ddBm", (int)net.rssi);
                } else {
                    snprintf(boxText[2], 20, "No nets");
                }
                snprintf(boxText[0], 20, "WARDIVE");
                snprintf(boxText[1], 20, "Nets:%d", (int)networks.size());
                snprintf(boxText[3], 20, "GPS:%s S:%d", gpsStr, (int)stats.gpsSatellites);
            }
            break;
        case gotchi::Mode::BLE_SCAN:
            boxBgColor = lv_color_hex(0x002233);
            boxTextColor = lv_color_hex(0x66FFFF);
            snprintf(boxText[0], 20, "BLE");
            snprintf(boxText[1], 20, "Devs:%d", (int)devices.size());
            snprintf(boxText[2], 20, "Scan...");
            snprintf(boxText[3], 20, "Lv%d %d%%", (int)stats.level, progress);
            break;
        case gotchi::Mode::SPECTRUM:
            {
                auto channels = gotchi::getChannelAnalysis();
                int bestCh = 1, bestCount = 999, busiestCh = 1, busiestCount = 0, totalNets = 0;
                for (const auto& ch : channels) {
                    totalNets += ch.networkCount;
                    if (ch.networkCount > busiestCount) { busiestCount = ch.networkCount; busiestCh = ch.channel; }
                    if (ch.networkCount > 0 && ch.networkCount < bestCount) { bestCount = ch.networkCount; bestCh = ch.channel; }
                }
                boxBgColor = lv_color_hex(0x220033);
                boxTextColor = lv_color_hex(0xFF66FF);
                snprintf(boxText[0], 20, "SPECT");
                snprintf(boxText[1], 20, "Best:CH%d", bestCh);
                snprintf(boxText[2], 20, "Bsy:CH%d", busiestCh);
                snprintf(boxText[3], 20, "Tot:%d", totalNets);
            }
            break;
        case gotchi::Mode::ROGUE:
            {
                auto& rogue = gotchi::getRogueManager();
                const char* target = rogue.getTargetSSID();
                uint8_t ch = rogue.getTargetChannel();
                bool running = rogue.isActive();
                boxBgColor = lv_color_hex(0x332200);
                boxTextColor = running ? lv_color_hex(0xFFAA44) : lv_color_hex(0xFFCC88);
                snprintf(boxText[0], 20, "ROGUE");
                snprintf(boxText[1], 20, "%.12s", target);
                snprintf(boxText[2], 20, "CH:%d", (int)ch);
                snprintf(boxText[3], 20, running ? "● On" : "○ Off");
            }
            break;
        case gotchi::Mode::CONFIG:
            boxBgColor = lv_color_hex(0x003333);
            boxTextColor = lv_color_hex(0x88FFFF);
            snprintf(boxText[0], 20, "CONFIG");
            snprintf(boxText[1], 20, "WiFi:%s", gotchi::isConfigMode() ? "On" : "Off");
            snprintf(boxText[2], 20, "192.168.4.1");
            snprintf(boxText[3], 20, "TAP EXIT");
            break;
        case gotchi::Mode::STATS:
            boxBgColor = lv_color_hex(0x330033);
            boxTextColor = lv_color_hex(0xFF88FF);
            snprintf(boxText[0], 20, "Lv:%d%s", (int)stats.level, stats.prestige > 0 ? "+P" : "");
            snprintf(boxText[1], 20, "XP:%d", (int)stats.xp);
            snprintf(boxText[2], 20, "Ach:%d/17", (int)stats.achievementCount);
            snprintf(boxText[3], 20, "Up:%dh%dm", (int)(stats.uptimeSeconds / 3600), (int)((stats.uptimeSeconds % 3600) / 60));
            break;
        default:
            boxBgColor = lv_color_hex(0x111111);
            boxTextColor = lv_color_hex(0x888888);
            snprintf(boxText[0], 20, "MODE");
            snprintf(boxText[1], 20, "-");
            snprintf(boxText[2], 20, "-");
            snprintf(boxText[3], 20, "-");
            break;
    }
    
    for (int i = 0; i < NUM_HEADER_BOXES; i++) {
        _headerBoxes[i]->setBgColor(boxBgColor);
        _headerBoxes[i]->setTextColor(boxTextColor);
        _headerBoxes[i]->setText(boxText[i]);
    }
}

void AppGotchi::destroyHeaderBoxes() {
    if (!_headerBoxesCreated) return;
    
    for (int i = 0; i < NUM_HEADER_BOXES; i++) {
        _headerBoxes[i].reset();
    }
    _headerBoxesCreated = false;
}