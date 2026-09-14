# Power Budget & Sizing

This document walks through the power system from the dynamo to the 5 V rail that feeds both ESP32 boards, with the reasoning and numbers used to size each stage.

---

## Overview

6 W dynamo
│ AC
▼
Bridge rectifier ──► 4700 µF smoothing cap
│ 15 V DC (approx.)
▼
Buck 15 V → 14 V
│
▼
1N5822 Schottky diode (forward only)
│
▼
4S LiFePO₄ BMS ──► 4S LiFePO₄ battery pack
│ ~14.4 V nominal
▼
Buck 14 V → 5 V
│
├──► ESP32 #1 (sensor node)
└──► ESP32 #2 (display node + 4" TFT)


The battery sits in the middle as a **buffer**: the dynamo charges it whenever the wheel turns, and the 5 V buck draws from it continuously, so the ESP32 boards see a stable rail even when the dynamo output is zero (stopped) or spiky (fast descent).

---

## 1. Dynamo & rectification

**Given**

- Dynamo rated power: 6 W
- Output after bridge rectifier + smoothing: ≈ 15 V DC

**Current capability after rectification**
I = P / V = 6 W / 15 V = 0.4 A


So the rectified side can deliver **0.4 A at 15 V**.

**Power delivered to the BMS input (14 V rail)**

P = V · I = 14 V × 0.4 A = 5.6 W


---

## 2. 15 V → 14 V buck (BMS charging stage)

**Target:** charge a **4S LiFePO₄** pack.

- 4S LiFePO₄ nominal = 4 × 3.6 V = **14.4 V**
- Max charge voltage for 4S LiFePO₄ ≈ **14.6 V**

**Choice:** buck to **14 V** rather than 14.6 V.

**Reason:** the 14 V setpoint sits slightly below the max, giving a conservative margin that avoids over-voltage if the buck's feedback drifts. The BMS handles the final cell-level regulation and balancing; the buck just needs to supply a clean, current-limited source.

**Current available to the battery**

I = P / V = 5.6 W / 14 V = 0.4 A


**Justification:** 0.4 A is a slow charge — ideal for LiFePO₄, which prefers low-C-rate charging for cell health. Cell balancing on a 4S pack with modest currents is gentler and stays well within the BMS's thermal limits.

---

## 3. 14 V → 5 V buck (ESP32 rail)

**Load estimate**

| Load | Typical | Peak |
|---|---|---|
| ESP32 #1 core | 80 mA | 250 mA |
| — NEO-6M GPS | 45 mA | 60 mA |
| — MAX30102 | 6 mA | 15 mA |
| — MPU6050 | 4 mA | 6 mA |
| **ESP32 #1 total** | **~135 mA** | **~330 mA** |
| ESP32 #2 core | 80 mA | 250 mA |
| — 4" TFT backlight + logic | 120 mA | 150 mA |
| **ESP32 #2 total** | **~200 mA** | **~400 mA** |
| **Combined** | **~335 mA** | **~730 mA** |

**Working number:** allow **~0.7 A at 5 V** for design margin.

**Power required on the 5 V rail**

P = V · I = 5 V × 0.7 A = 3.5 W


**Power available from battery** (assuming ~90 % buck efficiency):

P_avail = 14 V × 0.4 A × 0.9 = 5.04 W


**Margin**
5.04 W − 3.5 W = 1.54 W


That 1.5 W of headroom absorbs:
- ESP32 Wi-Fi TX spikes (~500 mA for tens of milliseconds),
- TFT backlight PWM switching,
- buck switching losses at light load.

Conclusion: **sufficient**.

---

## 4. Capacitor sizing (4700 µF)

**Purpose:** smooth the pulsating DC from the bridge rectifier and supply short load-current spikes.

**Ripple voltage (full-wave rectifier)**

V_ripple = I_load / (f · C)


Where:
- `I_load = 0.4 A`
- `f ≈ 100 Hz` (assuming the dynamo's AC output is ~50 Hz at cruise and full-wave rectification doubles it)
- `C = 4700 µF = 0.0047 F`

V_ripple = 0.4 / (100 × 0.0047) = 0.85 V


**Result:** ripple < 1 V — the DC level stays at ~15 V ± 0.85 V, well above the 14 V buck's minimum input.

**Why 4700 µF specifically**

- **Too small** → larger ripple → voltage dips below 14 V → the BMS charge current becomes intermittent.
- **Too large** → larger inrush current at power-up → stresses the bridge rectifier and dynamo windings.
- **4700 µF** hits a sensible middle ground: ripple < 1 V, moderate inrush, standard electrolytic footprint, low cost.

---

## 5. Diode selection (1N5822)

**Without a diode:** the 4S LiFePO₄ pack (14.4 V) would have a direct path back through the 14 V buck and bridge rectifier to the dynamo. When the wheel is stationary, the dynamo side sits near 0 V → current flows backward from the pack to the dynamo.

**Consequences without a diode**
- Battery discharges through the dynamo → wasted energy
- Potential damage to rectifier diodes / dynamo windings from reverse current

**Solution:** place a **1N5822 Schottky diode** between the buck output and the BMS input.

**Why 1N5822**

| Property | Value | Why it matters |
|---|---|---|
| Forward drop | ~0.45 V @ 3 A | Low loss at 0.4 A operating current |
| Forward current rating | 3 A | ≥ 7× nominal current — thermally safe |
| Reverse voltage rating | 40 V | Comfortable margin over 14.4 V pack |
| Type | Schottky | Low Vf compared to standard rectifiers |

**Reverse protection logic**

- Battery-side voltage = 14.4 V max
- Diode reverse rating = 40 V → large safety margin
- Forward current = 0.4 A nominal, 3 A rated → no thermal stress

**Efficiency impact**
P_loss = V_fwd × I = 0.45 V × 0.4 A ≈ 0.18 W


0.18 W loss on a 5.6 W path = ~3 %. Acceptable.

---

## 6. Bridge rectifier selection

- **Voltage:** rated for hundreds of volts, dynamo produces ~15 V → very large margin.
- **Current:** rated 2 A, dynamo current ~0.4 A → 5× headroom against inrush and surges.

Conclusion: no thermal stress, long-term reliability.

---

## 7. System justification summary

1. **Dynamo → battery → ESP32** chain provides mechanical-to-electrical conversion and stable downstream power regardless of wheel speed.
2. **Battery buffer** protects the ESP32 boards from voltage spikes and provides power when the bike is stationary.
3. **Two-stage buck conversion** (15 → 14 V for the BMS, 14 → 5 V for logic) gives clean regulation at each stage and avoids stressing a single converter with both battery-charging and logic-supply duties.
4. **LiFePO₄ + BMS** provides cell balancing and over-charge protection.
5. **Component sizing:**
   - 6 W dynamo → 0.4 A at 15 V
   - 4S LiFePO₄ pack → ~14 V nominal on the charge rail
   - ESP32 boards + sensors + TFT → ~3.5 W at 5 V
   - Available power ~5 W → ~1.5 W margin

The system is comfortably sized for continuous operation of both ESP32 boards with all peripherals active.

---

## Summary table

| Stage | Input | Output | Current | Notes |
|---|---|---|---|---|
| Dynamo | mechanical | 6 W AC | — | Wheel-driven |
| Rectifier | 6 W AC | 15 V DC pulsating | 0.4 A | Full-wave |
| Smoothing cap | 15 V pulsating | 15 V ± 0.85 V | 0.4 A | 4700 µF |
| Buck #1 | 15 V | 14 V | 0.4 A | BMS charge rail |
| Diode | 14 V | 14 V − 0.45 V | 0.4 A | 1N5822 |
| BMS + battery | 14 V in | ~14.4 V out | 0.4 A | 4S LiFePO₄ |
| Buck #2 | 14 V | 5 V | ≤ 0.7 A | ESP32 rail |
| Load | 5 V | — | ~0.33 A avg / 0.73 A peak | Both ESP32 boards + peripherals |



I = P / V = 6 W / 15 V = 0.4 AI = P / V = 6 W / 15 V = 0.4 A
