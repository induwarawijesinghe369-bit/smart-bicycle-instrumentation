#include "config.h"

#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <FirebaseESP32.h>

// =============================================
// WiFi Configuration
// =============================================
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

// =============================================
// Firebase Configuration
// =============================================
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// =============================================
// MPU6050 Configuration
// =============================================
const int MPU_ADDR = 0x68;  // I2C address

// =============================================
// CALIBRATION VALUES
// =============================================
const float ACCEL_BIAS_X = -0.0165;
const float ACCEL_BIAS_Y = 0.0083;
const float ACCEL_BIAS_Z = 1.1941;  // This is your actual Z reading when flat

const float GYRO_BIAS_X = 0.1828;
const float GYRO_BIAS_Y = 0.1466;
const float GYRO_BIAS_Z = 0.6659;

// =============================================
// Global Variables
// =============================================
float rollAngle = 0;
float pitchAngle = 0;
float ax_g = 0, ay_g = 0, az_g = 0;
float gx_dps = 0, gy_dps = 0, gz_dps = 0;
unsigned long prevTime = 0;

// Complementary filter (92% gyro, 8% accel)
const float ALPHA = 0.92;

// =============================================
// Timers for Firebase Updates
// =============================================
unsigned long lastLiveUpdate = 0;
const unsigned long LIVE_UPDATE_INTERVAL = 2000;  // 2 seconds

unsigned long lastHistoryUpdate = 0;
const unsigned long HISTORY_UPDATE_INTERVAL = 30000;  // 30 seconds

// =============================================
// Firebase Upload Functions
// =============================================
void uploadLiveData() {
  FirebaseJson json;
  json.set("roll", rollAngle);
  json.set("pitch", pitchAngle);
  json.set("accel_x", ax_g);
  json.set("accel_y", ay_g);
  json.set("accel_z", az_g);
  json.set("gyro_x", gx_dps);
  json.set("gyro_y", gy_dps);
  json.set("gyro_z", gz_dps);
  json.set("timestamp", millis());
  
  Serial.println("Uploading live data to Firebase...");
  
  if (Firebase.setJSON(fbdo, "/mpu6050/latest", json)) {
    Serial.println("Live data uploaded successfully!");
  } else {
    Serial.println("Failed to upload live data:");
    Serial.println(fbdo.errorReason());
  }
}

void uploadHistoryData() {
  FirebaseJson json;
  json.set("roll", rollAngle);
  json.set("pitch", pitchAngle);
  json.set("accel_x", ax_g);
  json.set("accel_y", ay_g);
  json.set("accel_z", az_g);
  json.set("gyro_x", gx_dps);
  json.set("gyro_y", gy_dps);
  json.set("gyro_z", gz_dps);
  json.set("timestamp", millis());
  
  Serial.println("Uploading history data to Firebase...");
  
  // Push data to generate unique key
  if (Firebase.pushJSON(fbdo, "/mpu6050/history", json)) {
    Serial.println("History data pushed successfully!");
    Serial.println("Push Name: " + fbdo.pushName());
  } else {
    Serial.println("Failed to push history data:");
    Serial.println(fbdo.errorReason());
  }
}

// =============================================
// Setup
// =============================================
void setup() {
  Serial.begin(115200);
  
  // =========================================
  // 1. Connect to WiFi
  // =========================================
  Serial.println();
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  
  // =========================================
  // 2. Initialize Firebase
  // =========================================
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Set buffer sizes
  fbdo.setBSSLBufferSize(1024, 1024);
  fbdo.setResponseSize(1024);
  
  // Set read timeout
  Firebase.setReadTimeout(fbdo, 1000 * 60);
  
  // Set write size limit
  Firebase.setwriteSizeLimit(fbdo, "tiny");
  
  Serial.println("Firebase initialized!");
  
  // =========================================
  // 3. Initialize MPU6050
  // =========================================
  Serial.println("Initializing MPU6050 with calibration...");
  
  // Initialize I2C with ESP32 pins (SDA=21, SCL=22 by default)
  Wire.begin(21, 22);  // Explicitly set SDA=GPIO21, SCL=GPIO22
  
  // Wake up MPU6050
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission();
  
  // Configure accelerometer (±2g)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission();
  
  // Configure gyroscope (±250°/s)
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission();
  
  delay(1000);
  
  Serial.println("\n=== MPU6050 SENSOR READY ===");
  Serial.println("Output format:");
  Serial.println("Roll(deg), Pitch(deg), AccX(g), AccY(g), AccZ(g)");
  Serial.println("When flat: Roll~0, Pitch~0, Z~1.0g");
  Serial.println("Firebase updates: Live every 2s, History every 30s");
  Serial.println("\nStarting in 2 seconds...");
  delay(2000);
  
  prevTime = micros();
}

// =============================================
// Main Loop
// =============================================
void loop() {
  static unsigned long lastPrint = 0;
  unsigned long currentMillis = millis();
  
  // =========================================
  // 1. Read MPU6050 Sensor Data
  // =========================================
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  
  if (Wire.endTransmission(false) != 0) {
    Serial.println("I2C error!");
    delay(100);
    return;
  }
  
  Wire.requestFrom(MPU_ADDR, 14);
  
  if (Wire.available() >= 14) {
    // Read accelerometer
    int16_t ax_raw = Wire.read() << 8 | Wire.read();
    int16_t ay_raw = Wire.read() << 8 | Wire.read();
    int16_t az_raw = Wire.read() << 8 | Wire.read();
    
    // Skip temperature
    Wire.read() << 8 | Wire.read();
    
    // Read gyroscope
    int16_t gx_raw = Wire.read() << 8 | Wire.read();
    int16_t gy_raw = Wire.read() << 8 | Wire.read();
    int16_t gz_raw = Wire.read() << 8 | Wire.read();
    
    // =========================================
    // 2. ACCELEROMETER PROCESSING
    // =========================================
    // Convert to g's and apply bias
    ax_g = (ax_raw / 16384.0) - ACCEL_BIAS_X;
    ay_g = (ay_raw / 16384.0) - ACCEL_BIAS_Y;
    az_g = (az_raw / 16384.0) - ACCEL_BIAS_Z;
    
    // Important: Since your Z bias is 1.1941g, subtracting it will make Z ~0 when flat
    // We need to ADD 1g back to get correct readings
    az_g = az_g + 1.0;  // Now Z will be ~1.0g when flat
    
    // =========================================
    // 3. CALCULATE ANGLES FROM ACCELEROMETER
    // =========================================
    // Roll (rotation around X-axis)
    float accelRoll = atan2(ay_g, az_g) * 180.0 / PI;
    
    // Pitch (rotation around Y-axis)
    float accelPitch = atan2(-ax_g, sqrt(ay_g * ay_g + az_g * az_g)) * 180.0 / PI;
    
    // =========================================
    // 4. GYROSCOPE PROCESSING
    // =========================================
    gx_dps = (gx_raw / 131.0) - GYRO_BIAS_X;
    gy_dps = (gy_raw / 131.0) - GYRO_BIAS_Y;
    gz_dps = (gz_raw / 131.0) - GYRO_BIAS_Z;
    
    // =========================================
    // 5. COMPLEMENTARY FILTER
    // =========================================
    unsigned long currentTime = micros();
    float dt = (currentTime - prevTime) / 1000000.0;
    prevTime = currentTime;
    
    // Prevent large dt values
    if (dt > 0.1) dt = 0.01;
    
    // Gyro integration
    float gyroRoll = rollAngle + gx_dps * dt;
    float gyroPitch = pitchAngle + gy_dps * dt;
    
    // Complementary filter
    rollAngle = ALPHA * gyroRoll + (1.0 - ALPHA) * accelRoll;
    pitchAngle = ALPHA * gyroPitch + (1.0 - ALPHA) * accelPitch;
    
    // =========================================
    // 6. SERIAL OUTPUT (Every 100ms = 10Hz)
    // =========================================
    if (currentMillis - lastPrint >= 100) {
      lastPrint = currentMillis;
      
      // CSV format for Serial Monitor
      Serial.print("Roll: ");
      Serial.print(rollAngle, 2);
      Serial.print("°, Pitch: ");
      Serial.print(pitchAngle, 2);
      Serial.print("°, Accel: ");
      Serial.print(ax_g, 4);
      Serial.print(", ");
      Serial.print(ay_g, 4);
      Serial.print(", ");
      Serial.print(az_g, 4);
      Serial.print(", Gyro: ");
      Serial.print(gx_dps, 2);
      Serial.print(", ");
      Serial.print(gy_dps, 2);
      Serial.print(", ");
      Serial.print(gz_dps, 2);
      Serial.println(" dps");
    }
    
    // =========================================
    // 7. FIREBASE UPDATES
    // =========================================
    // Live data update (every 2 seconds)
    if (currentMillis - lastLiveUpdate >= LIVE_UPDATE_INTERVAL) {
      uploadLiveData();
      lastLiveUpdate = currentMillis;
    }
    
    // History data update (every 30 seconds)
    if (currentMillis - lastHistoryUpdate >= HISTORY_UPDATE_INTERVAL) {
      uploadHistoryData();
      lastHistoryUpdate = currentMillis;
    }
  } else {
    Serial.println("MPU6050 read error!");
  }
  
  delay(10);  // 100Hz sensor reading
}
