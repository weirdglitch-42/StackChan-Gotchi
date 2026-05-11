/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <stdint.h>

namespace gotchi {

class XPSystem {
public:
    XPSystem();
    
    void init();
    void addXP(int32_t amount);
    int32_t getXP() const { return _xp; }
    int32_t getLevel() const { return _level; }
    
    const char* getLevelTitle() const;
    int getXPForLevel(int level) const;
    int getXPProgress() const;
    
    uint8_t getPrestige() const { return _prestige; }
    void prestigeReset();
    
    void loadFromNVS();
    void saveToNVS();

private:
    void updateLevel();
    static const char* LEVEL_TITLES[];
    static const int XP_PER_LEVEL[];
    
    int32_t _xp;
    int32_t _level;
    uint8_t _prestige;
    bool _initialized;
};

}