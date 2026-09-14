#include "config.h"

// UNIFIED ESP32 FIRMWARE - MPU6050 + MAX30102 + GPS + VELOCITY SENSOR
// Sending all data to a single Firebase database

#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include <ArduinoJson.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <TinyGPSPlus.h>

// ==================== WIFI CONFIGURATION ====================
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// ==================== FIREBASE CONFIGURATION ====================
// Using a single database for all sensors
#define FIREBASE_HOST "test1-21b16-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "8Feq4Ge8PcTKzhMglCpS8VEkB2BLVvmP6VpfTQXi"

// ==================== MPU6050 CONSTANTS ====================
const int MPU_ADDR = 0x68;
const float ACCEL_SENSITIVITY = 16384.0f;
const float GYRO_SENSITIVITY = 131.0f;

// MPU6050 Calibration biases
const float BIAS_X = 886.18f;
const float BIAS_Y = 147.64f;
const float BIAS_Z = 19555.00f;
const float GYRO_BIAS_X = 0.02;
const float GYRO_BIAS_Y = -0.03;
const float GYRO_BIAS_Z = 0.01;

// ==================== MAX30102 CONSTANTS ====================
#define HR_FAST_DETECT_WINDOW 20
#define MOVING_AVG_SIZE 10
const float dc_alpha = 0.95;

// ==================== GPS CONFIGURATION ====================
TinyGPSPlus gps;
HardwareSerial SerialGPS(2);  // UART2: RX=16, TX=17

// ==================== VELOCITY SENSOR CONSTANTS ====================
const int IRSensorPin = 33;
const int DETECTION_THRESHOLD = 3900;
const float radius = 0.3;  // Wheel radius in meters

// Kalman Filter parameters for speed
float kf_q = 0.001f;    // Process noise
float kf_r = 0.5f;      // Measurement noise
float kf_x = 0.0f;      // Estimated speed
float kf_p = 1.0f;      // Estimation error covariance
float kf_k = 0.0f;      // Kalman gain

// Kalman Filter parameters for consecutive count
float kf_time_q = 0.005f;  // Process noise for time
float kf_time_r = 2.0f;    // Measurement noise for time
float kf_time_x = 0.0f;    // Estimated consecutive count
float kf_time_p = 1.0f;    // Estimation error for time
float kf_time_k = 0.0f;    // Kalman gain for time

// Velocity sensor variables
int deltay_time = 20;  // ms between readings
int ini_det;
bool ini_det_status = true;
int consecutive_count = 0;
int current_state = 0;
float filteredSpeed = 0.0;
float currentSpeed = 0.0;
float previousSpeed = 0.0;
bool isSpeedStable = false;
float speedMaintainedTime = 0.0;
unsigned long lastSpeedChangeTime = 0;

// ==================== FIREBASE OBJECTS ====================
FirebaseData fbdo;
FirebaseConfig config;
FirebaseAuth auth;
FirebaseJson json;

// ==================== TIMING VARIABLES ====================
// MPU6050 Timing
unsigned long mpu_lastLatestUpdate = 0;
unsigned long mpu_lastHistoryUpdate = 0;
const unsigned long MPU_LATEST_INTERVAL = 2000;    // 2 seconds
const unsigned long MPU_HISTORY_INTERVAL = 30000;  // 30 seconds

// MAX30102 Timing
unsigned long max_lastSensorRead = 0;
unsigned long max_lastLatestUpdate = 0;
unsigned long max_lastHistoryUpdate = 0;
const unsigned long MAX_SENSOR_INTERVAL = 10;      // 100Hz sampling
const unsigned long MAX_LATEST_INTERVAL = 2000;    // 2 seconds
const unsigned long MAX_HISTORY_INTERVAL = 30000;  // 30 seconds

// GPS Timing
unsigned long gps_lastLatestUpdate = 0;
unsigned long gps_lastHistoryUpdate = 0;
const unsigned long GPS_LATEST_INTERVAL = 10000;    // 10 seconds
const unsigned long GPS_HISTORY_INTERVAL = 60000;   // 60 seconds

// Velocity Sensor Timing
unsigned long velocity_lastLatestUpdate = 0;
unsigned long velocity_lastHistoryUpdate = 0;
const unsigned long VELOCITY_LATEST_INTERVAL = 2000;    // 2 seconds
const unsigned long VELOCITY_HISTORY_INTERVAL = 30000;  // 30 seconds

// ==================== DATA STRUCTURES ====================
struct MPUData {
  float ax_with_g, ay_with_g, az_with_g;
  float linear_ax, linear_ay, linear_az;
  float roll_deg, pitch_deg;
  unsigned long timestamp;
};

struct GPSData {
  double latitude;
  double longitude;
  float speed_kmph;
  float altitude;
  int satellites;
  unsigned long timestamp;
  bool valid;
};

struct VelocityData {
  float speed_mps;
  float filtered_speed_mps;
  float maintained_time_sec;
  int consecutive_count;
  unsigned long timestamp;
};

// ==================== MPU6050 FILTERS ====================
class SimpleKalmanFilter {
private:
  float Q, R, x_hat, P;
public:
  SimpleKalmanFilter(float Q_val, float R_val, float initial_value) {
    Q = Q_val; R = R_val; x_hat = initial_value; P = 1.0;
  }
  float update(float measurement) {
    float x_hat_minus = x_hat;
    P = P + Q;
    float K = P / (P + R);
    x_hat = x_hat_minus + K * (measurement - x_hat_minus);
    P = (1 - K) * P;
    return x_hat;
  }
};

class ComplementaryFilter {
private:
  float angle, alpha;
  unsigned long prev_time;
public:
  ComplementaryFilter(float filter_alpha) {
    angle = 0.0; alpha = filter_alpha; prev_time = micros();
  }
  float update(float accel_angle, float gyro_rate) {
    unsigned long current_time = micros();
    float dt = (current_time - prev_time) / 1000000.0;
    prev_time = current_time;
    float gyro_angle = angle + gyro_rate * dt;
    angle = alpha * gyro_angle + (1.0 - alpha) * accel_angle;
    return angle;
  }
  float getAngle() { return angle; }
};

// ==================== MAX30102 OBJECTS ====================
MAX30105 particleSensor;

// MAX30102 Data variables
float heartRate = 0.0;
int spo2 = 0;
int beatAvg = 0;
bool fingerDetected = false;
long irValue = 0;

// MAX30102 Buffers and filters
float irBuffer[MOVING_AVG_SIZE];
float redBuffer[MOVING_AVG_SIZE];
int bufferIndex = 0;
bool bufferFilled = false;

// MAX30102 DC removal
float dc_ir = 0;
float dc_red = 0;

// MAX30102 Calibration
bool isCalibrated = false;
float calibration_ir = 0;
float calibration_red = 0;

// MAX30102 Filtered data
float filtered_ir = 0;
float filtered_red = 0;
float raw_ir = 0;
float raw_red = 0;
float ac_ir = 0;
float ac_red = 0;

// MAX30102 Heart rate detection
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;

// MAX30102 Statistics
int readingsCount = 0;
float minHeartRate = 999;
float maxHeartRate = 0;
int minSpO2 = 100;
int maxSpO2 = 0;

// Session ID for all sensors
unsigned long sessionStartTime = 0;
String sessionId = "";

// ==================== MPU6050 GLOBAL VARIABLES ====================
int16_t AcX, AcY, AcZ, GyX, GyY, GyZ;
SimpleKalmanFilter kf_ax(0.001, 0.1, 0.0);
SimpleKalmanFilter kf_ay(0.001, 0.1, 0.0);
SimpleKalmanFilter kf_az(0.001, 0.1, 1.0);
ComplementaryFilter cf_roll(0.98);
ComplementaryFilter cf_pitch(0.98);

// ==================== FUNCTION DECLARATIONS ====================
// Common Functions
void initWiFi();
void initFirebase();

// MPU6050 Functions
void initMPU6050();
MPUData readAndProcessMPU();
void sendMPUToFirebaseLatest(MPUData data);
void sendMPUToFirebaseHistory(MPUData data);

// MAX30102 Functions
void initMAX30102();
void readMAX30102Data();
bool checkForFinger(long irValue);
void readHeartRate();
void readSpO2(float acIR, float acRed);
void resetHeartRateBuffer();
void sendMAXToFirebaseLatest();
void sendMAXToFirebaseHistory();
String getSignalQuality(long irValue);
void calibrateSensor();
void applyMovingAverageFilter();
float removeDCComponent(float rawValue, float &dc, float alpha);
float calculateHeartRateFast();

// GPS Functions
void initGPS();
GPSData readAndProcessGPS();
void sendGPSToFirebaseLatest(GPSData data);
void sendGPSToFirebaseHistory(GPSData data);

// Velocity Sensor Functions
void initVelocitySensor();
VelocityData readAndProcessVelocity();
void initKalmanFilter();
float updateKalmanSpeed(float measurement);
float updateKalmanTime(float measurement);
void updateSpeedStability(float newSpeed);
void sendVelocityToFirebaseLatest(VelocityData data);
void sendVelocityToFirebaseHistory(VelocityData data);

// Utility Functions
String generateSessionId();
String getTimestamp();

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=======================================");
  Serial.println("ESP32 UNIFIED SENSOR SYSTEM");
  Serial.println("MPU6050 + MAX30102 + GPS + VELOCITY");
  Serial.println("Single Firebase Database");
  Serial.println("=======================================\n");
  
  // Initialize WiFi
  initWiFi();
  
  // Initialize Firebase
  initFirebase();
  
  // Initialize I2C bus for MPU6050 and MAX30102
  Wire.begin(21, 22);
  delay(100);
  
  // Initialize MPU6050
  initMPU6050();
  
  // Initialize MAX30102
  initMAX30102();
  
  // Initialize GPS
  initGPS();
  
  // Initialize Velocity Sensor
  initVelocitySensor();
  
  // Generate session ID
  sessionStartTime = millis();
  sessionId = generateSessionId();
  
  // Initialize timers
  mpu_lastLatestUpdate = millis();
  mpu_lastHistoryUpdate = millis();
  max_lastLatestUpdate = millis();
  max_lastHistoryUpdate = millis();
  gps_lastLatestUpdate = millis();
  gps_lastHistoryUpdate = millis();
  velocity_lastLatestUpdate = millis();
  velocity_lastHistoryUpdate = millis();
  
  Serial.println("\n✅ SYSTEM INITIALIZED");
  Serial.print("📡 Sending data to: ");
  Serial.println(FIREBASE_HOST);
  Serial.print("📱 Session ID: ");
  Serial.println(sessionId);
  Serial.println("🔧 Ready to collect and transmit data");
}

// ==================== LOOP ====================
void loop() {
  unsigned long currentMillis = millis();
  
  // ==================== MPU6050 PROCESSING ====================
  MPUData mpuData = readAndProcessMPU();
  mpuData.timestamp = currentMillis;
  
  // Update MPU6050 latest values every 2 seconds
  if (currentMillis - mpu_lastLatestUpdate >= MPU_LATEST_INTERVAL) {
    sendMPUToFirebaseLatest(mpuData);
    mpu_lastLatestUpdate = currentMillis;
  }
  
  // Add MPU6050 to history every 30 seconds
  if (currentMillis - mpu_lastHistoryUpdate >= MPU_HISTORY_INTERVAL) {
    sendMPUToFirebaseHistory(mpuData);
    mpu_lastHistoryUpdate = currentMillis;
  }
  
  // ==================== MAX30102 PROCESSING ====================
  // Read MAX30102 sensor data at 100Hz
  if (currentMillis - max_lastSensorRead >= MAX_SENSOR_INTERVAL) {
    readMAX30102Data();
    max_lastSensorRead = currentMillis;
  }
  
  // Update MAX30102 latest values every 2 seconds
  if (currentMillis - max_lastLatestUpdate >= MAX_LATEST_INTERVAL) {
    sendMAXToFirebaseLatest();
    max_lastLatestUpdate = currentMillis;
  }
  
  // Add MAX30102 to history every 30 seconds
  if (currentMillis - max_lastHistoryUpdate >= MAX_HISTORY_INTERVAL) {
    sendMAXToFirebaseHistory();
    max_lastHistoryUpdate = currentMillis;
  }
  
  // ==================== GPS PROCESSING ====================
  // Read GPS data
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }
  
  GPSData gpsData = readAndProcessGPS();
  
  // Update GPS latest values every 10 seconds
  if (currentMillis - gps_lastLatestUpdate >= GPS_LATEST_INTERVAL) {
    sendGPSToFirebaseLatest(gpsData);
    gps_lastLatestUpdate = currentMillis;
  }
  
  // Add GPS to history every 60 seconds (only if valid)
  if (currentMillis - gps_lastHistoryUpdate >= GPS_HISTORY_INTERVAL && gpsData.valid) {
    sendGPSToFirebaseHistory(gpsData);
    gps_lastHistoryUpdate = currentMillis;
  }
  
  // ==================== VELOCITY SENSOR PROCESSING ====================
  VelocityData velocityData = readAndProcessVelocity();
  
  // Update Velocity latest values every 2 seconds
  if (currentMillis - velocity_lastLatestUpdate >= VELOCITY_LATEST_INTERVAL) {
    sendVelocityToFirebaseLatest(velocityData);
    velocity_lastLatestUpdate = currentMillis;
  }
  
  // Add Velocity to history every 30 seconds
  if (currentMillis - velocity_lastHistoryUpdate >= VELOCITY_HISTORY_INTERVAL) {
    sendVelocityToFirebaseHistory(velocityData);
    velocity_lastHistoryUpdate = currentMillis;
  }
  
  delay(10); // Maintain sampling rate
}

// ==================== COMMON FUNCTIONS ====================
void initWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("📶 IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi Connection Failed!");
    ESP.restart();
  }
}

void initFirebase() {
  Serial.println("\nInitializing Firebase...");
  
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  config.timeout.serverResponse = 10 * 1000;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  Serial.print("Connecting to Firebase");
  int attempts = 0;
  while (!Firebase.ready() && attempts < 20) {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  
  if (Firebase.ready()) {
    Serial.println("\n✅ Firebase Connected!");
    
    // Test connection
    if (Firebase.setString(fbdo, "/system/status", "connected")) {
      Serial.println("✅ Firebase test successful");
    }
  } else {
    Serial.println("\n⚠️  Firebase Connection Warning");
  }
}

String generateSessionId() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  String timestamp = String(millis());
  return "SES_" + mac.substring(8) + "_" + timestamp.substring(timestamp.length() - 6);
}

String getTimestamp() {
  unsigned long currentMillis = millis();
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  seconds = seconds % 60;
  minutes = minutes % 60;
  hours = hours % 24;
  
  char timestamp[20];
  sprintf(timestamp, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  return String(timestamp);
}

// ==================== MPU6050 FUNCTIONS ====================
void initMPU6050() {
  Serial.println("\nInitializing MPU6050...");
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission(true);
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1B);
  Wire.write(0x00);
  Wire.endTransmission(true);
  
  Serial.println("✅ MPU6050 Initialized");
}

MPUData readAndProcessMPU() {
  MPUData data;
  
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);
  
  if (Wire.available() >= 14) {
    AcX = Wire.read() << 8 | Wire.read();
    AcY = Wire.read() << 8 | Wire.read();
    AcZ = Wire.read() << 8 | Wire.read();
    Wire.read() << 8 | Wire.read();
    GyX = Wire.read() << 8 | Wire.read();
    GyY = Wire.read() << 8 | Wire.read();
    GyZ = Wire.read() << 8 | Wire.read();
    
    // Apply bias correction
    float AcX_corrected = AcX - BIAS_X;
    float AcY_corrected = AcY - BIAS_Y;
    float AcZ_corrected = AcZ - BIAS_Z + ACCEL_SENSITIVITY;
    
    // Convert to g's
    float Ax_g_raw = AcX_corrected / ACCEL_SENSITIVITY;
    float Ay_g_raw = AcY_corrected / ACCEL_SENSITIVITY;
    float Az_g_raw = AcZ_corrected / ACCEL_SENSITIVITY;
    
    // Apply Kalman Filter
    float Ax_g_kf = kf_ax.update(Ax_g_raw);
    float Ay_g_kf = kf_ay.update(Ay_g_raw);
    float Az_g_kf = kf_az.update(Az_g_raw);
    
    // Store acceleration with gravity
    data.ax_with_g = Ax_g_kf;
    data.ay_with_g = Ay_g_kf;
    data.az_with_g = Az_g_kf;
    
    // Calculate angles from accelerometer
    float accel_roll_rad = atan2(Ay_g_kf, Az_g_kf);
    float accel_pitch_rad = atan2(-Ax_g_kf, sqrt(Ay_g_kf * Ay_g_kf + Az_g_kf * Az_g_kf));
    
    // Convert gyro data
    float gyro_x_rate = (GyX / GYRO_SENSITIVITY) - GYRO_BIAS_X;
    float gyro_y_rate = (GyY / GYRO_SENSITIVITY) - GYRO_BIAS_Y;
    float gyro_x_rate_rad = gyro_x_rate * (M_PI / 180.0);
    float gyro_y_rate_rad = gyro_y_rate * (M_PI / 180.0);
    
    // Update complementary filters
    float roll_rad = cf_roll.update(accel_roll_rad, gyro_x_rate_rad);
    float pitch_rad = cf_pitch.update(accel_pitch_rad, -gyro_y_rate_rad);
    
    // Convert to degrees
    data.roll_deg = roll_rad * (180.0 / M_PI);
    data.pitch_deg = pitch_rad * (180.0 / M_PI);
    
    // Remove gravity to get linear acceleration
    data.linear_ax = Ax_g_kf;
    data.linear_ay = Ay_g_kf;
    data.linear_az = Az_g_kf;
    
    // Gravity removal
    float sin_roll = sin(roll_rad);
    float cos_roll = cos(roll_rad);
    float sin_pitch = sin(pitch_rad);
    float cos_pitch = cos(pitch_rad);
    float gravity_x = -sin_pitch;
    float gravity_y = sin_roll * cos_pitch;
    float gravity_z = cos_roll * cos_pitch;
    
    data.linear_ax -= gravity_x;
    data.linear_ay -= gravity_y;
    data.linear_az -= gravity_z;
  }
  
  return data;
}

void sendMPUToFirebaseLatest(MPUData data) {
  json.clear();
  
  json.set("acceleration/with_gravity/x", data.ax_with_g);
  json.set("acceleration/with_gravity/y", data.ay_with_g);
  json.set("acceleration/with_gravity/z", data.az_with_g);
  
  json.set("acceleration/linear/x", data.linear_ax);
  json.set("acceleration/linear/y", data.linear_ay);
  json.set("acceleration/linear/z", data.linear_az);
  
  json.set("tilt/roll", data.roll_deg);
  json.set("tilt/pitch", data.pitch_deg);
  
  json.set("timestamp", data.timestamp);
  json.set("formatted_time", getTimestamp());
  json.set("session_id", sessionId);
  
  if (Firebase.updateNode(fbdo, "/mpu6050/latest", json)) {
    Serial.println("[MPU6050] Latest data sent"); 
  } else {
    Serial.println("[MPU6050] Error: " + fbdo.errorReason());
  }
}

void sendMPUToFirebaseHistory(MPUData data) {
  String historyPath = "/mpu6050/history/" + String(data.timestamp);
  
  json.clear();
  
  json.set("ax_with_g", data.ax_with_g);
  json.set("ay_with_g", data.ay_with_g);
  json.set("az_with_g", data.az_with_g);
  json.set("linear_ax", data.linear_ax);
  json.set("linear_ay", data.linear_ay);
  json.set("linear_az", data.linear_az);
  json.set("roll_deg", data.roll_deg);
  json.set("pitch_deg", data.pitch_deg);
  json.set("timestamp", data.timestamp);
  json.set("session_id", sessionId);
  
  if (Firebase.setJSON(fbdo, historyPath, json)) {
    Serial.println("[MPU6050] History data added");
  } else {
    Serial.println("[MPU6050] History error: " + fbdo.errorReason());
  }
}

// ==================== MAX30102 FUNCTIONS ====================
void initMAX30102() {
  Serial.println("\nInitializing MAX30102 Sensor...");
  
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("❌ MAX30102 not found!");
    delay(5000);
    return;
  }
  
  Serial.println("✅ MAX30102 Found!");
  
  byte ledBrightness = 0x4F;
  byte sampleAverage = 4;
  byte ledMode = 2;
  int sampleRate = 400;
  int pulseWidth = 411;
  int adcRange = 4096;
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x4F);
  particleSensor.setPulseAmplitudeIR(0x4F);
  particleSensor.enableFIFORollover();
  
  // Initialize buffers
  for(int i = 0; i < MOVING_AVG_SIZE; i++) {
    irBuffer[i] = 0;
    redBuffer[i] = 0;
  }
  
  for(int i = 0; i < RATE_SIZE; i++) {
    rates[i] = 0;
  }
  
  Serial.println("✅ MAX30102 Configured");
}

float removeDCComponent(float rawValue, float &dc, float alpha) {
  float ac = rawValue - dc;
  dc = alpha * dc + (1 - alpha) * rawValue;
  return ac;
}

void applyMovingAverageFilter() {
  irBuffer[bufferIndex] = raw_ir;
  redBuffer[bufferIndex] = raw_red;
  
  bufferIndex = (bufferIndex + 1) % MOVING_AVG_SIZE;
  if (bufferIndex == 0) bufferFilled = true;
  
  float sum_ir = 0;
  float sum_red = 0;
  int count = bufferFilled ? MOVING_AVG_SIZE : bufferIndex;
  
  for (int i = 0; i < count; i++) {
    sum_ir += irBuffer[i];
    sum_red += redBuffer[i];
  }
  
  filtered_ir = sum_ir / count;
  filtered_red = sum_red / count;
  
  ac_ir = removeDCComponent(filtered_ir, dc_ir, dc_alpha);
  ac_red = removeDCComponent(filtered_red, dc_red, dc_alpha);
}

void calibrateSensor() {
  static unsigned long calibrationStart = 0;
  static int calibrationCount = 0;
  static float sum_ir = 0;
  static float sum_red = 0;
  
  if (!isCalibrated && fingerDetected) {
    if (calibrationStart == 0) {
      calibrationStart = millis();
      sum_ir = 0;
      sum_red = 0;
      calibrationCount = 0;
    }
    
    if (millis() - calibrationStart < 5000) {
      sum_ir += raw_ir;
      sum_red += raw_red;
      calibrationCount++;
    } else {
      calibration_ir = sum_ir / calibrationCount;
      calibration_red = sum_red / calibrationCount;
      isCalibrated = true;
      calibrationStart = 0;
      calibrationCount = 0;
    }
  }
  
  if (!fingerDetected && calibrationStart > 0) {
    calibrationStart = 0;
    calibrationCount = 0;
    sum_ir = 0;
    sum_red = 0;
  }
}

float calculateHeartRateFast() {
  static float lastValue = 0;
  static bool rising = false;
  static int peakCount = 0;
  static unsigned long lastPeakTime = 0;
  static unsigned long peakIntervalSum = 0;
  static int validPeaks = 0;
  
  float currentValue = filtered_ir;
  float derivative = currentValue - lastValue;
  lastValue = currentValue;
  
  if (derivative > 50 && !rising) {
    rising = true;
    unsigned long now = millis();
    
    if (lastPeakTime > 0) {
      unsigned long interval = now - lastPeakTime;
      
      if (interval > 300 && interval < 1500) {
        peakIntervalSum += interval;
        validPeaks++;
        
        if (validPeaks >= 2) {
          float avgInterval = peakIntervalSum / validPeaks;
          float calculatedBPM = 60000.0 / avgInterval;
          
          if (calculatedBPM >= 40 && calculatedBPM <= 200) {
            rates[rateSpot++] = (byte)calculatedBPM;
            rateSpot %= RATE_SIZE;
            
            int sum = 0;
            int count = 0;
            for (byte x = 0; x < RATE_SIZE; x++) {
              if (rates[x] > 0) {
                sum += rates[x];
                count++;
              }
            }
            
            if (count > 0) {
              beatAvg = sum / count;
              heartRate = calculatedBPM;
              return calculatedBPM;
            }
          }
        }
      }
    }
    
    lastPeakTime = now;
    peakCount++;
    
    if (peakCount > 10 && validPeaks < 2) {
      peakCount = 0;
      validPeaks = 0;
      peakIntervalSum = 0;
      lastPeakTime = 0;
    }
  } else if (derivative < -50) {
    rising = false;
  }
  
  return heartRate;
}

void readMAX30102Data() {
  raw_ir = particleSensor.getIR();
  raw_red = particleSensor.getRed();
  
  fingerDetected = checkForFinger((long)raw_ir);
  
  if (fingerDetected) {
    applyMovingAverageFilter();
    
    irValue = (long)filtered_ir;
    
    calibrateSensor();
    
    static unsigned long lastHRUpdate = 0;
    if (millis() - lastHRUpdate > 250) {
      float fastHR = calculateHeartRateFast();
      
      readHeartRate();
      
      if (fastHR > 0 && fastHR < 200) {
        heartRate = fastHR;
      } else if (beatsPerMinute > 0 && beatsPerMinute < 200) {
        heartRate = beatsPerMinute;
      }
      
      lastHRUpdate = millis();
    }
    
    if (isCalibrated) {
      readSpO2(ac_ir, ac_red);
    }
    
    if (heartRate > 0) {
      readingsCount++;
      if (heartRate < minHeartRate) minHeartRate = heartRate;
      if (heartRate > maxHeartRate) maxHeartRate = heartRate;
    }
    if (spo2 > 0) {
      if (spo2 < minSpO2) minSpO2 = spo2;
      if (spo2 > maxSpO2) maxSpO2 = spo2;
    }
  } else {
    heartRate = 0.0;
    spo2 = 0;
    beatAvg = 0;
    beatsPerMinute = 0;
    resetHeartRateBuffer();
    isCalibrated = false;
    dc_ir = 0;
    dc_red = 0;
    bufferFilled = false;
    bufferIndex = 0;
  }
}

bool checkForFinger(long irValue) {
  return (irValue > 3000);
}

void readHeartRate() {
  if (checkForBeat((long)filtered_ir) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    beatsPerMinute = 60 / (delta / 1000.0);
    
    if (beatsPerMinute > 40 && beatsPerMinute < 200) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      
      int sum = 0;
      int count = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        if (rates[x] > 0) {
          sum += rates[x];
          count++;
        }
      }
      
      if (count > 0) {
        beatAvg = sum / count;
        if (heartRate == 0) {
          heartRate = beatsPerMinute;
        }
      }
    }
  }
}

void readSpO2(float acIR, float acRed) {
  static unsigned long lastSpO2Time = 0;
  
  if (millis() - lastSpO2Time > 1000) {
    lastSpO2Time = millis();
    
    if (irValue > 5000 && dc_ir > 0 && dc_red > 0) {
      float red_ac = fabs(acRed);
      float ir_ac = fabs(acIR);
      
      if (ir_ac > 0 && dc_ir > 0 && dc_red > 0) {
        float ratio = (red_ac / dc_red) / (ir_ac / dc_ir);
        float calculatedSpO2 = 110.0 - 25.0 * ratio;
        
        if (calculatedSpO2 < 70.0) calculatedSpO2 = 70.0;
        if (calculatedSpO2 > 100.0) calculatedSpO2 = 100.0;
        
        static float spo2Buffer[3] = {0};
        static int spo2Index = 0;
        
        spo2Buffer[spo2Index] = calculatedSpO2;
        spo2Index = (spo2Index + 1) % 3;
        
        float sum = 0;
        int count = 0;
        for (int i = 0; i < 3; i++) {
          if (spo2Buffer[i] > 0) {
            sum += spo2Buffer[i];
            count++;
          }
        }
        
        if (count > 1) {
          spo2 = (int)(sum / count + 0.5);
        } else if (count == 1) {
          spo2 = (int)spo2Buffer[0];
        }
      }
    }
  }
}

void resetHeartRateBuffer() {
  for (byte x = 0; x < RATE_SIZE; x++) {
    rates[x] = 0;
  }
  rateSpot = 0;
  lastBeat = 0;
  beatsPerMinute = 0;
  heartRate = 0;
}

String getSignalQuality(long irValue) {
  if (irValue < 3000) return "No Finger";
  if (irValue < 7000) return "Poor";
  if (irValue < 15000) return "Fair";
  if (irValue < 30000) return "Good";
  if (irValue < 50000) return "Very Good";
  return "Excellent";
}

void sendMAXToFirebaseLatest() {
  json.clear();
  
  json.set("heart_rate", heartRate);
  json.set("spo2", spo2);
  json.set("beat_avg", beatAvg);
  json.set("finger_detected", fingerDetected);
  json.set("ir_value", irValue);
  json.set("signal_quality", getSignalQuality(irValue));
  json.set("timestamp", millis());
  json.set("readings_count", readingsCount);
  json.set("min_heart_rate", minHeartRate);
  json.set("max_heart_rate", maxHeartRate);
  json.set("min_spo2", minSpO2);
  json.set("max_spo2", maxSpO2);
  json.set("session_id", sessionId);
  json.set("calibrated", isCalibrated);
  
  if (isCalibrated) {
    json.set("baseline_ir", calibration_ir);
    json.set("baseline_red", calibration_red);
  }
  
  if (Firebase.updateNode(fbdo, "/max30102/latest", json)) {
    Serial.print("[MAX30102] HR: ");
    Serial.print(heartRate, 1);
    Serial.print(" | SpO2: ");
    Serial.print(spo2);
    Serial.print("% | Finger: ");
    Serial.println(fingerDetected ? "Yes" : "No");
  } else {
    Serial.println("[MAX30102] Error: " + fbdo.errorReason());
  }
}

void sendMAXToFirebaseHistory() {
  FirebaseJson historicalJson;
  
  historicalJson.set("heart_rate", heartRate);
  historicalJson.set("spo2", spo2);
  historicalJson.set("beat_avg", beatAvg);
  historicalJson.set("ir_value", irValue);
  historicalJson.set("signal_quality", getSignalQuality(irValue));
  historicalJson.set("timestamp", millis());
  historicalJson.set("session_id", sessionId);
  historicalJson.set("calibrated", isCalibrated);
  
  String historicalPath = "/max30102/history/" + String(millis());
  
  if (Firebase.setJSON(fbdo, historicalPath, historicalJson)) {
    Serial.println("[MAX30102] Historical data saved");
  }
}

// ==================== GPS FUNCTIONS ====================
void initGPS() {
  Serial.println("\nInitializing GPS...");
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17);  // RX=16, TX=17
  delay(100);
  
  Serial.println("✅ GPS Serial Initialized");
  Serial.println("📡 Waiting for GPS signal...");
}

GPSData readAndProcessGPS() {
  GPSData data;
  data.valid = false;
  data.timestamp = millis();
  
  if (gps.location.isValid()) {
    data.latitude = gps.location.lat();
    data.longitude = gps.location.lng();
    data.speed_kmph = gps.speed.kmph();
    data.altitude = gps.altitude.meters();
    data.satellites = gps.satellites.value();
    data.valid = true;
  }
  
  return data;
}

void sendGPSToFirebaseLatest(GPSData data) {
  json.clear();
  
  if (data.valid) {
    json.set("latitude", data.latitude);
    json.set("longitude", data.longitude);
    json.set("speed_kmph", data.speed_kmph);
    json.set("altitude", data.altitude);
    json.set("satellites", data.satellites);
    json.set("timestamp", data.timestamp);
    json.set("formatted_time", getTimestamp());
    json.set("session_id", sessionId);
    
    if (Firebase.updateNode(fbdo, "/gps/latest", json)) {
      Serial.println("[GPS] Latest data sent");
    } else {
      Serial.println("[GPS] Error: " + fbdo.errorReason());
    }
  } else {
    Serial.println("[GPS] No valid GPS data available");
  }
}

void sendGPSToFirebaseHistory(GPSData data) {
  if (!data.valid) return;
  
  String historyPath = "/gps/history/" + String(data.timestamp);
  
  json.clear();
  
  json.set("latitude", data.latitude);
  json.set("longitude", data.longitude);
  json.set("speed_kmph", data.speed_kmph);
  json.set("altitude", data.altitude);
  json.set("satellites", data.satellites);
  json.set("timestamp", data.timestamp);
  json.set("session_id", sessionId);
  
  if (Firebase.setJSON(fbdo, historyPath, json)) {
    Serial.println("[GPS] History data added");
  } else {
    Serial.println("[GPS] History error: " + fbdo.errorReason());
  }
}

// ==================== VELOCITY SENSOR FUNCTIONS ====================
void initVelocitySensor() {
  Serial.println("\nInitializing Velocity Sensor...");
  
  // Initialize Kalman filters
  initKalmanFilter();
  
  // Read initial value
  int rawValue = analogRead(IRSensorPin);
  
  // Determine initial detection state
  if (rawValue > DETECTION_THRESHOLD) {
    ini_det = 1;
  } else {
    ini_det = 0;
  }
  
  Serial.println("✅ Velocity Sensor Initialized");
  Serial.print("📊 Detection Threshold: ");
  Serial.println(DETECTION_THRESHOLD);
}

void initKalmanFilter() {
  kf_x = 0.0f;
  kf_p = 1.0f;
  kf_time_x = 0.0f;
  kf_time_p = 1.0f;
}

float updateKalmanSpeed(float measurement) {
  kf_p = kf_p + kf_q;
  kf_k = kf_p / (kf_p + kf_r);
  kf_x = kf_x + kf_k * (measurement - kf_x);
  kf_p = (1.0f - kf_k) * kf_p;
  return kf_x;
}

float updateKalmanTime(float measurement) {
  kf_time_p = kf_time_p + kf_time_q;
  kf_time_k = kf_time_p / (kf_time_p + kf_time_r);
  kf_time_x = kf_time_x + kf_time_k * (measurement - kf_time_x);
  kf_time_p = (1.0f - kf_time_k) * kf_time_p;
  return kf_time_x;
}

void updateSpeedStability(float newSpeed) {
  unsigned long currentTime = millis();
  
  // Define speed tolerance (±10%)
  float speedTolerance = filteredSpeed * 0.1;
  
  // Check if speed is within tolerance of previous speed
  if (abs(newSpeed - previousSpeed) <= speedTolerance && filteredSpeed > 0.1) {
    if (!isSpeedStable) {
      // Speed just became stable
      lastSpeedChangeTime = currentTime;
      isSpeedStable = true;
    }
    // Calculate maintained time in seconds
    speedMaintainedTime = (currentTime - lastSpeedChangeTime) / 1000.0;
  } else {
    // Speed changed significantly
    isSpeedStable = false;
    speedMaintainedTime = 0.0;
  }
  
  previousSpeed = filteredSpeed;
}

VelocityData readAndProcessVelocity() {
  VelocityData data;
  data.timestamp = millis();
  
  // Read the raw analog value
  int rawValue = analogRead(IRSensorPin);
  int sense_value;
  
  // Convert to digital value
  if (rawValue > DETECTION_THRESHOLD) {
    sense_value = 1;
  } else {
    sense_value = 0;
  }
  
  if (ini_det_status) {
    // Initial detection phase
    if (sense_value != ini_det) {
      ini_det_status = false;
      current_state = sense_value;
      consecutive_count = 1;
    }
  } else {
    // Normal operation
    if (current_state == sense_value) {
      consecutive_count += 1;
      
      // Apply Kalman filter to consecutive count (time intervals)
      float filtered_consecutive = updateKalmanTime(consecutive_count);
      
      // Only calculate speed when we have enough stable readings
      if (consecutive_count >= 5) {
        // Calculate raw speed: circumference / time
        float timeSeconds = consecutive_count * (deltay_time * 0.001);
        float rawSpeed = (3.14159265358979323846 * radius) / timeSeconds;
        
        // Apply Kalman filter to speed
        filteredSpeed = updateKalmanSpeed(rawSpeed);
        currentSpeed = rawSpeed;
        
        // Update speed stability tracking
        updateSpeedStability(filteredSpeed);
        
        data.speed_mps = currentSpeed;
        data.filtered_speed_mps = filteredSpeed;
        data.consecutive_count = consecutive_count;
        data.maintained_time_sec = speedMaintainedTime;
      }
    } else {
      // State changed
      if (consecutive_count >= 5) {
        // Calculate speed for the previous stable interval
        float timeSeconds = consecutive_count * (deltay_time * 0.001);
        float rawSpeed = (3.14159265358979323846 * radius) / timeSeconds;
        
        // Apply Kalman filter
        filteredSpeed = updateKalmanSpeed(rawSpeed);
        currentSpeed = rawSpeed;
        
        data.speed_mps = currentSpeed;
        data.filtered_speed_mps = filteredSpeed;
        data.consecutive_count = consecutive_count;
        data.maintained_time_sec = 0.0; // Reset for new interval
        
        // Reset speed stability
        isSpeedStable = false;
        speedMaintainedTime = 0.0;
      }
      
      // Reset for new state
      current_state = sense_value;
      consecutive_count = 1;
    }
  }
  
  // Add a small delay between readings
  delay(deltay_time);
  
  return data;
}

void sendVelocityToFirebaseLatest(VelocityData data) {
  json.clear();
  
  json.set("speed_mps", data.speed_mps);
  json.set("filtered_speed_mps", data.filtered_speed_mps);
  json.set("maintained_time_sec", data.maintained_time_sec);
  json.set("consecutive_count", data.consecutive_count);
  json.set("timestamp", data.timestamp);
  json.set("formatted_time", getTimestamp());
  json.set("session_id", sessionId);
  
  if (Firebase.updateNode(fbdo, "/velocity/latest", json)) {
    Serial.print("[VELOCITY] Speed: ");
    Serial.print(data.filtered_speed_mps, 1);
    Serial.println(" m/s");
  } else {
    Serial.println("[VELOCITY] Error: " + fbdo.errorReason());
  }
}

void sendVelocityToFirebaseHistory(VelocityData data) {
  String historyPath = "/velocity/history/" + String(data.timestamp);
  
  json.clear();
  
  json.set("speed_mps", data.speed_mps);
  json.set("filtered_speed_mps", data.filtered_speed_mps);
  json.set("maintained_time_sec", data.maintained_time_sec);
  json.set("consecutive_count", data.consecutive_count);
  json.set("timestamp", data.timestamp);
  json.set("session_id", sessionId);
  
  if (Firebase.setJSON(fbdo, historyPath, json)) {
    Serial.println("[VELOCITY] History data added");
  } else {
    Serial.println("[VELOCITY] History error: " + fbdo.errorReason());
  }
}
