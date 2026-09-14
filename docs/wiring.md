# Wiring

## ESP32 pin assignment

> Adjust to match your build.

| Signal | ESP32 GPIO | Notes |
|---|---|---|
| I²C SDA | 21 | shared by HR + IMU |
| I²C SCL | 22 | shared by HR + IMU |
| GPS RX | 16 | ESP32 RX ← GPS TX |
| GPS TX | 17 | ESP32 TX → GPS RX |
| Wheel sensor | 27 | interrupt on FALLING |
| Status LED | 2 | onboard |

## I²C devices

- MAX30102 heart-rate sensor — default address `0x57`
- MPU6050 IMU — default address `0x68` (or `0x69` if AD0 high)

Both share the same I²C bus; addresses don't conflict.

## GPS

- NEO-6M module, 9600 baud UART
- TX pin of the module → ESP32 GPIO 16
- Power from 3.3 V (module is 3.3 V tolerant)

## Wheel sensor

- Hall-effect switch to GPIO 27 with a 10 kΩ pull-up to 3.3 V
- Magnet glued to a spoke
- Pulses per revolution = 1 (adjust firmware if using more magnets)

## Power

- 3.7 V LiPo → 5 V boost (or 3.3 V LDO) → ESP32 VIN
- Buck converter strongly recommended: ESP32 draws Wi-Fi current spikes up to 500 mA

## Grounding

All grounds (ESP32, sensors, battery) must be common.
