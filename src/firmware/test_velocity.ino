#include "config.h"

#include <WiFi.h>
#include <FirebaseESP32.h>

// WiFi credentials
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

// Firebase configuration
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""

// Define Firebase Data object
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Define the GPIO pin connected to the Analog OUT (AO) pin of the IR sensor.
const int IRSensorPin = 33;

// Calibrated threshold to detect an object
const int DETECTION_THRESHOLD = 3900;

// Kalman Filter parameters for speed
float kf_q = 0.01f;    // Process noise (small = trust model more)
float kf_r = 0.2f;      // Measurement noise (adjust based on speed variance)
float kf_x = 0.0f;      // Estimated speed
float kf_p = 1.0f;      // Estimation error covariance
float kf_k = 0.0f;      // Kalman gain

// Kalman Filter parameters for consecutive count (time intervals)
float kf_time_q = 0.005f;  // Process noise for time
float kf_time_r = 2.0f;    // Measurement noise for time
float kf_time_x = 0.0f;    // Estimated consecutive count
float kf_time_p = 1.0f;    // Estimation error for time
float kf_time_k = 0.0f;    // Kalman gain for time

// Speed tracking variables
float currentSpeed = 0.0;
float filteredSpeed = 0.0;
float previousSpeed = 0.0;
float speedMaintainedTime = 0.0;  // Time speed has been maintained (seconds)

// State variables
int ini_det;
bool ini_det_status = true;
int consecutive_count = 0;
int current_state = 0;
int deltay_time = 20;  // ms between readings
const float radius = 0.3;

// Time tracking
unsigned long lastSpeedChangeTime = 0;
unsigned long currentTime = 0;
bool isSpeedStable = false;

// Firebase update timers
unsigned long lastLiveUpdate = 0;
const unsigned long LIVE_UPDATE_INTERVAL = 2000;  // 2 seconds

unsigned long lastHistoryUpdate = 0;
const unsigned long HISTORY_UPDATE_INTERVAL = 30000;  // 30 seconds

// Kalman filter initialization function
void initKalmanFilter() {
  kf_x = 0.0f;
  kf_p = 1.0f;
  kf_time_x = 0.0f;
  kf_time_p = 1.0f;
}

// Kalman filter update for speed
float updateKalmanSpeed(float measurement) {
  // Prediction step
  kf_p = kf_p + kf_q;
  
  // Update step
  kf_k = kf_p / (kf_p + kf_r);
  kf_x = kf_x + kf_k * (measurement - kf_x);
  kf_p = (1.0f - kf_k) * kf_p;
  
  return kf_x;
}

// Kalman filter update for time intervals
float updateKalmanTime(float measurement) {
  // Prediction step
  kf_time_p = kf_time_p + kf_time_q;
  
  // Update step
  kf_time_k = kf_time_p / (kf_time_p + kf_time_r);
  kf_time_x = kf_time_x + kf_time_k * (measurement - kf_time_x);
  kf_time_p = (1.0f - kf_time_k) * kf_time_p;
  
  return kf_time_x;
}

// Check if speed has been maintained
void updateSpeedStability(float newSpeed) {
  currentTime = millis();
  
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

// Function to upload live velocity data to Firebase
void uploadLiveData() {
  FirebaseJson json;
  json.set("speed", filteredSpeed);
  json.set("raw_speed", currentSpeed);
  json.set("stability_time", speedMaintainedTime);
  json.set("is_stable", isSpeedStable);
  json.set("consecutive_count", consecutive_count);
  json.set("timestamp", millis());
  
  Serial.println("Uploading live velocity data to Firebase...");
  
  if (Firebase.setJSON(fbdo, "/velocity/latest", json)) {
    Serial.println("Live velocity data uploaded successfully!");
  } else {
    Serial.println("Failed to upload live velocity data:");
    Serial.println(fbdo.errorReason());
  }
}

// Function to upload history velocity data to Firebase
void uploadHistoryData() {
  // Only upload if we have valid speed data
  if (filteredSpeed > 0.1) {
    FirebaseJson json;
    json.set("speed", filteredSpeed);
    json.set("raw_speed", currentSpeed);
    json.set("stability_time", speedMaintainedTime);
    json.set("is_stable", isSpeedStable);
    json.set("consecutive_count", consecutive_count);
    json.set("timestamp", millis());
    
    Serial.println("Uploading history velocity data to Firebase...");
    
    if (Firebase.pushJSON(fbdo, "/velocity/history", json)) {
      Serial.println("History velocity data pushed successfully!");
    } else {
      Serial.println("Failed to push history velocity data:");
      Serial.println(fbdo.errorReason());
    }
  }
}

void setup() {
  // Start serial communication at 115200 baud rate.
  Serial.begin(115200);
  
  // Connect to WiFi
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
  
  // Initialize Firebase
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
  
  Serial.println("--- Starting Digital Object Detection with Kalman Filter ---");
  Serial.print("Threshold set to: ");
  Serial.println(DETECTION_THRESHOLD);
  
  Serial.println("System initialized. Ready for speed measurement and Firebase upload.");
  Serial.println("FilteredSpeed(m/s),RawSpeed(m/s),StabilityTime(s),Stable");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Read the raw analog value
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
        
        // Print velocity data to serial
        Serial.print("Filtered: ");
        Serial.print(filteredSpeed, 1);
        Serial.print(" m/s, Raw: ");
        Serial.print(currentSpeed, 1);
        Serial.print(" m/s, Stable: ");
        Serial.print(speedMaintainedTime, 1);
        Serial.print(" s, Status: ");
        Serial.println(isSpeedStable ? "STABLE" : "NOT STABLE");
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
        
        // Apply Kalman filter to consecutive count
        float filtered_consecutive = updateKalmanTime(consecutive_count);
        
        // Final speed reading for this interval
        Serial.print("Interval Complete - Filtered: ");
        Serial.print(filteredSpeed, 1);
        Serial.println(" m/s");
        
        // Reset speed stability
        isSpeedStable = false;
        speedMaintainedTime = 0.0;
      }
      
      // Reset for new state
      current_state = sense_value;
      consecutive_count = 1;
    }
  }

  // Firebase Live Data Upload (every 2 seconds)
  if (currentMillis - lastLiveUpdate > LIVE_UPDATE_INTERVAL) {
    uploadLiveData();
    lastLiveUpdate = currentMillis;
  }
  
  // Firebase History Data Upload (every 30 seconds)
  if (currentMillis - lastHistoryUpdate > HISTORY_UPDATE_INTERVAL) {
    uploadHistoryData();
    lastHistoryUpdate = currentMillis;
  }

  // Wait before next reading
  delay(deltay_time);
}
