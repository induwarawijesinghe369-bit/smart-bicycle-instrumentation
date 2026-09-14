# Wiring

Two ESP32 boards. **ESP32 #1 (sensor node)** handles all sensors. **ESP32 #2 (display node)** drives the TFT + touch and talks to Firebase. They communicate over **ESP-NOW** — no wires between them.

> GPIO numbers below match the firmware. If your build differs, update `src/firmware/config.h` to match your actual pins.

---

## ESP32 #1 — Sensor node

### I²C bus (MAX30102 + MPU6050)

| Signal | ESP32 #1 GPIO | Notes |
|---|---|---|
| SDA | 21 | shared by both I²C devices |
| SCL | 22 | 400 kHz fast mode |

Both devices sit on the same I²C bus — addresses don't collide:
- **MAX30102** — `0x57`
- **MPU6050** — `0x68` (or `0x69` if AD0 is tied high)

Pull-ups: most breakout boards include 4.7 kΩ on SDA/SCL. If yours don't, add external 4.7 kΩ resistors to 3.3 V.

### NEO-6M GPS (UART)

| Signal | ESP32 #1 GPIO | Notes |
|---|---|---|
| GPS TX → ESP32 RX | 16 | ESP32 UART2 RX |
| GPS RX ← ESP32 TX | 17 | ESP32 UART2 TX |
| VCC | 3.3 V | module is 3.3 V tolerant |
| GND | GND | common ground |

Baud: **9600**, `SERIAL_8N1`.

### TCRT5000 wheel-speed sensor (analog)

| Signal | ESP32 #1 GPIO | Notes |
|---|---|---|
| Analog OUT (AO) | 33 | ADC1 channel — safe to use with Wi-Fi active |
| VCC | 3.3 V | |
| GND | GND | |

Mounting notes:
- Large **white reflective strip** on the wheel (not a small strip) — the report notes that a full half-wheel strip is needed for a strong enough signal.
- Detection threshold: **≈ 3900 ADC counts**.
- State must be confirmed for **≥ 3 consecutive samples** before a transition is accepted (rejects spoke interference).

### Power

- Sensor node VIN ← **5 V rail** from the buck converter on the display node.
- If mounting the two boards far apart, run a dedicated 2-wire 5 V + GND pair, not a shared signal line.

---

## ESP32 #2 — Display node

### 4.0" TFT, 360×480 IPS, SPI + FT6336 capacitive touch

> Pin numbers below are typical for common ESP32 + TFT_eSPI wiring. Adjust `User_Setup.h` in TFT_eSPI to match your build — the report's display is the **4.0" 360×480 SPI V1.0** module.

| TFT / Touch pin | ESP32 #2 GPIO | Notes |
|---|---|---|
| SCLK | 18 | SPI clock |
| MOSI (SDA) | 23 | SPI data out |
| MISO | 19 | SPI data in (needed by touch) |
| CS (LCD) | 15 | TFT chip select |
| DC (RS) | 2 | Data / command |
| RST | 4 | TFT reset |
| BL | 27 | Backlight, PWM-controlled |
| Touch SDA | 21 | FT6336 I²C |
| Touch SCL | 22 | FT6336 I²C |
| Touch RST | 5 | |
| Touch INT | 34 | Input only, no pull-up |

Notes:
- Backlight brightness is set via **PWM on the BL pin** (`applyBrightnessPct()` in firmware).
- Touch uses a **separate I²C bus** on the display node (different pins from the sensor node's I²C bus). Do not cross-connect the two buses.

### Wi-Fi + Firebase
- Wi-Fi credentials and Firebase host/auth are stored in `config.h` (gitignored).
- The display node is the **only** node that connects to Wi-Fi.

### Power

| Source | Voltage | Powers |
|---|---|---|
| 5 V buck output | 5 V | ESP32 #2 VIN, TFT module VCC |
| 3.3 V regulator on ESP32 | 3.3 V | internal logic, TFT logic (check your module) |

The TFT backlight draws ~120–150 mA peak — keep the 5 V buck rated ≥ 1 A total for this node.

---

## Power system

6 W dynamo
│
├─► Bridge rectifier (≥ 2 A, ≥ 50 V)
│
├─► 4700 µF electrolytic (across rectifier output, GND common)
│
├─► Buck 15 V → 14 V
│
├─► 1N5822 Schottky diode (forward only — blocks back-feed from battery)
│
├─► 4S LiFePO₄ BMS input
│
└─► 4S LiFePO₄ battery pack
│
└─► Buck 14 V → 5 V
│
├─► ESP32 #1 VIN (via 2-wire pair)
└─► ESP32 #2 VIN + TFT module VCC


### Grounding
- All grounds common: dynamo return, rectifier GND, BMS GND, battery negative, buck GND, both ESP32 GND, sensor GND.
- Star-ground at the BMS negative terminal to keep high-current return paths away from ADC signals.

---

## Soldering / connector notes

The report explicitly calls out that **breadboards are unsuitable** under vibration. Everything is:
- Soldered on **perfboard** for permanent joints.
- Broken out through **JST connectors** so each sensor module can be removed for testing without desoldering.
- Secured with heatshrink and zip ties at the frame.

Do not use Dupont jumpers on the bike. They will loosen within minutes of riding.

---

## Quick reference — ESP32 #1 pin summary

| Function | GPIO |
|---|---|
| I²C SDA (MAX30102 + MPU6050) | 21 |
| I²C SCL (MAX30102 + MPU6050) | 22 |
| GPS UART RX | 16 |
| GPS UART TX | 17 |
| Wheel IR analog in | 33 |

## Quick reference — ESP32 #2 pin summary

| Function | GPIO |
|---|---|
| TFT SCLK | 18 |
| TFT MOSI | 23 |
| TFT MISO | 19 |
| TFT CS | 15 |
| TFT DC | 2 |
| TFT RST | 4 |
| TFT backlight (PWM) | 27 |
| Touch I²C SDA | 21 |
| Touch I²C SCL | 22 |
| Touch RST | 5 |
| Touch INT | 34 |

Full sizing math in [`power-budget.md`](power-budget.md). Wiring order:
