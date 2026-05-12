/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>
#include <gotchi/gotchi.h>
#include <lvgl.h>

namespace gotchi {

struct UIModeStyle {
    const char* headerBox[4];
    lv_color_t headerBoxBg[4];
    lv_color_t headerBoxText[4];
    lv_color_t bodyBg;
    lv_color_t bodyText;
    const char* bodyContent;
    int bodyHeight;
    int bodyX;
    int bodyY;
};

class UITemplate {
public:
    UITemplate();
    
    void init();
    
    void setHeaderBox(int index, const char* text, lv_color_t bgColor, lv_color_t textColor);
    void setHeaderBoxesFromStyle(const UIModeStyle& style);
    
    void setBodyText(const char* text);
    void setBodySize(int width, int height);
    void setBodyPosition(int x, int y);
    void setBodyColors(lv_color_t bg, lv_color_t text);
    void setBodyFromStyle(const UIModeStyle& style);
    
    UIModeStyle getStyleForMode(Mode mode, const char* box0, const char* box1, 
                                const char* box2, const char* box3, 
                                const char* body, int bodyHeight = 180);

private:
    bool _initialized = false;
};

UITemplate& getUITemplate();

}