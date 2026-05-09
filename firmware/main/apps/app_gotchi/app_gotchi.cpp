/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "app_gotchi.h"
#include <esp_log.h>
#include <cstring>
#include <cmath>
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
    _statsLabel->setSize(300, 50);
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
    _currentMode = gotchi::Mode::SNIFF;  // Auto-start in SNIFF mode
    _lastModeChange = GetHAL().millis();

    gotchi::setMode(gotchi::Mode::SNIFF);  // Auto-start scanning
    
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

    // Require long press in top area to change mode (prevents accidental triggers)
    if (state == LV_INDEV_STATE_PRESSED) {
        if (point.y < 80) {
            if (_pressStartTime == 0) {
                _pressStartTime = GetHAL().millis();
            }
            // Require 500ms hold in top area
            if (GetHAL().millis() - _pressStartTime > 500 &&
                GetHAL().millis() - _lastModeChange > 1000) {
                _lastModeChange = GetHAL().millis();
                cycleMode();
            }
        } else {
            _pressStartTime = 0;
        }
    } else {
        _pressStartTime = 0;
    }
}

void AppGotchi::cycleMode() {
    switch (_currentMode) {
        case gotchi::Mode::IDLE:
            _currentMode = gotchi::Mode::SNIFF;
            break;
        case gotchi::Mode::SNIFF:
            _currentMode = gotchi::Mode::SCOUT;
            break;
        case gotchi::Mode::SCOUT:
            _currentMode = gotchi::Mode::WARDIVE;
            break;
        case gotchi::Mode::WARDIVE:
            _currentMode = gotchi::Mode::SPECTRUM;
            break;
        case gotchi::Mode::SPECTRUM:
            _currentMode = gotchi::Mode::BLE_SNIFF;
            break;
        case gotchi::Mode::BLE_SNIFF:
            _currentMode = gotchi::Mode::IDLE;
            break;
    }

    gotchi::setMode(_currentMode);

    // Show mode in speech bubble
    const char* modeName = gotchi::getModeName(_currentMode);

    // Play different tone for each mode (bypasses xiaozhi AudioService to avoid WiFi conflict)
    uint16_t tone_freq = 600;
    switch (_currentMode) {
        case gotchi::Mode::SNIFF: tone_freq = 600; break;   // Low beep
        case gotchi::Mode::SCOUT: tone_freq = 800; break;   // Mid beep
        case gotchi::Mode::WARDIVE: tone_freq = 1000; break; // Higher beep
        case gotchi::Mode::SPECTRUM: tone_freq = 1200; break; // Highest beep
        default: tone_freq = 400; break;  // IDLE - very low
    }
    hal_bridge::app_play_tone(tone_freq, 100);

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
    avatar::Emotion baseEmotion = avatar::Emotion::Neutral;
    
    switch (_currentMode) {
        case gotchi::Mode::SNIFF:
            baseEmotion = avatar::Emotion::Doubt;  // Scanning/alert
            break;
        case gotchi::Mode::SCOUT:
            baseEmotion = avatar::Emotion::Happy;   // Exploring/curious
            break;
        case gotchi::Mode::WARDIVE:
            baseEmotion = avatar::Emotion::Angry;   // Intense/active
            break;
        case gotchi::Mode::SPECTRUM:
            baseEmotion = avatar::Emotion::Doubt;   // Analyzing
            break;
        case gotchi::Mode::BLE_SNIFF:
            baseEmotion = avatar::Emotion::Doubt;   // BLE scanning
            break;
        case gotchi::Mode::IDLE:
            baseEmotion = avatar::Emotion::Neutral;
            break;
    }
    
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

    static const uint32_t IDLE_INTERVAL = 3000;
    static const uint32_t SNIFF_INTERVAL = 800;
    static const uint32_t SCOUT_INTERVAL = 1500;

    uint32_t interval = IDLE_INTERVAL;
    int16_t baseYaw = 0;
    int16_t basePitch = 200;

    switch (_currentMode) {
        case gotchi::Mode::SNIFF:
            interval = SNIFF_INTERVAL;
            break;
        case gotchi::Mode::SCOUT:
            interval = SCOUT_INTERVAL;
            basePitch = 150;
            break;
        case gotchi::Mode::WARDIVE:
            interval = 600;
            baseYaw = (now / interval) % 2 ? 300 : -300;
            break;
        case gotchi::Mode::SPECTRUM:
            interval = 1000;
            baseYaw = (int16_t)((now / interval) % 6 - 3) * 150;
            break;
        case gotchi::Mode::BLE_SNIFF:
            interval = 600;
            break;
        default:
            break;
    }

    bool targetChanged = false;

    if (now - _lastHeadAnim > interval) {
        _lastHeadAnim = now;
        targetChanged = true;

        if (_currentMode == gotchi::Mode::IDLE) {
            static bool idleDir = false;
            _headYawOffset = idleDir ? 150 : -150;
            idleDir = !idleDir;
            _headPitchOffset = (now / 4000 % 2) ? 30 : -30;
        } else if (_currentMode == gotchi::Mode::SNIFF) {
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
        } else if (_currentMode == gotchi::Mode::BLE_SNIFF) {
            auto devices = gotchi::getBLEDevices();
            int devCount = devices.size();
            
            int yawRange = (devCount >= 10) ? 180 : (devCount >= 5) ? 150 : 100;
            _headYawOffset = (int16_t)(sin(now / 600.0) * yawRange);
            _headPitchOffset = (now / 1200 % 2) ? 20 : -20;
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
    
    // Dynamic blink speed based on network count
    int blinkSpeed = 400;  // base speed
    if (netCount > 10) blinkSpeed = 150;      // Very fast when many networks
    else if (netCount > 5) blinkSpeed = 250;  // Fast
    else if (netCount > 2) blinkSpeed = 350;  // Medium
    else blinkSpeed = 500;  // Slow
    
    bool blinkOn = (now / blinkSpeed) % 2;

    switch (_currentMode) {
        case gotchi::Mode::IDLE:
            // Gentle pulse - slow breathing effect
            if ((now / 1000) % 2) {
                leftLight.setColor(0x00, 0xAA, 0x66);
                rightLight.setColor(0x00, 0xAA, 0x66);
            } else {
                leftLight.setColor(0x00, 0x66, 0x33);
                rightLight.setColor(0x00, 0x66, 0x33);
            }
            break;

        case gotchi::Mode::SNIFF:
            // Dynamic colors based on network count
            if (netCount >= 10) {
                // EXCITED - lots of networks!
                if (blinkOn) {
                    leftLight.setColor(0x00, 0xFF, 0x00);  // Bright green
                    rightLight.setColor(0xFF, 0xFF, 0x00); // Yellow
                } else {
                    leftLight.setColor(0x00, 0x88, 0x00);
                    rightLight.setColor(0x88, 0x88, 0x00);
                }
            } else if (netCount >= 5) {
                // HAPPY - good number of networks
                if (blinkOn) {
                    leftLight.setColor(0x00, 0xFF, 0x88);  // Green
                    rightLight.setColor(0x00, 0xFF, 0xFF);  // Cyan
                } else {
                    leftLight.setColor(0x00, 0xAA, 0x55);
                    rightLight.setColor(0x00, 0xAA, 0xAA);
                }
            } else {
                // CALM - few networks
                if (blinkOn) {
                    leftLight.setColor(0x00, 0x88, 0x44);  // Dim green
                    rightLight.setColor(0x00, 0x88, 0x88); // Dim cyan
                } else {
                    leftLight.setColor(0x00, 0x44, 0x22);
                    rightLight.setColor(0x00, 0x44, 0x44);
                }
            }
            break;

        case gotchi::Mode::SCOUT:
            // Blue pulse - exploring
            if (blinkOn) {
                leftLight.setColor(0x00, 0x66, 0xFF);
                rightLight.setColor(0x00, 0x66, 0xFF);
            } else {
                leftLight.setColor(0x00, 0x33, 0x88);
                rightLight.setColor(0x00, 0x33, 0x88);
            }
            break;

        case gotchi::Mode::WARDIVE:
            if ((now / 150) % 2) {
                leftLight.setColor(0xFF, 0x66, 0x00);
                rightLight.setColor(0xFF, 0x66, 0x00);
            } else {
                leftLight.setColor(0x88, 0x33, 0x00);
                rightLight.setColor(0x88, 0x33, 0x00);
            }
            break;

        case gotchi::Mode::SPECTRUM: {
            uint8_t phase = (now / 100) % 6;
            if (phase < 3) {
                leftLight.setColor(0xFF, 0x00 << (phase * 2), 0xFF);
                rightLight.setColor(0xFF, 0x00 << ((phase + 1) * 2), 0xFF);
            } else {
                leftLight.setColor(0x00, 0xFF, 0x00);
                rightLight.setColor(0x00, 0x00, 0xFF);
            }
            break;
        }

        case gotchi::Mode::BLE_SNIFF: {
            // Blue/Magenta pulse - BLE scanning
            if (blinkOn) {
                leftLight.setColor(0x00, 0x88, 0xFF);
                rightLight.setColor(0x88, 0x00, 0xFF);
            } else {
                leftLight.setColor(0x00, 0x44, 0x88);
                rightLight.setColor(0x44, 0x00, 0x88);
            }
            break;
        }

        default:
            leftLight.setColor(0x00, 0xFF, 0x88);
            rightLight.setColor(0x00, 0xFF, 0x88);
            break;
    }
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
    
    // Get handshake count
    int hsCount = gotchi::getHandshakeCount();
    const char* hsIcon = hsCount > 0 ? "#" : "o";
    
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
    } else if (networks.size() > 0) {
        int progress = gotchi::getXPProgress(stats.xp, stats.level);
        snprintf(statsText, sizeof(statsText), "Nets:%d|Lv:%d %d%%|CH%d|%s%d HS|%s|%s",
                 (int)networks.size(),
                 (int)stats.level, progress,
                 stats.currentChannel, hsIcon, hsCount,
                 bestSsid, gpsDisplay);
    } else {
        int progress = gotchi::getXPProgress(stats.xp, stats.level);
        snprintf(statsText, sizeof(statsText), "Nets:0|Scanning...|Lv:%d %d%%|CH%d|%s%d HS|%s",
                 (int)stats.level, progress,
                 stats.currentChannel, hsIcon, hsCount, gpsDisplay);
    }
    _statsLabel->setText(statsText);
    
    // Announce new networks found in SNIFF mode
    if (_currentMode == gotchi::Mode::SNIFF && networks.size() > _lastNetworkCount) {
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
    
    // Announce new BLE devices found in BLE_SNIFF mode
    if (_currentMode == gotchi::Mode::BLE_SNIFF) {
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
            snprintf(scoutText, sizeof(scoutText), "No networks found.\nTry SNIFF mode first.");
        }
        _networkListLabel->setText(scoutText);
    } else if (_currentMode == gotchi::Mode::BLE_SNIFF) {
        // BLE_SNIFF - show discovered BLE devices
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
    
    // Idle dialogue - random quirky phrases in all active modes
    if (_currentMode == gotchi::Mode::SNIFF || _currentMode == gotchi::Mode::SCOUT ||
        _currentMode == gotchi::Mode::WARDIVE || _currentMode == gotchi::Mode::SPECTRUM ||
        _currentMode == gotchi::Mode::BLE_SNIFF) {
        uint32_t now = GetHAL().millis();
        if (now - _lastIdleSpeak > 5000 && _idleDialogue.shouldSpeak(now)) {
            _lastIdleSpeak = now;
            if (GetStackChan().hasAvatar()) {
                const char* phrase = _idleDialogue.getRandomPhrase(networks, stats.xp, stats.level, true);
                GetStackChan().avatar().setSpeech(phrase);
                GetStackChan().addModifier(std::make_unique<stackchan::SpeakingModifier>(2500, 180, true));
            }
        }
    }
    
    // Update last count when leaving sniff mode
    if (_currentMode != gotchi::Mode::SNIFF) {
        _lastNetworkCount = 0;
    }
    if (_currentMode != gotchi::Mode::BLE_SNIFF) {
        _lastBLEDeviceCount = 0;
    }
    
    if (_currentMode != _lastLoggedMode) {
        _lastLoggedMode = _currentMode;
        mclog::tagInfo(getAppInfo().name, "Mode: {} | XP: {} | Lvl: {} | Networks: {}", 
                       gotchi::getModeName(_currentMode), (int)stats.xp, (int)stats.level, (int)networks.size());
    }
}