# StackChan-Gotchi

<p align="center">
  <img src="https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/K151_stack_chan_main_pictures_01.webp" width="60%">
</p>

A **pwnagotchi-style WiFi/BLE reconnaissance companion** for M5Stack CoreS3 robot (StackChan). Combines Tamagotchi-like gamification with network scanning, uniquely leveraging StackChan's robot capabilities—expressive face, head movement, and neon lights.

---

## Overview

**Goal**: Create an engaging WiFi/BLE reconnaissance tool that leverages StackChan's robot capabilities to make network security research more interactive and fun.

**Hardware**: M5Stack CoreS3 (ESP32-S3, 16MB Flash, 8MB PSRAM) + GPS Unit (optional)

**Inspiration**:
- [pwnagotchi] (https://github.com/evilsocket/pwnagotchi) - the original security "gotchi" for rPi
- [M5PORKCHOP](https://github.com/0ct0sec/M5PORKCHOP) - Gamification, XP system, multiple modes, personality
- [M5Gotchi](https://github.com/Devsur11/M5Gotchi) - Pwnagotchi UI, auto mode, web interface

---

## Features

### Network Scanning
- WiFi beacon frame capture (promiscuous mode)
- Channel hopping (1-13, prioritizes 1/6/11)
- EAPOL handshake capture
- BLE device scanning via NimBLE

### Gamification System
- XP earned from: networks discovered (+1), handshakes captured (+25), channels visited (+5), BLE devices (+2), uptime (+1/min)
- **42 levels** with robot-themed titles:
  - Levels 1-8: Unit → Omega
  - Levels 9-16: Observer, Probe, Analyst, Decoder, Tracker, Hunter, Crawler, Synth
  - Levels 17-24: Cortex, Nexus, Matrix, Quantum, Singularity, Hyperion, Archon, Titan
  - Levels 25-32: Prime, Alpha, Omega Prime, Supreme, Transcendent, Paramount, Glorious, Eternal
  - Levels 33-39: Legendary, Mythic, Omnipotent, Infinite, Absolute, Ultimate, Paramount
  - **Level 40: Enigma** (something lurks beyond...)
  - **Level 41: ???** (the answer approaches...)
  - **Level 42: ???** (the ultimate question...)
- Prestige system: Reset to level 1, keep prestige count, +10% XP bonus per prestige level
- 37 achievements with XP rewards (hybrid system)
- Daily challenges with streak tracking
- Persistent XP storage via ESP32 NVS

### Modes
| Mode | Description | Neon Color |
|------|-------------|------------|
| **IDLE** | Idle mode | Green |
| **SCOUT** | Passive scanning, no transmission | Blue |
| **HUNT** | Active WiFi monitoring, capture handshakes + deauth | Green/Cyan |
| **WARDIVE** | Active wardriving with GPS logging | Orange |
| **SPECTRUM** | Channel analysis | Rainbow |
| **BLE-SCAN** | BLE device scanning | Blue/Purple |
| **ROGUE** | Educational beacon spam on fixed channel 6 (OWN networks only!) | Orange |
| **CONFIG** | Web config portal (AP: StackChan-Config, visit 192.168.4.1) | Purple |
| **STATS** | View achievements, XP, prestige | Purple/White |

### StackChan Integration
- Dynamic avatar emotions per mode
- Head movement speed increases with activity
- Neon light indicators color-coded by mode
- Touch interaction for mode cycling
- **Touch pauses robot motion** - touch screen to pause head movement

### Additional
- GPS support (GPS-BDS Unit on UART2)
- Internal flash storage (~2MB FATFS)
- On-screen stats display

---

## Hardware

### Requirements
- M5Stack CoreS3
- (Optional) GPS-BDS Unit v1.1 for wardriving

### Known Limitations
- SD card unavailable (hardware pin conflict on CoreS3 - LCD and microSD share SPI3 pins)
- Internal flash storage (~2MB FATFS partition) used instead

---

## Build & Flash

### Quick Start (Windows CMD)
```batch
cd firmware
menu.bat
```
Then select option 1 for clean build, or 3 to flash.

### Manual Build
```batch
cd firmware
idf.py build
idf.py -p COM8 flash monitor
```

### Available Scripts
| Script | Description |
|--------|-------------|
| `menu.bat` | Interactive build menu (recommended) |
| `clean_build.bat` | Clean + build (removes build folder first) |
| `build.bat` | Quick incremental build |
| `flash.bat` | Flash to device (prompts for COM port) |
| `erase_flash.bat` | Erase NVS or full flash |

**Note**: Run scripts in CMD (not PowerShell or Git Bash).

---

## Project Structure

```
firmware/main/
├── apps/app_gotchi/     - Main UI and mode handling
├── gotchi/              - Core scanning logic (OOP refactored)
│   ├── gotchi.cpp/h    - Core API (335 lines)
│   ├── mode_manager.cpp/h    - Mode state machine
│   ├── wifi_scanner.cpp/h     - WiFi promiscuous + hopping
│   ├── handshake_parser.cpp/h - EAPOL parsing
│   ├── deauth_manager.cpp/h  - Deauth attack logic
│   ├── ble_scanner.cpp/h      - BLE GAP scanning
│   ├── network_db.cpp/h      - Network/handshake/BLE storage
│   ├── xp_system.cpp/h       - XP/level progression
│   ├── achievement_system.cpp/h - Achievements & challenges
│   ├── gps.cpp/h             - GPS NMEA parsing
│   ├── rogue_manager.cpp/h   - ROGUE mode beacon spam
│   └── web_manager.cpp/h     - CONFIG mode HTTP server
└── hal/board/           - StackChan board initialization
```

---

## Legal Warning

This tool is for **educational and security research purposes only**.

- Only test networks you own or have explicit permission to test
- Unauthorized access to computer systems is illegal
- The author takes no responsibility for misuse

---

## References

- StackChan: https://github.com/M5Stack/M5Stack-StackChan
- M5PORKCHOP: https://github.com/0ct0sec/M5PORKCHOP
- M5Gotchi: https://github.com/Devsur11/M5Gotchi/
- (THE OG) pwnagotchi: https://github.com/evilsocket/pwnagotchi