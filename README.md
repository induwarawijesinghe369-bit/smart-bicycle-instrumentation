# Smart Bicycle Instrumentation System

An ESP32-based instrumentation system for a bicycle that captures rider heart rate, motion (IMU), GPS position, and wheel speed, then uploads the data to Firebase for real-time IoT monitoring and post-ride analysis.

![Status](https://img.shields.io/badge/status-in%20progress-yellow)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Language](https://img.shields.io/badge/language-C%2B%2B-green)
![License](https://img.shields.io/badge/license-MIT-blue)

---

## 📌 Overview

This project turns a regular bicycle into a connected, instrumented platform. An ESP32 microcontroller collects data from four sensor subsystems and publishes it to Firebase, where it can be viewed live from a dashboard and analysed after a ride.

**Sensors integrated:**

| Sensor | Measured | Interface |
|---|---|---|
| MAX30102 | Heart rate + SpO₂ | I²C |
| MPU6050 (IMU) | Acceleration + gyro (tilt, motion, vibration) | I²C |
| NEO-6M GPS | Latitude, longitude, speed, UTC time | UART |
| Hall-effect switch | Wheel revolutions → speed + distance | GPIO interrupt |

**Data path:**

Sensors → ESP32 → Wi-Fi → Firebase Realtime Database → Dashboard / analysis


---

## 🎯 Objectives

- Read multiple sensor streams on a single microcontroller without blocking.
- Compute derived values (speed, distance, inclination) on-device.
- Publish structured telemetry to Firebase in near-real-time.
- Handle Wi-Fi reconnection and GPS fix acquisition gracefully.
- Log data that can later be replayed, plotted and analysed.

---

## 🧠 System Architecture
┌───────────────┐ I²C ┌──────────┐
│ Heart rate │ ───────► │ │
├───────────────┤ │ │
│ IMU │ ───────► │ ESP32 │ ── Wi-Fi ──► Firebase
├───────────────┤ UART │ │
│ GPS │ ───────► │ │
├───────────────┤ GPIO │ │
│ Wheel sensor │ ───────► │ │
└───────────────┘ └──────────┘

- **I²C bus** shared by heart-rate and IMU sensors (distinct addresses).
- **UART** for GPS NMEA sentences.
- **GPIO interrupt** for wheel pulses; speed derived from pulse interval and wheel circumference.

See [`docs/architecture.md`](docs/architecture.md) for detail.

---

## 🛠️ Hardware

| Component | Purpose |
|---|---|
| ESP32 dev board | Main controller, Wi-Fi |
| MAX30102 pulse sensor | Heart rate |
| MPU6050 IMU | Acceleration + gyro |
| NEO-6M GPS module | Position + speed |
| Hall-effect sensor + magnet | Wheel revolutions |
| LiPo battery + regulator | Portable power |
| Enclosure + mounts | Bike mounting |

Full list in [`docs/bill-of-materials.md`](docs/bill-of-materials.md).

---

## 💻 Software

### Firmware (`src/firmware/`)
- ESP32 Arduino code
- Non-blocking sensor polling
- Wi-Fi + Firebase client
- Speed / distance calculation

### Dashboard (`src/dashboard/`)
- Web dashboard to view live telemetry
- Reads from Firebase Realtime Database

### Analysis (`src/analysis/`)
- Python scripts to export and plot ride data

---

## 📷 Media

*(Ride photos, wiring photos, dashboard screenshots will be added here.)*

---

## 🚀 Getting Started

### Hardware side
1. Wire the sensors as documented in [`docs/wiring.md`](docs/wiring.md).
2. Copy `src/firmware/config.example.h` → `src/firmware/config.h` and fill in your Wi-Fi + Firebase values.
3. Flash the ESP32 with `src/firmware/firmware.ino`.

📁 Repository Structure
text
smart-bicycle-instrumentation/
├── README.md
├── requirements.txt
├── LICENSE
├── docs/               # architecture, wiring, BOM
├── src/
│   ├── firmware/       # ESP32 Arduino code
│   ├── dashboard/      # web dashboard
│   └── analysis/       # Python plotting scripts
└── images/

🗺️ Roadmap
☑ Sensor selection and interfacing
☑ ESP32 firmware skeleton
☑ Wi-Fi + Firebase connectivity
☑ Wheel-speed calculation from hall pulses
☑ GPS NMEA parsing
□ Unified sample-rate scheduler
□ Dashboard UI
□ Ride data export + plotting
□ Enclosure design and bike mount

👤 Author
Induwara Wijesinghe
Mechanical Engineering Undergraduate — Mechatronics
University of Moratuwa
📧 induwarawijesinghe369@gmail.com
### Software side
```bash
git clone https://github.com/induwarawijesinghe369-bit/smart-bicycle-instrumentation.git
cd smart-bicycle-instrumentation
pip install -r requirements.txt


