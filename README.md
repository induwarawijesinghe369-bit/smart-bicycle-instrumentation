# Smart Bicycle Instrumentation System

An IoT bicycle instrumentation platform built on **two ESP32 microcontrollers** that measures **heart rate, SpO₂, motion (accel + gyro), GPS position, and wheel speed**, displays them live on an **on-bike TFT touchscreen**, and streams the data to **Firebase Realtime Database** for real-time web dashboards and post-ride analysis.

![Status](https://img.shields.io/badge/status-working-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32%20×%202-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B%20%7C%20Python-green)
![License](https://img.shields.io/badge/license-MIT-blue)

---

## 📌 Overview

The system is split across two ESP32 boards connected over **ESP-NOW** — a low-latency, connectionless link that keeps the display responsive without needing Wi-Fi for local data:

- **Sensor node** — reads MAX30102 (HR + SpO₂), MPU6050 (IMU), NEO-6M (GPS), and a reflective IR wheel-speed sensor. Runs per-sensor filtering (moving average + Kalman + complementary filter) on-device and transmits compact telemetry over ESP-NOW.
- **Display node** — receives telemetry over ESP-NOW, drives a **4" 360×480 IPS TFT with capacitive touch** running **LVGL**, and is the only board that talks to Wi-Fi and Firebase.

The bike also carries a **6 W dynamo → rectifier → 4S LiFePO₄ BMS → 5 V buck** power chain that supplies both boards and holds charge between rides.

---

## 🎯 What the system measures

| Quantity | Sensor | Method |
|---|---|---|
| Heart rate (BPM) | MAX30102 | PPG AC peak detection on IR channel |
| SpO₂ (%) | MAX30102 | Ratio-of-ratios on Red/IR AC & DC |
| Acceleration (g) | MPU6050 | Calibrated raw ÷ scale factor |
| Roll / Pitch (°) | MPU6050 | Complementary filter (α = 0.92) |
| Latitude / Longitude | NEO-6M | NMEA parsing (GPGGA, GPRMC) |
| Speed (km/h) | NEO-6M | From `$GPRMC` |
| Wheel speed (m/s) | TCRT5000 IR | Reflective strip + pulse timing |

---

## 🏗️ System Architecture
┌──────────────────────┐ ┌────────────────────────┐
│ SENSOR NODE │ ESP- │ DISPLAY NODE │
│ (ESP32 #1) │ NOW │ (ESP32 #2) │
│ │ ──────► │ │
│ MAX30102 (I²C) │ │ 4" TFT + touch (SPI) │
│ MPU6050 (I²C) │ │ LVGL UI │
│ NEO-6M (UART) │ │ │
│ TCRT5000 (Analog) │ │ Wi-Fi → Firebase RTDB │
└──────────────────────┘ └───────────┬────────────┘
│
▼
┌──────────────────────────┐
│ Firebase Realtime DB │
│ /max30102 /mpu6050 │
│ /gps /velocity │
└──────────┬───────────────┘
│
▼
┌──────────────────────────┐
│ HTML Dashboards │
│ (health / motion / │
│ GPS / velocity) │
└──────────────────────────┘


Full detail in [`docs/architecture.md`](docs/architecture.md).

---

## 🛠️ Hardware

**Compute & display**
- 2 × ESP32 DevKit
- 4.0" 360×480 IPS TFT, SPI, FT6336 capacitive touch

**Sensors**
- MAX30102 — pulse oximeter (Red 660 nm + IR 880 nm)
- MPU6050 — 6-axis IMU
- u-blox NEO-6M — GPS (UART NMEA)
- TCRT5000 — reflective IR wheel sensor

**Power**
- 6 W bicycle dynamo → bridge rectifier → 4700 µF smoothing
- Buck 15 V → 14 V → 1N5822 → 4S LiFePO₄ BMS
- Buck 14 V → 5 V → both ESP32 boards

Full list in [`docs/bill-of-materials.md`](docs/bill-of-materials.md).  
Sizing math in [`docs/power-budget.md`](docs/power-budget.md).

---

## 🧠 Signal Processing

### MAX30102 — heart rate & SpO₂
1. Read Red + IR raw values from FIFO.
2. Remove DC with a **moving average** over a buffer → AC component.
3. Smooth the AC with a **1D Kalman filter** (per-channel Q, R tuned from measurement variance).
4. **Threshold-based peak detection** with a minimum peak-to-peak distance → BPM.
5. SpO₂ via **ratio-of-ratios**:

   `R = (Red_AC / Red_DC) / (IR_AC / IR_DC)`  
   `SpO₂ = A·R² + B·R + C`

   with coefficients adapted from the Maxim reference.

### MPU6050 — roll, pitch, linear acceleration
1. Convert raw signed-16 values to g and °/s using FSR scale factors.
2. Subtract factory/field-measured **biases** for gyro and accel.
3. Fuse with a **complementary filter** (α = 0.92):
   - `angle = α·(angle + ω·dt) + (1 − α)·angle_accel`
4. Remove gravitational component from forward acceleration:  
   `linear_acc = ax_g + sin(pitch)`
5. Validated against a physical protractor with a **linear regression R² = 0.9998**.

### NEO-6M GPS — position
1. Parse NMEA `$GPGGA` / `$GPRMC`.
2. Convert DDMM.MMMM → decimal degrees.
3. Logged a **2D Kalman filter** (constant-velocity model, lat/lon → metres via equirectangular projection) with measurement noise R derived from 100 stationary samples.
4. **Deliberate decision:** after testing, the raw NMEA path was chosen for the plotted track because the KF traded **accuracy for smoothness** — see the report discussion. Raw data preserved for honesty about the trade-off.

### TCRT5000 — wheel speed
1. Reflective strip on the wheel creates High/Low transitions at the sensor (threshold ≈ 3900 ADC).
2. Require ≥ 3 repeated readings of the same state to accept a transition (rejects spoke interference).
3. Compute speed from pulse timing, capped at  
   `Vmax = π·r / (3·20 ms)` ≈ 15.7 m/s.
4. **1D Kalman filter** smooths speed; Q/R tuned to balance responsiveness vs. noise during braking.

---

## 🖥️ Display UI (LVGL)

- **Dashboard** — large 7-segment speed display, live HR waveform + BPM, live SpO₂.
- **Stats / System** — acceleration, roll/pitch, uptime, Wi-Fi SSID/IP, Firebase status, min/max HR and SpO₂.
- **Settings** — brightness slider (PWM backlight).
- **Wi-Fi popup** — scan networks, select SSID, enter password, connect.
- Swipe navigation between screens; persistent top bar with battery + Wi-Fi status.

---

## 🌐 Web Dashboards

Four HTML pages read directly from Firebase RTDB via the JS SDK:

| Page | Shows |
|---|---|
| **Health Monitor** | Live HR, SpO₂, signal quality, historical chart |
| **Motion Sensor** | 3-axis acceleration, roll/pitch, linear acceleration |
| **GPS Tracker** | Leaflet map, live position, path history |
| **Velocity Sensor** | Live speed, Kalman-filtered speed, stability, stats |

---

## 📷 Media

*(Add photos of the bike, the display UI, and the dashboards here.)*

---

## 🚀 Getting Started

### Hardware
1. Wire sensors per [`docs/wiring.md`](docs/wiring.md).
2. Copy `src/firmware/config.example.h` → `src/firmware/config.h` and fill in Wi-Fi + Firebase.
3. Flash `src/firmware/sensor_node.ino` to ESP32 #1.
4. Flash `src/firmware/display_node.ino` to ESP32 #2.

📁 Repository Structure

smart-bicycle-instrumentation/
├── README.md
├── requirements.txt
├── LICENSE
├── docs/
│   ├── architecture.md
│   ├── wiring.md
│   ├── bill-of-materials.md
│   └── power-budget.md
├── src/
│   ├── firmware/          # ESP32 code (sensor node + display node)
│   ├── dashboard/         # HTML + Firebase JS dashboards
│   └── analysis/          # Python ride analysis
└── images/

🗺️ Roadmap
☑ Sensor selection and comparison (MAX30102 vs 30100 vs 30105, NEO-6M vs 7M/8M)
☑ Dual-ESP32 architecture with ESP-NOW
☑ MAX30102: moving average + Kalman + peak detection + SpO₂
☑ MPU6050: calibration + complementary filter, regression-validated
☑ NEO-6M: NMEA parsing + Kalman evaluation
☑ TCRT5000: reflective speed sensing + noise rejection + Kalman
☑ LVGL GUI with dashboard, settings, Wi-Fi popup, stats
☑ Firebase RTDB + four HTML dashboards
☑ Dynamo → BMS → 5 V power chain with justified sizing
□ CAD design of the on-bike enclosure
□ Final ride validation + demo video

👤 Author
Induwara Wijesinghe
Mechanical Engineering Undergraduate — Mechatronics
University of Moratuwa
📧 induwarawijesinghe369@gmail.com

### Software
```bash
git clone https://github.com/induwarawijesinghe369-bit/smart-bicycle-instrumentation.git
cd smart-bicycle-instrumentation
pip install -r requirements.txt
