# JC3248W535EN CyberDeck // Smart IoT Display Terminal

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Framework%20ESP--IDF%205.2.1-orange.svg)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3%208MB%20PSRAM%2016MB%20Flash-red.svg)](https://www.espressif.com/)
[![LVGL](https://img.shields.io/badge/GUI-LVGL%20v8.4.0-blue.svg)](https://lvgl.io/)
[![License](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

An advanced Cyberpunk / Sci-Fi Orbital-HUD IoT operating environment and multimedia workstation tailored for the **JC3248W535EN** (ESP32-S3, 3.5" 320x480 Capacitive Touch IPS display, 8MB Octal PSRAM, 16MB Flash, MicroSD slot, and onboard I2S audio amplifier).

Originally forked from [NorthernMan54/JC3248W535EN](https://github.com/NorthernMan54/JC3248W535EN).

![Board Front](docs/IMG_6782.jpg)

---

## 📸 Live UI Gallery

Native 1:1 hardware captures (480x320) exported via onboard screenshot engine:

| Mission Control Dashboard | System Telemetry & Diagnostics |
| :---: | :---: |
| ![Mission Control Dashboard](docs/snap0001.jpg)<br/>*Orbital HUD Desktop with live RAM/PSRAM stats & module cards* | ![System Telemetry](docs/snap0002.jpg)<br/>*Dual-Core stats, task counter, SRAM/PSRAM gauges & storage* |
| **System Settings Grid (5x2 HUD)** | **Wi-Fi Scanner & Network Recon** |
| ![System Settings](docs/snap0003.jpg)<br/>*Modular 5x2 settings panel with 10 dedicated sub-modules* | ![Wi-Fi Scanner](docs/snap0004.jpg)<br/>*Live 2.4GHz AP scan, RSSI signal indicators & active connection status* |
| **Storage Diagnostics & Capacity** | **Media File Explorer** |
| ![Storage Diagnostics](docs/snap0005.jpg)<br/>*Radial storage allocation gauge, FAT32 mount status & SDMMC bus* | ![Media Explorer](docs/snap0006.jpg)<br/>*Dual-drive file repository with folder navigation across Flash & TF Card* |
| **Motion JPEG Video Player** | **Atmospheric Telemetry (Weather HUD)** |
| ![MJPEG Video Player](docs/snap0007.jpg)<br/>*Real-time ~25 FPS MJPEG player with Fullscreen toggle mode* | ![Atmospheric Telemetry](docs/snap0008.jpg)<br/>*Open-Meteo REST API weather, Solar cycle daylight arc & forecast* |

---

## 🏗️ Architecture & Project Structure

The codebase is organized into modular tiers separating UI rendering, hardware BSP, and FreeRTOS background services:

```
src/
├── ui/                   # Modular GUI Views & Navigation Engine
│   ├── ui_frame.h / .c   # Centralized Header/Footer & Page Navigation Stack
│   ├── gui_launcher.c    # Mission Control Dashboard Desktop
│   ├── gui_settings_page.c  # 5x2 Settings Grid & Non-blocking Wi-Fi Scanner
│   ├── gui_display_settings.c  # Brightness & Screen Saver (Matrix, Life, Stars)
│   ├── gui_clock_page.c        # AMOLED Fullscreen Watchface (US/UK/JP World Clocks)
│   ├── gui_clock_settings.c    # NTP Time Sync & Programmable Chime Alarms
│   ├── gui_power_settings.c    # Battery ADC telemetry & CPU power profiles
│   ├── gui_storage_page.c      # SPIFFS & SDCard partition inspection
│   ├── gui_file_explorer.c     # Multimedia Station (MP3, Video, Images, Text)
│   ├── gui_weather_page.c      # Weather HUD & 5-day forecast
│   ├── gui_weather_settings.c  # City & geo coordinate search
│   ├── gui_system_page.c       # Core diagnostics, heap meters & task list
│   └── gui_about_page.c        # Mission identity editor with virtual keyboard
├── services/             # Core 0 Asynchronous Background Workers
│   ├── wifi_controller.c       # Non-blocking FreeRTOS message queue Wi-Fi manager
│   ├── clock_service.c         # SNTP synchronization & background alarm evaluator
│   ├── weather_service.c       # Open-Meteo HTTPS REST client with JSON parser
│   ├── audio_player.cpp        # Helix MP3 decoder & I2S DMA streaming worker
│   ├── screenshot_service.c    # 1:1 BMP snapshot engine (Touch hold / Serial cmd)
│   └── sys_log.c               # Real-time event logging pipeline
├── bsp/                  # Board Support Package & Drivers
│   ├── esp_bsp.c / .h          # Hardware pin definitions & power init
│   ├── esp_lcd_axs15231b.c     # QSPI LCD Controller driver
│   ├── esp_lcd_touch.c         # I2C Capacitive Touch driver
│   └── lv_port.c               # LVGL 8.4 porting layer & FreeRTOS display task
├── assets/               # Sci-Fi Orbitron fonts & vector weather icons
├── media/                # MJPEG & MP3 stream decoding wrappers
└── DEMO_LVGL.c           # System entry point
```

### Dual-Core Threading Model
* **Core 1 (Rendering)**: Dedicated entirely to LVGL display flushing, touch scanning, and frame animations for butter-smooth 60 FPS interactions.
* **Core 0 (Services)**: Handles Wi-Fi handshakes, HTTP TLS network requests, I2S audio decoding, and SD card file I/O asynchronously via FreeRTOS queues.

---

## ⚡ Key Highlights

### 1. Modular Subpage Frame Engine (`ui_frame`)
* **Standardized Header**: Breadcrumb indicator, digital clock, live Wi-Fi/battery symbols, concise subpage title ($\le$ 8 chars), and real-time orange memory telemetry (`> RAM: %uK :: PSRAM: %uK FREE`).
* **Flexible Display Modes**:
  * `UI_FRAME_DEFAULT`: Header (46px) + Central Content Area (460x224px) + Footer (36px).
  * `UI_FRAME_NO_FOOTER` / `UI_FRAME_NO_HEADER`: Content expands dynamically.
  * `UI_FRAME_FULLSCREEN`: Full 480x320 canvas (used by AMOLED Clock Watchface).
* **Navigation Stack**: Built-in `ui_frame_push` / `ui_frame_pop` with automatic view hiding/restoration and standard `< BACK` footer navigation.
* **Footer Action Slot**: Reserved area on the left for contextual widgets (Wi-Fi Power Switch, Scan Net button, Storage Rescan, Sleep Countdown timer).

### 2. High-Performance Multimedia Station
* **Audio Player**: Native **Helix MP3** and **WAV** playback over I2S to onboard MAX98357A / NS4168 amplifier. Includes a 32-bar pulsing waveform visualizer.
* **Video Player**: Real-time **Motion JPEG (`.mjpeg`)** decoding at ~25 FPS with Fullscreen toggle mode.
* **Image Viewer**: High-speed **JPEG** (`JPEGDEC`) and **BMP** decoders rendering directly to PSRAM with native `LV_COLOR_16_SWAP` byte ordering.
* **Text / Log Viewer**: Phosphor-green terminal for reading `.txt`, `.log`, `.c`, `.json`, `.md`.

### 3. Responsive Wi-Fi & Network Recon
* State changes (ON/OFF) offloaded to FreeRTOS Core 0 queue for lag-free UI responsiveness.
* Enlarged touch hit-box (`lv_obj_set_ext_click_area`) for effortless toggling.
* Live 2.4GHz AP scanner with signal bars and encryption flags.

### 4. Clock, Alarms & Screen Savers
* **NTP Synchronization**: Automated internet time sync with timezone selection.
* **Alarm Engine**: Programmable chime alarms with wake-up dismiss modals.
* **Screen Savers**: Matrix digital rain, Conway's Game of Life, and ambient starfield to protect IPS display backlight and burn-in.

### 5. Hardware Screen Capture
* Capture pixel-perfect 480x320 24-bit BMP screenshots directly to SD Card (`/sdcard/snapXXXX.bmp`) or Flash.
* Triggered by holding the top-right corner for 1.5s or sending `screenshot` via serial console.

---

## 🔌 Hardware Specifications & Pinout

| Peripheral | Controller / Chip | Interface / GPIO Pinout |
| :--- | :--- | :--- |
| **SoC** | ESP32-S3 (240MHz Dual-Core) | 8MB Octal PSRAM, 16MB SPI Flash |
| **Display** | 3.5" IPS 320x480 (AXS15231B / ST7796) | QSPI / 8-bit Parallel SPI (`LV_COLOR_16_SWAP 1`) |
| **Touch Screen** | Capacitive Touch (FT6336 / AXS) | I2C (SCL: GPIO 45, SDA: GPIO 4) |
| **Audio I2S** | MAX98357A / NS4168 Amplifier | **BCK**: 42, **LRCK**: 2, **DOUT**: 41 |
| **MicroSD Card**| Standard TF Card Slot | 1-bit SDMMC / SPI (Mount: `/sdcard`) |
| **Internal Flash**| SPIFFS / FATFS Partition | Mount: `/storage` |

---

## 🛠️ Build & Flash

Ensure [PlatformIO](https://platformio.org/) is installed:

```powershell
# Compile firmware
pio run

# Upload to JC3248W535EN
pio run --target upload

# Open Serial Monitor (115200 baud)
pio device monitor
```

---

## 🗺️ Roadmap
- [ ] **Home Assistant Dashboard**: Integration via WebSockets / REST API for sensor telemetry and smart switches.
- [ ] **BLE HID Macro Pad**: Bluetooth trackpad and shortcut controller for PC/Mac.
- [ ] **Productivity Suite**: Pomodoro focus timer, habit tracker, and quick scratchpad notes.

---

## 📄 License
Released under the [MIT License](LICENSE).
Base BSP by [NorthernMan54](https://github.com/NorthernMan54/JC3248W535EN). Modular UI Frame architecture, dual-core engine, and cyberpunk design system developed for the maker community.