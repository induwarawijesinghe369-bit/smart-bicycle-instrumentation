# System Architecture

## High-level topology

The system is split across **two ESP32 microcontrollers**:

- **Sensor node (ESP32 #1)** — reads all sensors, runs per-sensor filtering, transmits compact telemetry over **ESP-NOW**.
- **Display node (ESP32 #2)** — receives telemetry over ESP-NOW, drives the on-bike TFT + touch UI, and is the only node that talks to Wi-Fi and Firebase.

Splitting the workload this way keeps the UI fluid (LVGL renders at high frame rate without being blocked by sensor polling) and isolates Wi-Fi power spikes to one board.

┌─────────────────────────┐ ┌───────────────────────────┐
│ SENSOR NODE │ │ DISPLAY NODE │
│ (ESP32 #1) │ ESP-NOW │ (ESP32 #2) │
│ │ ──────► │ │
│ I²C: MAX30102, MPU6050 │ │ SPI: 4" TFT + FT6336 │
│ UART: NEO-6M GPS │ │ LVGL UI │
│ ADC: TCRT5000 IR │ │ │
│ │ │ Wi-Fi → Firebase RTDB │
└─────────────────────────┘ └─────────────┬─────────────┘
│
▼
┌───────────────────────────┐
│ Firebase Realtime DB │
│ /max30102 /mpu6050 │
│ /gps /velocity │
└─────────────┬─────────────┘
│
▼




---

## Node 1 — Sensor node

### Responsibilities
- Poll all four sensors at independent rates.
- Run filtering and derived-value computation on-device.
- Emit one compact ESP-NOW packet per telemetry update.
- Never block: no long delays, no Wi-Fi.

### Task rates

| Sensor | Rate | Reason |
|---|---|---|
| MAX30102 (HR + SpO₂) | ~100 Hz sampling, ~1 Hz BPM/SpO₂ output | Physiological bandwidth + stable averages |
| MPU6050 | ~50 Hz | Motion / tilt |
| NEO-6M GPS | 1 Hz (module default) | NMEA sentence cadence |
| TCRT5000 IR wheel | ~20 ms polling (~50 Hz) with ≥ 3-sample state confirmation | Rejects spoke-induced spurious pulses |

### Signal chains

**MAX30102 → HR + SpO₂**


Raw Red / IR from FIFO
│
├─► DC estimate (moving average over buffer)
│
├─► AC = raw − DC
│
├─► 1D Kalman filter (per channel, per Q/R)
│
├─► Threshold peak detection
│ • local maximum
│ • amplitude > threshold
│ • enforced minimum peak-to-peak distance
│
├─► BPM = 60 / (peak-to-peak seconds)
│
└─► SpO₂ via ratio-of-ratios:
R = (Red_AC / Red_DC) / (IR_AC / IR_DC)
SpO₂ = A·R² + B·R + C


Kalman Q/R values were derived empirically from stationary and moving measurements of the raw channels (documented in the report).

**MPU6050 → roll, pitch, forward acceleration**

Raw signed-16 accel + gyro
│
├─► subtract calibrated biases
├─► convert to g and °/s (FSR scale factors)
│
├─► accelerometer angle:
│ roll = atan2(ay, az)
│ pitch = atan2(-ax, sqrt(ay² + az²))
│
├─► gyro integration:
│ angle_g = angle_prev + ω·dt
│
├─► complementary filter (α = 0.92):
│ angle = α·angle_g + (1 − α)·angle_accel
│
└─► forward linear acceleration:
a_linear = a_x + sin(pitch)



Validated against a physical protractor. Linear regression of measured vs. real roll angle gave **R² = 0.9998** — see the report for the data table.

**NEO-6M → position**

UART NMEA stream @ 9600 baud
│
├─► TinyGPS++ parses 
G
P
G
G
A
,
GPGGA,GPRMC, 
G
P
G
S
V
,
GPGSV,GPVTG
├─► decimal degrees = degrees + minutes/60
│
├─► (evaluated) 2D Kalman, constant-velocity model
│ • lat/lon → metres via equirectangular projection:
│ x = (λ − λ₀) · 111320 · cos(φ₀)
│ y = (φ − φ₀) · 111320
│ • state = [x, y, vx, vy]ᵀ
│ • R derived from 100 stationary samples (σ_lat² , σ_lon²)
│
└─► Final choice: raw NMEA path (see “Design trade-offs” below)


**TCRT5000 → wheel speed**

Analog voltage from reflective IR sensor
│
├─► threshold ≈ 3900 ADC counts
├─► state = HIGH (strip present) / LOW (gap)
├─► require ≥ 3 consecutive readings of same state to accept transition
│ → rejects single-spoke interference
│
├─► speed = π·r / Δt (per revolution)
├─► Vmax = π·r / (3·20 ms) ≈ 15.7 m/s (theoretical cap)
│
└─► 1D Kalman filter (process Q, measurement R)
→ balanced responsiveness vs. noise



---

## Node 2 — Display node

### Responsibilities
- Receive ESP-NOW telemetry, keep the latest values in memory.
- Render LVGL UI at high frame rate.
- Manage Wi-Fi, credentials, and Firebase uploads.
- Update the top bar (battery + Wi-Fi icon) across all screens.

### Screen structure

| Screen | Contents |
|---|---|
| **Dashboard** | 7-segment speed, HR waveform + BPM, SpO₂ |
| **Stats / System** | Acceleration, roll/pitch, uptime, SSID, IP, Firebase status, min/max HR/SpO₂ |
| **Settings** | Brightness slider (PWM backlight) |
| **Wi-Fi popup** | Scan, select SSID |
| **Wi-Fi password** | On-screen keyboard, connect button |

Navigation: left/right swipe + top-bar buttons. Global gesture handler (`global_gesture_cb`) routes gestures to the active screen.

### LVGL rendering approach
- Small **draw buffers** (partial-screen) — LVGL renders a section, then hands it to `my_disp_flush()`.
- **Double buffering** — one buffer being drawn while the other is filled → no tearing.
- **Tick** every 5 ms via `lv_tick_inc(5)`.
- **Event-driven** for buttons/sliders; periodic tasks in `loop()` for want-driven updates (`refresh_bars`, `wifi_update_icons`, `stats_update_values`).

---

## Firebase structure

Realtime Database with four top-level sensor nodes. Each has a `latest` and a `history` branch, matching the ESP32 firmware's dual-rate update strategy (fast for `latest`, slow for `history`).

/max30102/
latest/
hr: 78
spo2: 98
signal_quality: "good"
finger: true
ts: 1730000000
history/
{ts}/
hr: 78
spo2: 98

/mpu6050/
latest/
ax: 0.02 ay: -0.01 az: 9.79
roll: 1.2 pitch: -0.8
ts: ...
history/
{ts}/ ...

/gps/
latest/
lat: 6.7945
lon: 79.90066
speed_kmh: 12.4
alt: 22.1
sats: 10
hdop: 1.25
ts: ...
history/
{ts}/ ...

/velocity/
latest/
speed_ms: 4.7
speed_kalman: 4.6
stable: true
consecutive: 467
ts: ...
history/
{ts}/ ...



---

## Data-flow summary

Sensors ─► ESP32 #1 ─► ESP-NOW ─► ESP32 #2 ─► Wi-Fi ─► Firebase ─► HTML dashboards
(local UI has
zero latency)


- Sensor → display latency (ESP-NOW): sub-10 ms.
- Display → Firebase: `latest` every ~2 s, `history` every ~30 s.
- Dashboard → Firebase: JS SDK reads on a live `on('value')` listener per node.

---

## Design trade-offs

### Single ESP32 vs. dual ESP32
A single board could run everything, but Wi-Fi transmission blocks the CPU for short bursts. That caused LVGL animations to stutter and occasionally dropped HR samples. Splitting into sensor + display nodes over ESP-NOW eliminated both problems and made the code more modular.

### Kalman on GPS: accuracy vs. smoothness
A 2D constant-velocity Kalman was implemented and evaluated. It produced a smoother plotted path but slightly **lower positional accuracy** when the bike was near stationary (the model "predicts" motion that isn't happening). The raw NMEA path was chosen for the plotted track to preserve accuracy. This is documented honestly rather than hidden — see the report's comparison plots.

### Kalman on wheel speed: responsiveness vs. noise
The 1D speed Kalman has tunable process Q and measurement R. Reducing Q (or raising R) gives smoother but slower response — noticeable when braking hard. The chosen values balance the two for normal riding.

### Reflective IR vs. hall-effect for wheel speed
Chosen for: non-contact, high response speed, immune to motor EMI, no magnet alignment, low cost, and ability to detect very slow rotations (hall sensors can miss slow, small field changes). Downside: sensitive to ambient light and dirt on the wheel — mitigated by a large white strip and threshold-based state logic.
┌───────────────────────────┐
│ HTML dashboards (JS SDK) │
└───────────────────────────┘
