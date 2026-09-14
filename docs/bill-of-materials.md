# Bill of Materials

Grouped by subsystem. Prices are indicative only — the report did not include costs, so none are listed.

---

## 1. Compute & Display

| # | Item | Qty | Notes |
|---|---|---|---|
| 1 | ESP32 DevKit | 2 | One sensor node, one display node |
| 2 | 4.0" 360×480 IPS TFT, SPI, capacitive touch (FT6336) | 1 | LVGL GUI, sunlight-readable |
| 3 | Perfboard + JST connectors | — | Vibration-resistant soldered interconnect |
| 4 | Micro-USB or USB-C cables | 2 | Flashing + bench power |

---

## 2. Sensors

| # | Item | Qty | Notes |
|---|---|---|---|
| 5 | MAX30102 pulse oximeter | 1 | Red 660 nm + IR 880 nm; I²C `0x57` |
| 6 | MPU6050 6-axis IMU | 1 | I²C `0x68` (or `0x69`) |
| 7 | u-blox NEO-6M GPS module + antenna | 1 | UART NMEA, 9600 baud |
| 8 | TCRT5000 reflective IR sensor | 1 | Analog output, wheel-speed |
| 9 | White reflective strip (half-wheel) | 1 | Improves reflective contrast |
| 10 | I²C pull-up resistors, 4.7 kΩ | 2 | If not already on breakout boards |

---

## 3. Power System

| # | Item | Qty | Notes |
|---|---|---|---|
| 11 | 6 W bicycle dynamo | 1 | Wheel-driven AC source |
| 12 | Bridge rectifier (≥ 2 A, ≥ 50 V) | 1 | AC → pulsating DC |
| 13 | Electrolytic capacitor, 4700 µF | 1 | Ripple smoothing, < 1 V ripple |
| 14 | Buck converter, 15 V → 14 V | 1 | BMS input stage |
| 15 | 1N5822 Schottky diode | 1 | Blocks reverse current to dynamo |
| 16 | 4S LiFePO₄ battery pack | 1 | ~14.4 V nominal |
| 17 | 4S LiFePO₄ BMS board | 1 | Cell balancing + protection |
| 18 | Buck converter, 14 V → 5 V | 1 | Powers both ESP32 boards |
| 19 | DC on/off switch | 1 | System power |
| 20 | Fuses / inline protection | — | Recommended, not in original build |

Full sizing math in [`power-budget.md`](power-budget.md).

---

## 4. Mechanical & Mounting

| # | Item | Qty | Notes |
|---|---|---|---|
| 21 | Waterproof enclosure for electronics | 1 | Bike frame mount |
| 22 | Sensor brackets | — | IR sensor, GPS antenna, display mount |
| 23 | Wiring, heatshrink, zip ties | — | Assembly |
| 24 | Display enclosure / frame | 1 | Handlebar or stem mount |

---

## 5. Optional / Consumables

| # | Item | Qty | Notes |
|---|---|---|---|
| 25 | Solder + flux | — | Perfboard assembly |
| 26 | Silicone sealant | — | Enclosure waterproofing |
| 27 | Velcro / dual-lock strips | — | Removable mount points |

---

## Component justification summary

Short justification for the key choices — details in [`architecture.md`](architecture.md) and [`power-budget.md`](power-budget.md).

### MAX30102 (vs MAX30100 / MAX30105)
The MAX30102 was chosen over the MAX30100 for better SNR, higher LED drive current, integrated ambient-light cancellation, and a cleaner analog front-end. The MAX30105 was rejected outright — it is a **particle / smoke sensor**, not a pulse oximeter, and has no SpO₂ algorithm support.

### NEO-6M (vs NEO-7M / NEO-8M / SIM808)
The NEO-6M provides ~2.5 m CEP horizontal accuracy, runs from 3.3 V, uses ~45 mA while tracking, and is significantly cheaper than the higher-end alternatives. The SIM808 was rejected on power (needs > 2 A peaks) and size.

### TCRT5000 (vs hall-effect)
Reflective IR chosen for non-contact operation, high response speed, immunity to motor EMI, no magnet alignment required, and the ability to register very slow wheel rotations that hall-effect sensors can miss.

### 4S LiFePO₄ (vs Li-ion)
LiFePO₄ tolerates partial-state-of-charge better, has a flatter discharge curve (good for a 5 V buck downstream), and is safer under the temperature and vibration conditions of a bicycle. The 4S pack gives ~14.4 V nominal — enough headroom for the 5 V buck to run cleanly.

### 1N5822 Schottky (vs standard rectifier diode)
Without a series diode, the LiFePO₄ pack would back-feed through the buck and rectifier into the dynamo when the wheel is stationary, wasting energy and stressing the rectifier. The 1N5822 has a low forward drop (~0.45 V at 3 A), blocking reverse flow with minimal efficiency loss. Reverse rating 40 V — comfortable margin over the 14.4 V pack.
