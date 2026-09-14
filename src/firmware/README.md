# Firmware

ESP32 firmware for the Smart Bicycle Instrumentation System. Two roles, both in this folder:

| File | Purpose |
|---|---|
| `combined_node.ino` | Full multi-sensor firmware — MAX30102 + MPU6050 + GPS + IR speed, with Firebase uplink |
| `test_gps.ino` | GPS-only test sketch |
| `test_max30102.ino` | MAX30102-only test sketch (HR + SpO₂) |
| `test_mpu6050.ino` | MPU6050-only test sketch (no Firebase) |
| `test_mpu6050_firebase.ino` | MPU6050 + Firebase test sketch |
| `test_velocity.ino` | IR wheel-speed test sketch with Kalman filter |
| `config.example.h` | Template for the private `config.h` (Wi-Fi + Firebase credentials) |

The individual `test_*.ino` sketches were used during development to validate each sensor in isolation. `combined_node.ino` is the integrated firmware that runs on the bike.

---

## Setup

Each `.ino` file includes `config.h`, which holds Wi-Fi credentials and Firebase tokens. `config.h` is **not** in this repository — it's gitignored so secrets stay private.

### Create your own `config.h`

**On Windows/Mac/Linux (recommended):**

1. In this folder, make a copy of `config.example.h` and name it `config.h`.
2. Open `config.h` in any text editor.
3. Fill in your own values:
   - Wi-Fi SSID + password
   - Firebase host URL + database secret

**On the command line:**

```bash
cd src/firmware
cp config.example.h config.h
