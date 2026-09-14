# Firmware Design

This folder documents the **firmware design** for the two ESP32 nodes in the Smart Bicycle Instrumentation System.

> **Status:** The original firmware was written and validated on real hardware during the project. The `.ino` source files are not currently included in this repository — they'll be added in a later commit if/when they are recovered. What follows is the complete **design**: polling model, signal-processing chains, communication protocol, and measured results.
>
> Because the design references per-board values (Wi-Fi credentials, Firebase secrets, ESP-NOW peer MAC addresses) that must be filled in on real hardware, **build-and-flash instructions are intentionally omitted.** The design below is sufficient to reconstruct the firmware from scratch.

For the full engineering context see:

- [`../../docs/architecture.md`](../../docs/architecture.md) — full system architecture and signal-processing math
- [`../../docs/wiring.md`](../../docs/wiring.md) — pin assignments for both boards
- [`../../docs/power-budget.md`](../../docs/power-budget.md) — power chain and sizing

---

## Node topology

Two ESP32 boards communicate over **ESP-NOW** — a connectionless, low-latency link that requires no Wi-Fi between them.

| Node | Role | Peripherals |
|---|---|---|
| **Sensor node (ESP32 #1)** | Read sensors, filter, publish telemetry | MAX30102, MPU6050, NEO-6M, TCRT5000 |
| **Display node (ESP32 #2)** | LVGL UI + Wi-Fi + Firebase uplink | 4" 360×480 IPS TFT, FT6336 touch |

Only the display node connects to Wi-Fi. Keeping the sensor node off the network makes its polling loop deterministic — no Wi-Fi TX bursts blocking sensor reads.

---

## Sensor node — design

### Polling model

Cooperative scheduler driven by `millis()`. No `delay()` calls anywhere — every task checks whether its own period has elapsed and runs if so.

| Sensor | Rate | Reason |
|---|---|---|
| MAX30102 (HR + SpO₂) | ~100 Hz FIFO reads, 1 Hz output | Physiological signal bandwidth |
| MPU6050 | 50 Hz | Motion and tilt capture |
| NEO-6M | 1 Hz | NMEA sentence cadence |
| TCRT5000 | ~50 Hz polling | Wheel pulse resolution |

### MAX30102 → heart rate + SpO₂

Pipeline:

1. Read Red + IR raw values from the FIFO buffer.
2. Compute **DC** as a moving average over the sample window.
3. **AC = raw − DC** — isolates the pulsatile component.
4. **1D Kalman filter** smooths the AC (per channel; Q and R derived empirically from measured variance at rest and in motion).
5. **Threshold-based peak detection**:
   - local maximum requirement,
   - amplitude above threshold,
   - enforced minimum peak-to-peak distance to avoid double-counting.
6. `BPM = 60 / (peak-to-peak seconds)`.
7. SpO₂ via **ratio-of-ratios**:

R = (Red_AC / Red_DC) / (IR_AC / IR_DC)
SpO₂ = A·R² + B·R + C

Coefficients adapted from the Maxim reference design.
8. Publish moving-averaged HR and SpO₂ once per second.

**Measured output during validation:** Avg BPM = 73, SpO₂ = 98–99 % with a finger placed firmly on the sensor.

### MPU6050 → roll, pitch, forward acceleration

1. Convert raw signed-16 values to physical units:
- `accel_g = raw / 16384` (±2 g range)
- `gyro_dps = raw / 131` (±250 °/s range)
2. Subtract calibrated **biases** measured at rest (values in `../../docs/architecture.md`).
3. Fuse gyro + accelerometer with a **complementary filter** (α = 0.92):

angle = α · (angle_prev + ω · dt) + (1 − α) · angle_accel

4. Remove gravity from the forward axis:

a_linear = a_x + sin(pitch)


**Validation:** measured roll angle vs. physical protractor across −50° … +50°. Linear regression: **R² = 0.9998**.

### NEO-6M → position

- TinyGPSPlus parses NMEA `$GPGGA` and `$GPRMC`.
- Coordinate format converted from `DDMM.MMMM` → decimal degrees.
- A **2D Kalman filter** was implemented and evaluated (constant-velocity model, lat/lon projected to metres via equirectangular approximation, R derived from 100 stationary samples).
- **Final decision: raw NMEA path used** for the plotted track. The Kalman filter smoothed the path but reduced positional accuracy at low speed. Trade-off documented in `../../docs/architecture.md`.

### TCRT5000 → wheel speed

- Analog threshold ≈ 3900 ADC counts, distinguishing reflective strip vs. gap.
- A state change is only accepted after **≥ 3 consecutive samples** confirm it — rejects spurious pulses from spokes passing the sensor.
- Speed computed from pulse-to-pulse timing.
- Theoretical maximum: `Vmax = π·r / (3 · 20 ms) ≈ 15.7 m/s` — set by the sampling interval.
- **1D Kalman filter** smooths output. Q/R tuning balances noise reduction against responsiveness to braking.



### ESP-NOW telemetry packet

Fixed-size packed struct (~46 bytes) sent per telemetry tick:

```c
struct __attribute__((packed)) TelemetryPacket {
uint32_t millis_now;
uint16_t hr_bpm_x10;         // BPM × 10
uint16_t spo2_x10;           // % × 10
int16_t  ax_g_x1000;
int16_t  ay_g_x1000;
int16_t  az_g_x1000;
int16_t  roll_x10;           // degrees × 10
int16_t  pitch_x10;
int16_t  linear_acc_x1000;
int32_t  lat_e7;             // latitude × 1e7
int32_t  lon_e7;
int16_t  alt_dm;             // decimetres
uint16_t speed_ms_x100;      // m/s × 100
uint8_t  sats;
uint8_t  flags;              // bit0 GPS fix, bit1 finger present, bit2 speed stable
};



Fixed-size binary framing keeps parsing simple on the display node and stays well under ESP-NOW's 250-byte limit.

Display node — design
LVGL rendering strategy
Partial draw buffers, double-buffered — one buffer being rendered while the other is sent to the display.

my_disp_flush() hands completed regions to TFT_eSPI.

lv_tick_inc(5) called every 5 ms to advance LVGL's internal clock.

Event-driven for user input; periodic tasks in loop() for status updates.

Screens
Screen	Contents
Dashboard	Large 7-segment speed digits, live HR waveform + BPM, live SpO₂
Stats / System	Acceleration, roll/pitch, uptime, SSID, IP, Firebase status, min/max HR + SpO₂
Settings	Brightness slider (PWM backlight control)
Wi-Fi popup	Scan and select SSID
Wi-Fi password	On-screen keyboard + Connect button
Navigation is by left/right swipe. A global gesture handler routes swipes to the currently active screen.

Firebase uplink
latest node overwritten every ~2 s — fast path for live dashboards.

history node appended every ~30 s — slow path for later analysis.

firebase_keep_ready() maintains the RTDB connection and reconnects if it drops.

Database paths: /max30102, /mpu6050, /gps, /velocity — schema in ../../docs/architecture.md.

Wi-Fi credential persistence
SSID + password stored in NVS via Preferences, so the display node reconnects automatically after a power cycle without re-entering credentials.

Configuration (for reference)
When the firmware is rebuilt on real hardware, each node needs a private config.h (not committed — contains secrets) with:

Value	Sensor node	Display node
Wi-Fi SSID + password	—	✔
Firebase host + database secret	—	✔
Peer ESP32 MAC address (ESPNOW_PEER_MAC)	✔ (points to display node)	✔ (points to sensor node)
Pin assignments	✔	✔
Sensor and filter tuning constants	✔	✔
Values are documented in ../../docs/wiring.md (pins) and ../../docs/architecture.md (filter constants). The MAC addresses are unique to each physical board and can be read at runtime on the serial monitor.

