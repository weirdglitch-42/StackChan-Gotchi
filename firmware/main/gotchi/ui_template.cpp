/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "ui_template.h"
#include <esp_log.h>

static const char* TAG = "gotchi_ui_template";

namespace gotchi {

UITemplate::UITemplate() : _initialized(false) {}

void UITemplate::init() {
    _initialized = true;
    ESP_LOGI(TAG, "UI Template initialized");
}

void UITemplate::setHeaderBox(int index, const char* text, lv_color_t bgColor, lv_color_t textColor) {
    if (!_initialized || index < 0 || index >= 4) return;
    // This would be called from app_gotchi to update header boxes
    // The actual header boxes are managed in app_gotchi.cpp
    (void)index; (void)text; (void)bgColor; (void)textColor;
}

void UITemplate::setHeaderBoxesFromStyle(const UIModeStyle& style) {
    if (!_initialized) return;
    // Apply all 4 header boxes from style
    for (int i = 0; i < 4; i++) {
        setHeaderBox(i, style.headerBox[i], style.headerBoxBg[i], style.headerBoxText[i]);
    }
}

void UITemplate::setBodyText(const char* text) {
    if (!_initialized) return;
    // Body text is set via _networkListLabel in app_gotchi
    (void)text;
}

void UITemplate::setBodySize(int width, int height) {
    if (!_initialized) return;
    (void)width; (void)height;
}

void UITemplate::setBodyPosition(int x, int y) {
    if (!_initialized) return;
    (void)x; (void)y;
}

void UITemplate::setBodyColors(lv_color_t bg, lv_color_t text) {
    if (!_initialized) return;
    (void)bg; (void)text;
}

void UITemplate::setBodyFromStyle(const UIModeStyle& style) {
    if (!_initialized) return;
    setBodyColors(style.bodyBg, style.bodyText);
}

UIModeStyle UITemplate::getStyleForMode(Mode mode, const char* box0, const char* box1, 
                                        const char* box2, const char* box3, 
                                        const char* body, int bodyHeight) {
    UIModeStyle style = {};
    
    style.headerBox[0] = box0;
    style.headerBox[1] = box1;
    style.headerBox[2] = box2;
    style.headerBox[3] = box3;
    style.bodyContent = body;
    style.bodyHeight = bodyHeight;
    style.bodyX = 10;
    style.bodyY = 45;
    
    // Default colors
    for (int i = 0; i < 4; i++) {
        style.headerBoxBg[i] = lv_color_hex(0x111111);
        style.headerBoxText[i] = lv_color_hex(0x888888);
    }
    style.bodyBg = lv_color_hex(0x001a00);
    style.bodyText = lv_color_hex(0x88FF88);
    
    // Mode-specific colors
    switch (mode) {
        case Mode::IDLE:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x003300);
                style.headerBoxText[i] = lv_color_hex(0x88FF88);
            }
            style.bodyBg = lv_color_hex(0x001a00);
            style.bodyText = lv_color_hex(0x00FF00);
            break;
        case Mode::SCOUT:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x001133);
                style.headerBoxText[i] = lv_color_hex(0x88CCFF);
            }
            style.bodyBg = lv_color_hex(0x000D22);
            style.bodyText = lv_color_hex(0x66AAFF);
            break;
        case Mode::HUNT:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x003300);
                style.headerBoxText[i] = lv_color_hex(0x88FF88);
            }
            style.bodyBg = lv_color_hex(0x001A00);
            style.bodyText = lv_color_hex(0x66FF66);
            break;
        case Mode::WARDIVE:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x331A00);
                style.headerBoxText[i] = lv_color_hex(0xFFCC66);
            }
            style.bodyBg = lv_color_hex(0x221100);
            style.bodyText = lv_color_hex(0xFFaa44);
            break;
        case Mode::SPECTRUM:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x001522);
                style.headerBoxText[i] = lv_color_hex(0x44DDDD);
            }
            style.bodyBg = lv_color_hex(0x001522);
            style.bodyText = lv_color_hex(0x44DDDD);
            break;
        case Mode::BLE_SCAN:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x150022);
                style.headerBoxText[i] = lv_color_hex(0xDD66DD);
            }
            style.bodyBg = lv_color_hex(0x150022);
            style.bodyText = lv_color_hex(0xDD66DD);
            break;
        case Mode::ROGUE:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x1A0A00);
                style.headerBoxText[i] = lv_color_hex(0xFFCC66);
            }
            style.bodyBg = lv_color_hex(0x1A0A00);
            style.bodyText = lv_color_hex(0xFFCC66);
            break;
        case Mode::CONFIG:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x002222);
                style.headerBoxText[i] = lv_color_hex(0x88AAAA);
            }
            style.bodyBg = lv_color_hex(0x002222);
            style.bodyText = lv_color_hex(0x88AAAA);
            break;
        case Mode::STATS:
            for (int i = 0; i < 4; i++) {
                style.headerBoxBg[i] = lv_color_hex(0x330033);
                style.headerBoxText[i] = lv_color_hex(0xFF88FF);
            }
            style.bodyBg = lv_color_hex(0x1A0A1A);
            style.bodyText = lv_color_hex(0xDD88DD);
            break;
        default:
            break;
    }
    
    return style;
}

static UITemplate _uiTemplate;

UITemplate& getUITemplate() {
    return _uiTemplate;
}

}