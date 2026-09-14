#include "config.h"

#include <Wire.h>
#include <math.h>

// =============================================
// MPU6050 Configuration
// =============================================
const int MPU_ADDR = 0x68;  // I2C address

// =============================================
// YOUR CALIBRATION VALUES
// =============================================
// IMPORTANT: Your Z bias is 1.1941g, not 1.0g!
// This means your sensor reads 1.1941g when flat (should be ~1.0g)
// We'll compensate for this
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
unsigned long prevTime = 0;

// Complementary filter (92% gyro, 8% accel)
const float ALPHA = 0.92;

// =============================================
// Setup
// =============================================
void setup() {
  Serial.begin(115200);  // ESP32 typically uses higher baud rate
  
  // Initialize I2C with ESP32 pins (SDA=21, SCL=22 by default)
  Wire.begin(21, 22);  // Explicitly set SDA=GPIO21, SCL=GPIO22
  
  Serial.println("Initializing MPU6050 with your calibration...");
  
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
  
  Serial.println("\n=== SENSOR READY ===");
  Serial.println("Output format:");
  Serial.println("Roll(deg), Pitch(deg), AccX(g), AccY(g), AccZ(g)");
  Serial.println("When flat: Roll~0, Pitch~0, Z~1.0g");
  Serial.println("\nStarting in 2 seconds...");
  delay(2000);
  
  prevTime = micros();
}

// =============================================
// Main Loop
// =============================================
void loop() {
  static unsigned long lastPrint = 0;
  
  // Read sensor data
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
    // 1. ACCELEROMETER PROCESSING (CORRECTED!)
    // =========================================
    // Convert to g's and apply bias
    // NOTE: For Z, we want it to be ~1.0g when flat, not 1.1941g
    float ax_g = (ax_raw / 16384.0) - ACCEL_BIAS_X;
    float ay_g = (ay_raw / 16384.0) - ACCEL_BIAS_Y;
    float az_g = (az_raw / 16384.0) - ACCEL_BIAS_Z;
    
    // Important: Since your Z bias is 1.1941g, subtracting it will make Z ~0 when flat
    // We need to ADD 1g back to get correct readings
    az_g = az_g + 1.0;  // Now Z will be ~1.0g when flat
    
    // =========================================
    // 2. CALCULATE ANGLES FROM ACCELEROMETER
    // =========================================
    // Roll (rotation around X-axis)
    float accelRoll = atan2(ay_g, az_g) * 180.0 / PI;
    
    // Pitch (rotation around Y-axis)
    float accelPitch = atan2(-ax_g, sqrt(ay_g * ay_g + az_g * az_g)) * 180.0 / PI;
    
    // =========================================
    // 3. GYROSCOPE PROCESSING
    // =========================================
    float gx_dps = (gx_raw / 131.0) - GYRO_BIAS_X;
    float gy_dps = (gy_raw / 131.0) - GYRO_BIAS_Y;
    float gz_dps = (gz_raw / 131.0) - GYRO_BIAS_Z;
    
    // =========================================
    // 4. COMPLEMENTARY FILTER
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
    // 5. OUTPUT (Every 100ms = 10Hz)
    // =========================================
    if (millis() - lastPrint >= 100) {
      lastPrint = millis();
      
      // CSV format
      Serial.print(rollAngle, 2);
      Serial.print(",");
      Serial.print(pitchAngle, 2);
      Serial.print(",");
      Serial.print(ax_g + sin(pitchAngle*PI/180), 4);
      Serial.print(",");
      Serial.print(ay_g, 4);
      Serial.print(",");
      Serial.println(az_g, 4);
    }
  } else {
    Serial.println("Read error!");
  }
  
  delay(10);  // 100Hz sensor reading
}
