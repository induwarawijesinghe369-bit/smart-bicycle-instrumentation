/*
 * Smart Bicycle Instrumentation — shared configuration template
 *
 * Copy this file to config.h in the same folder and fill in your own
 * Wi-Fi and Firebase values. config.h is gitignored so credentials
 * never end up in the repository.
 *
 *   cp src/firmware/config.example.h src/firmware/config.h
 *
 * Pin numbers below match docs/wiring.md. Update them to match your build.
 */

#pragma once

// ─────────────────────────────────────────────────────────────────────────
//  Wi-Fi
// ─────────────────────────────────────────────────────────────────────────
#define WIFI_SSID           "your-ssid"
#define WIFI_PASSWORD       "your-password"

// ─────────────────────────────────────────────────────────────────────────
//  Firebase Realtime Database
// ─────────────────────────────────────────────────────────────────────────
#define FIREBASE_HOST       "https://your-project.firebaseio.com"
#define FIREBASE_AUTH       "your-database-secret"

// Update cadence to Firebase (ms)
#define FB_LATEST_PERIOD_MS     2000    // fast path — overwrite
#define FB_HISTORY_PERIOD_MS    30000   // slow path — append

// ─────────────────────────────────────────────────────────────────────────
//  ESP-NOW (sensor node → display node)
// ─────────────────────────────────────────────────────────────────────────
// 6-byte MAC of the peer node. Fill in with the receiver's MAC after
// flashing and reading it from the serial monitor.
#define ESPNOW_PEER_MAC     { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }
#define ESPNOW_CHANNEL      1       // must match on both nodes

// ─────────────────────────────────────────────────────────────────────────
//  Sensor node pins (ESP32 #1)
// ─────────────────────────────────────────────────────────────────────────
// I2C — shared by MAX30102 and MPU6050
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

// GPS — UART2
#define PIN_GPS_RX          16      // ESP32 RX ← GPS TX
#define PIN_GPS_TX          17      // ESP32 TX → GPS RX
#define GPS_BAUD            9600

// Wheel IR sensor — analog
#define PIN_WHEEL_IR        33

// ─────────────────────────────────────────────────────────────────────────
//  Display node pins (ESP32 #2)
// ─────────────────────────────────────────────────────────────────────────
#define PIN_TFT_SCLK        18
#define PIN_TFT_MOSI        23
#define PIN_TFT_MISO        19
#define PIN_TFT_CS          15
#define PIN_TFT_DC          2
#define PIN_TFT_RST         4
#define PIN_TFT_BL          27      // PWM backlight

#define PIN_TOUCH_SDA       21
#define PIN_TOUCH_SCL       22
#define PIN_TOUCH_RST       5
#define PIN_TOUCH_INT       34

// ─────────────────────────────────────────────────────────────────────────
//  Sample rates (sensor node)
// ─────────────────────────────────────────────────────────────────────────
#define HR_SAMPLE_MS        10      // ~100 Hz MAX30102 FIFO read
#define HR_OUTPUT_MS        1000    // 1 Hz smoothed HR + SpO2 publish
#define IMU_SAMPLE_MS       20      // 50 Hz
#define GPS_SAMPLE_MS       1000    // 1 Hz
#define WHEEL_POLL_MS       20      // ~50 Hz

// ─────────────────────────────────────────────────────────────────────────
//  MAX30102 — PPG + SpO2
// ─────────────────────────────────────────────────────────────────────────
#define MAX30102_LED_BRIGHTNESS     0x1F
#define MAX30102_SAMPLE_AVERAGE     4
#define MAX30102_LED_MODE           2       // 2 = Red + IR
#define MAX30102_SAMPLE_RATE        100
#define MAX30102_PULSE_WIDTH        411     // 18-bit ADC
#define MAX30102_ADC_RANGE          4096

#define HR_BUFFER_SIZE              100     // AC sample window
#define SPO2_ARRAY_SIZE             4       // moving-average over SpO2 readings
#define SPO2_CALC_INTERVAL_MS       1000
#define FINGER_THRESHOLD_IR         50000   // IR DC above this ⇒ finger present

// Kalman tuning for MAX30102 (per channel) — from empirical measurement
#define HR_KF_Q_IR          20545.7f
#define HR_KF_R_IR          684856.67f
#define HR_KF_Q_RED         2753.11f
#define HR_KF_R_RED         91770.43f
#define HR_KF_P_INIT        100000.0f

// ─────────────────────────────────────────────────────────────────────────
//  MPU6050 — IMU
// ─────────────────────────────────────────────────────────────────────────
// Measured at rest on a flat, vibration-free surface
#define GYRO_BIAS_X         0.1828f
#define GYRO_BIAS_Y         0.1466f
#define GYRO_BIAS_Z         0.6659f

#define ACCEL_BIAS_X       -0.0165f
#define ACCEL_BIAS_Y        0.0083f
#define ACCEL_BIAS_Z        1.1941f

#define COMP_FILTER_ALPHA   0.92f   // 92 % gyro, 8 % accel

// ─────────────────────────────────────────────────────────────────────────
//  TCRT5000 — wheel speed
// ─────────────────────────────────────────────────────────────────────────
#define WHEEL_IR_THRESHOLD      3900    // ADC counts
#define WHEEL_CONFIRM_COUNT     3       // min consecutive samples for a state change
#define WHEEL_RADIUS_M          0.3f    // metres (from the report)
#define WHEEL_SPEED_MAX_MS      15.71f  // theoretical cap (π·r / (3·20 ms))

// 1D Kalman on speed — tune Q/R per docs/architecture.md
#define SPEED_KF_Q              0.01f
#define SPEED_KF_R              0.2f
#define SPEED_STABLE_BAND       0.10f   // ±10 % considered "stable"

// ─────────────────────────────────────────────────────────────────────────
//  GPS — NEO-6M
// ─────────────────────────────────────────────────────────────────────────
// Reference point for lat/lon → metres projection, chosen at calibration site
#define GPS_REF_LAT             6.794428f
#define GPS_REF_LON             79.900701f

// Metres per degree latitude (used for equirectangular projection)
#define METRES_PER_DEG_LAT      111320.0f

// ─────────────────────────────────────────────────────────────────────────
//  Firebase paths
// ─────────────────────────────────────────────────────────────────────────
#define FB_PATH_HR_LATEST       "/max30102/latest"
#define FB_PATH_HR_HISTORY      "/max30102/history"
#define FB_PATH_IMU_LATEST      "/mpu6050/latest"
#define FB_PATH_IMU_HISTORY     "/mpu6050/history"
#define FB_PATH_GPS_LATEST      "/gps/latest"
#define FB_PATH_GPS_HISTORY     "/gps/history"
#define FB_PATH_VEL_LATEST      "/velocity/latest"
#define FB_PATH_VEL_HISTORY     "/velocity/history"

// ─────────────────────────────────────────────────────────────────────────
//  Session
// ─────────────────────────────────────────────────────────────────────────
#define DEVICE_ID               "bike-01"   // shown on dashboards
