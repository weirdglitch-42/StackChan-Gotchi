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
- [M5PORKCHOP](https://github.com/M-Tech-Innovation/M5PORKCHOP) - Gamification, XP system, multiple modes, personality
- [M5Gotchi](https://github.com/xenon-mastodon/M5Gotchi) - Pwnagotchi UI, auto mode, web interface

---

## Features

### Network Scanning
- WiFi beacon frame capture (promiscuous mode)
- Channel hopping (1-13, prioritizes 1/6/11)
- EAPOL handshake capture
- BLE device scanning via NimBLE

### Gamification System
- XP earned from: networks discovered, handshakes captured, channels scanned, uptime
- 8 robot-themed levels (Unit → Omega)
- Persistent XP storage via ESP32 NVS

### Modes
| Mode | Description | Neon Color |
|------|-------------|------------|
| **SNIFF** | Active WiFi monitoring, capture handshakes | Green/Cyan |
| **SCOUT** | Passive scanning, no transmission | Blue |
| **WARDIVE** | Active wardriving with GPS logging | Orange |
| **SPECTRUM** | Channel analysis | Rainbow |
| **BLE-SNIFF** | BLE device scanning | Blue/Purple |
| **IDLE** | Idle mode | Green |

### StackChan Integration
- Dynamic avatar emotions per mode
- Head movement speed increases with activity
- Neon light indicators color-coded by mode
- Touch interaction for mode cycling

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
- SD card unavailable (firmware bug affecting StackChan)
- Internal flash storage (~2MB) used instead

---

## Build & Flash

```bash
cd firmware
idf.py build
idf.py -p COM8 flash monitor
```

---

## Project Structure

```
firmware/main/
├── apps/app_gotchi/     - Main UI and mode handling
├── gotchi/              - Core scanning logic, XP system, GPS, storage
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
- M5PORKCHOP: https://github.com/M-Tech-Innovation/M5PORKCHOP
- M5Gotchi: https://github.com/xenon-mastodon/M5Gotchi