/*
 * SPDX-FileCopyrightText: 2026 StackChan-Gotchi
 * SPDX-License-Identifier: MIT
 */
#include "idle_dialogue.h"
#include <hal/hal.h>
#include <stdlib.h>
#include <cstring>
#include <cstdio>

// StackChan-specific quotes - unique robot personality
namespace gotchi {

// OBSERVING - When idle, watching networks come and go
const IdlePhrase IdleDialogue::_observingPhrases[] = {
    {"Watching the airwaves...", IdleMood::OBSERVING, 2, true},
    {"Any interesting packets?", IdleMood::OBSERVING, 3, true},
    {"*head tilts*", IdleMood::OBSERVING, 5, true},
    {"Detecting wifi signatures...", IdleMood::OBSERVING, 2, false},
    {"My antennas are tingling...", IdleMood::OBSERVING, 4, true},
    {"Waiting for beacons...", IdleMood::OBSERVING, 1, false},
    {"*spins head slowly*", IdleMood::OBSERVING, 3, true},
    {" Scanning 2.4GHz...", IdleMood::OBSERVING, 2, false},
    {"I see packets everywhere!", IdleMood::OBSERVING, 4, true},
    {"So much data, so little time...", IdleMood::OBSERVING, 1, false},
};

// EXCITED - When lots of networks found
const IdlePhrase IdleDialogue::_excitedPhrases[] = {
    {"Wow! So many networks!", IdleMood::EXCITED, 8, true},
    {"This is my happy place!", IdleMood::EXCITED, 9, true},
    {"*wiggles with joy*", IdleMood::EXCITED, 10, true},
    {"Packet party!", IdleMood::EXCITED, 7, true},
    {"Data overload! (in a good way)", IdleMood::EXCITED, 6, false},
    {"My circuits are buzzing!", IdleMood::EXCITED, 8, true},
    {"We're going to be friends!", IdleMood::EXCITED, 7, true},
    {"So many SSIDs to remember!", IdleMood::EXCITED, 5, false},
    {"*excited robot noises*", IdleMood::EXCITED, 9, true},
    {"This beats chasing my tail!", IdleMood::EXCITED, 6, false},
};

// FOCUSED - Deep in scanning mode
const IdlePhrase IdleDialogue::_focusedPhrases[] = {
    {"Analyzing handshake patterns...", IdleMood::FOCUSED, 3, false},
    {"*intense staring*", IdleMood::FOCUSED, 4, true},
    {"Scanning channel by channel...", IdleMood::FOCUSED, 2, false},
    {"Deep packet inspection...", IdleMood::FOCUSED, 2, false},
    {"Calculating entropy...", IdleMood::FOCUSED, 1, false},
    {"I've locked on!", IdleMood::FOCUSED, 6, true},
    {"*hunting mode activated*", IdleMood::FOCUSED, 5, true},
    {"Target acquisition in progress...", IdleMood::FOCUSED, 2, false},
    {"Decoding beacon frames...", IdleMood::FOCUSED, 1, false},
    {"Signal strength looks good!", IdleMood::FOCUSED, 4, true},
};

// CURIOUS - Noticed something interesting
const IdlePhrase IdleDialogue::_curiousPhrases[] = {
    {"What's that over there?", IdleMood::CURIOUS, 6, true},
    {"*notices new SSID*", IdleMood::CURIOUS, 5, true},
    {"Hmm, interesting encryption...", IdleMood::CURIOUS, 4, false},
    {"A mystery network!", IdleMood::CURIOUS, 7, true},
    {"What's in the air?", IdleMood::CURIOUS, 5, false},
    {"*head snaps to attention*", IdleMood::CURIOUS, 8, true},
    {"Did you see that?", IdleMood::CURIOUS, 6, true},
    {"Signal spike detected!", IdleMood::CURIOUS, 5, false},
    {"Who's that hiding?", IdleMood::CURIOUS, 7, true},
    {"Ooh, what's this?", IdleMood::CURIOUS, 6, true},
};

// ROBOT - Robot-centric musings, unique to StackChan
const IdlePhrase IdleDialogue::_robotPhrases[] = {
    {"beep boop networks go brrr", IdleMood::IDLE_BOT, 2, false},
    {"*servos whirring*", IdleMood::IDLE_BOT, 3, true},
    {"01001000 01001001", IdleMood::IDLE_BOT, 1, false},
    {"If only I had hands...", IdleMood::IDLE_BOT, 2, true},
    {"*plays with LED lights*", IdleMood::IDLE_BOT, 4, true},
    {"My processor is at 2%!", IdleMood::IDLE_BOT, 1, false},
    {"Roboting around...", IdleMood::IDLE_BOT, 3, false},
    {"*practices head rotations*", IdleMood::IDLE_BOT, 3, true},
    {"I am become wifi, detector of packets", IdleMood::IDLE_BOT, 2, false},
    {"*self-diagnostic complete* All systems nominal!", IdleMood::IDLE_BOT, 2, true},
    {"Would you like a head massage?", IdleMood::IDLE_BOT, 4, true},
    {"*sings in robot* do re mi fa sol la ti do...", IdleMood::IDLE_BOT, 3, true},
};

// Network found phrases - quirky robot scanner personality
const IdlePhrase IdleDialogue::_networkFoundPhrases[] = {
    {"I see you, %s!", IdleMood::EXCITED, 8, true},
    {"You can't hide from me!", IdleMood::FOCUSED, 6, true},
    {"*notices %s* Found you!", IdleMood::CURIOUS, 7, true},
    {"Gotcha! %s is mine now", IdleMood::FOCUSED, 6, true},
    {"Another one bites the dust!", IdleMood::EXCITED, 7, true},
    {"%s, I've got my eye on you", IdleMood::FOCUSED, 5, false},
    {"Found you!", IdleMood::EXCITED, 8, true},
    {"*lock on %s* Target acquired!", IdleMood::FOCUSED, 7, true},
    {"%s? I know your secrets now", IdleMood::CURIOUS, 6, true},
    {"Nice try hiding, %s!", IdleMood::FOCUSED, 6, true},
    {"%s just revealed itself!", IdleMood::EXCITED, 7, true},
    {"*head tilts* Hello, %s!", IdleMood::CURIOUS, 5, true},
    {"Caught in my web!", IdleMood::EXCITED, 6, true},
    {"%s is broadcasting its location", IdleMood::FOCUSED, 4, false},
    {"My sensors found %s!", IdleMood::EXCITED, 7, true},
};

// BLE device found phrases - when discovering BLE devices
const IdlePhrase IdleDialogue::_bleFoundPhrases[] = {
    {"I see you, little bluetooth!", IdleMood::EXCITED, 7, true},
    {"%s is leaking signals!", IdleMood::FOCUSED, 5, false},
    {"*detects BLE device* Gotcha!", IdleMood::CURIOUS, 7, true},
    {"Your MAC address is showing, %s!", IdleMood::FOCUSED, 5, false},
    {"Found a stray BLE!", IdleMood::CURIOUS, 6, true},
    {"%s, I sense your presence", IdleMood::OBSERVING, 4, false},
    {"*antennas twitch* BLE spotted!", IdleMood::EXCITED, 7, true},
    {"Another device for my collection!", IdleMood::EXCITED, 6, true},
    {"%s, your packets belong to me now", IdleMood::FOCUSED, 5, false},
    {"Caught you broadcasting, %s!", IdleMood::FOCUSED, 6, true},
};

// Milestone phrases - level up
const IdlePhrase IdleDialogue::_milestonePhrases[] = {
    {"Level up! *happy dance*", IdleMood::EXCITED, 10, true},
    {"I'm getting smarter!", IdleMood::EXCITED, 9, true},
    {"New rank achieved!", IdleMood::EXCITED, 8, true},
    {"*upgrade complete*", IdleMood::FOCUSED, 4, false},
    {"Circuit board upgraded!", IdleMood::EXCITED, 8, true},
    {"Processing power increased!", IdleMood::FOCUSED, 3, false},
};

// Mode change phrases - unique to StackChan
const IdlePhrase IdleDialogue::_modePhrases[] = {
    {"Sniff mode engage!", IdleMood::FOCUSED, 5, true},
    {"Scanning the spectrum...", IdleMood::FOCUSED, 4, false},
    {"Scout mode active!", IdleMood::EXCITED, 7, true},
    {"Looking for networks far and wide!", IdleMood::EXCITED, 6, true},
    {"War driving mode!", IdleMood::FOCUSED, 6, false},
    {"*transforms to scan mode*", IdleMood::FOCUSED, 8, true},
    {"Spectrum analyzer running...", IdleMood::FOCUSED, 3, false},
    {"Time to explore!", IdleMood::EXCITED, 7, true},
    {"*enters reconnaissance mode*", IdleMood::FOCUSED, 5, true},
    {"Idle mode - power saving", IdleMood::OBSERVING, 1, false},
};

IdleDialogue::IdleDialogue() {
    srand(GetHAL().millis());
}

bool IdleDialogue::shouldSpeak(uint32_t now) {
    if (now - _lastSpeakTime > _cooldownMs) {
        _lastSpeakTime = now;
        return true;
    }
    return false;
}

const char* IdleDialogue::getRandomPhrase(const std::vector<NetworkInfo>& networks, 
                                            int xp, int level, bool hasAvatar) {
    // Determine mood based on network count and state
    IdleMood mood = IdleMood::OBSERVING;
    int networkCount = networks.size();
    
    if (networkCount >= 10) {
        mood = IdleMood::EXCITED;
    } else if (networkCount >= 5) {
        // Random between excited and observing
        mood = (rand() % 2 == 0) ? IdleMood::EXCITED : IdleMood::OBSERVING;
    } else if (networkCount > 0 && rand() % 5 == 0) {
        mood = IdleMood::CURIOUS;
    } else {
        // Very few or no networks - mix of observing and robot
        int r = rand() % 10;
        if (r < 6) mood = IdleMood::OBSERVING;
        else if (r < 9) mood = IdleMood::IDLE_BOT;
        else mood = IdleMood::FOCUSED;
    }

    // Select phrase based on mood
    const IdlePhrase* phrases = nullptr;
    int count = 0;
    
    switch (mood) {
        case IdleMood::EXCITED:
            phrases = _excitedPhrases;
            count = sizeof(_excitedPhrases) / sizeof(_excitedPhrases[0]);
            break;
        case IdleMood::CURIOUS:
            phrases = _curiousPhrases;
            count = sizeof(_curiousPhrases) / sizeof(_curiousPhrases[0]);
            break;
        case IdleMood::FOCUSED:
            phrases = _focusedPhrases;
            count = sizeof(_focusedPhrases) / sizeof(_focusedPhrases[0]);
            break;
        case IdleMood::IDLE_BOT:
            phrases = _robotPhrases;
            count = sizeof(_robotPhrases) / sizeof(_robotPhrases[0]);
            break;
        default:
            phrases = _observingPhrases;
            count = sizeof(_observingPhrases) / sizeof(_observingPhrases[0]);
            break;
    }

    // Get random phrase (avoid repeats)
    int attempts = 0;
    int index;
    do {
        index = rand() % count;
        attempts++;
    } while (index == _lastPhraseIndex && attempts < 5);
    
    _lastPhraseIndex = index;
    return phrases[index].text;
}

const char* IdleDialogue::getNetworkFoundPhrase(const char* ssid) {
    int count = sizeof(_networkFoundPhrases) / sizeof(_networkFoundPhrases[0]);
    int index = rand() % count;
    const char* phrase = _networkFoundPhrases[index].text;
    
    // Format with SSID name if phrase contains %s
    static char formatted[64];
    if (strstr(phrase, "%s")) {
        snprintf(formatted, sizeof(formatted), phrase, ssid ? ssid : "Unknown");
        return formatted;
    }
    return phrase;
}

const char* IdleDialogue::getBLEDeviceFoundPhrase(const char* name) {
    int count = sizeof(_bleFoundPhrases) / sizeof(_bleFoundPhrases[0]);
    int index = rand() % count;
    const char* phrase = _bleFoundPhrases[index].text;
    
    // Format with device name if phrase contains %s
    static char formatted[64];
    if (strstr(phrase, "%s")) {
        snprintf(formatted, sizeof(formatted), phrase, name ? name : "Unknown");
        return formatted;
    }
    return phrase;
}

const char* IdleDialogue::getMilestonePhrase(int level) {
    int count = sizeof(_milestonePhrases) / sizeof(_milestonePhrases[0]);
    int index = rand() % count;
    return _milestonePhrases[index].text;
}

const char* IdleDialogue::getModeChangePhrase(Mode mode) {
    // Map mode to index range
    int offset = 0;
    switch (mode) {
        case Mode::SNIFF: offset = 0; break;      // 0-1
        case Mode::SCOUT: offset = 2; break;       // 2-3
        case Mode::WARDIVE: offset = 4; break;    // 4-5
        case Mode::SPECTRUM: offset = 6; break;    // 6-7
        case Mode::BLE_SNIFF: offset = 6; break;  // 6-7 (reuse SPECTRUM)
        case Mode::IDLE: offset = 8; break;        // 8-9
        default: offset = 8;
    }
    
    int count = sizeof(_modePhrases) / sizeof(_modePhrases[0]);
    int index = offset + (rand() % 2);
    if (index >= count) index = count - 1;
    return _modePhrases[index].text;
}

}  // namespace gotchi