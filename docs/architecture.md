# System Architecture

## Microcontroller

Single ESP32 (dual-core, 240 MHz, Wi-Fi + BLE) handles all sensor reading, computation and uploads.

## Task scheduling

The main loop runs a cooperative scheduler — each sensor is polled at its own rate:

| Sensor | Rate | Reason |
|---|---|---|
| Heart rate | 10 Hz | Physiological signal bandwidth |
| IMU | 50 Hz | Motion / vibration capture |
| GPS | 1 Hz | NMEA sentence cadence |
| Wheel pulse | event-driven | Interrupt on each magnet pass |

Firebase uploads are throttled to ~2 Hz to avoid hammering the endpoint.


## Connectivity

1. On boot, connect to Wi-Fi (blocking with timeout).
2. Initialise Firebase client.
3. If Wi-Fi drops: buffer locally (ring buffer), retry with exponential backoff.
4. On reconnect: flush buffered samples.

## Firebase schema
