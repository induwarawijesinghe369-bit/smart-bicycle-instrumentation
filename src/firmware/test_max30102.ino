#include "config.h"

#include <Wire.h>
#include <WiFi.h>
#include <FirebaseESP32.h>
#include "MAX30105.h"
#include "heartRate.h"

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

MAX30105 particleSensor;

float irValue;
float redValue;
float dc;
float alpha = 0.95;

const byte RATE_SIZE = 4; 
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

float beatsPerMinute;
int beatAvg;

// Variables for SpO2
#define BUFFER_SIZE 100
#define SPO2_ARRAY_SIZE 8

float irBuffer[BUFFER_SIZE];
float redBuffer[BUFFER_SIZE];
int bufferIndex = 0;
bool bufferFull = false;

float spo2Values[SPO2_ARRAY_SIZE];
byte spo2Index = 0;
int spo2Avg = 0;

long lastSpo2Calculation = 0;
const long SPO2_CALCULATION_INTERVAL = 2000;

// Variables for improved SpO2 calculation
float irDCRemoved[BUFFER_SIZE];
float redDCRemoved[BUFFER_SIZE];

// Timers for Firebase updates
unsigned long lastLiveUpdate = 0;
const unsigned long LIVE_UPDATE_INTERVAL = 2000; // 2 seconds

unsigned long lastHistoryUpdate = 0;
const unsigned long HISTORY_UPDATE_INTERVAL = 30000; // 30 seconds

// History data structure
struct HistoryData {
  unsigned long timestamp;
  int bpm;
  int spo2;
};

// Array to store history (optional, if you want to buffer before sending)
#define HISTORY_BUFFER_SIZE 10
HistoryData historyBuffer[HISTORY_BUFFER_SIZE];
int historyIndex = 0;

void beat(float ir) {
  if (checkForBeat(ir) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }
}

// Improved SpO2 calculation
void calculateSpO2() {
  int sampleCount = bufferFull ? BUFFER_SIZE : bufferIndex;
  if (sampleCount < 50) return;
  
  float irDC = 0, redDC = 0;
  for (int i = 0; i < sampleCount; i++) {
    irDC += irBuffer[i];
    redDC += redBuffer[i];
  }
  irDC /= sampleCount;
  redDC /= sampleCount;
  
  for (int i = 0; i < sampleCount; i++) {
    irDCRemoved[i] = irBuffer[i] - irDC;
    redDCRemoved[i] = redBuffer[i] - redDC;
    
    if (i > 0) {
      irDCRemoved[i] = 0.5 * irDCRemoved[i] + 0.5 * irDCRemoved[i-1];
      redDCRemoved[i] = 0.5 * redDCRemoved[i] + 0.5 * redDCRemoved[i-1];
    }
  }
  
  float irMax = -100000, irMin = 100000;
  float redMax = -100000, redMin = 100000;
  
  for (int i = 10; i < sampleCount - 10; i++) {
    if (irDCRemoved[i] > irMax) irMax = irDCRemoved[i];
    if (irDCRemoved[i] < irMin) irMin = irDCRemoved[i];
    if (redDCRemoved[i] > redMax) redMax = redDCRemoved[i];
    if (redDCRemoved[i] < redMin) redMin = redDCRemoved[i];
  }
  
  float irAC = irMax - irMin;
  float redAC = redMax - redMin;
  
  if (irDC <= 0 || redDC <= 0 || irAC <= 0 || redAC <= 0) {
    return;
  }
  
  float R = (redAC / redDC) / (irAC / irDC);
  float estimatedSpo2 = 0;
  
  if (R > 1.0) {
    estimatedSpo2 = 110.0 - 18.0 * R;
  } else if (R > 0.4) {
    estimatedSpo2 = 104.0 - 17.0 * R;
  } else {
    estimatedSpo2 = 100.0 - 15.0 * R;
  }
  
  if (estimatedSpo2 > 100) estimatedSpo2 = 100;
  if (estimatedSpo2 < 85) estimatedSpo2 = 85;
  
  if (estimatedSpo2 >= 85 && estimatedSpo2 <= 100) {
    spo2Values[spo2Index++] = estimatedSpo2;
    spo2Index %= SPO2_ARRAY_SIZE;
    
    spo2Avg = 0;
    int count = 0;
    for (byte x = 0; x < SPO2_ARRAY_SIZE; x++) {
      if (spo2Values[x] > 0) {
        spo2Avg += spo2Values[x];
        count++;
      }
    }
    if (count > 0) {
      spo2Avg /= count;
    }
  }
}

// Function to upload live data to Firebase
void uploadLiveData() {
  if (beatAvg > 0 && spo2Avg > 0) {
    FirebaseJson json;
    json.set("bpm", beatAvg);
    json.set("spo2", spo2Avg);
    json.set("timestamp", millis());
    
    Serial.println("Uploading live data to Firebase...");
    
    if (Firebase.setJSON(fbdo, "/max30102_01/latest", json)) {
      Serial.println("Live data uploaded successfully!");
    } else {
      Serial.println("Failed to upload live data:");
      Serial.println(fbdo.errorReason());
    }
  }
}

// Function to upload history data to Firebase
void uploadHistoryData() {
  if (beatAvg > 0 && spo2Avg > 0) {
    // Create a new history entry with timestamp
    String historyPath = "/max30102_01/history/";
    historyPath += String(millis()); // Using millis as unique ID
    
    FirebaseJson json;
    json.set("bpm", beatAvg);
    json.set("spo2", spo2Avg);
    json.set("timestamp", millis());
    
    Serial.println("Uploading history data to Firebase...");
    
    if (Firebase.setJSON(fbdo, historyPath.c_str(), json)) {
      Serial.println("History data uploaded successfully!");
    } else {
      Serial.println("Failed to upload history data:");
      Serial.println(fbdo.errorReason());
    }
  }
}

// Alternative: Push data to an array (auto-generates unique key)
void uploadHistoryDataPush() {
  if (beatAvg > 0 && spo2Avg > 0) {
    FirebaseJson json;
    json.set("bpm", beatAvg);
    json.set("spo2", spo2Avg);
    json.set("timestamp", millis());
    
    Serial.println("Pushing history data to Firebase...");
    
    if (Firebase.pushJSON(fbdo, "/max30102_01/history", json)) {
      Serial.println("History data pushed successfully!");
      Serial.println("Path: " + fbdo.dataPath());
      Serial.println("Push Name: " + fbdo.pushName());
    } else {
      Serial.println("Failed to push history data:");
      Serial.println(fbdo.errorReason());
    }
  }
}

void setup() {
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
  
  // Set the size of Firebase data upload buffer
  fbdo.setBSSLBufferSize(1024, 1024);
  
  // Optional: Set the size of HTTP response buffer
  fbdo.setResponseSize(1024);
  
  // Optional: Set Firebase read timeout to 1 minute
  Firebase.setReadTimeout(fbdo, 1000 * 60);
  
  // Optional: Set size and number of retry attempts for upload
  Firebase.setwriteSizeLimit(fbdo, "tiny");
  
  Serial.println("Firebase initialized!");
  
  // Initialize MAX30105 sensor
  Serial.println("Initializing MAX30105...");
  
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30105 was not found. Please check wiring/power.");
    while (1);
  }
  
  Serial.println("Place your index finger on the sensor with steady pressure.");
  Serial.println("Keep your hand still for accurate readings.");
  
  // Configure sensor with optimized settings
  byte ledBrightness = 0x1F;
  byte sampleAverage = 4;
  byte ledMode = 2;
  int sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);
  
  // Initialize buffers
  for (int i = 0; i < BUFFER_SIZE; i++) {
    irBuffer[i] = 0;
    redBuffer[i] = 0;
    irDCRemoved[i] = 0;
    redDCRemoved[i] = 0;
  }
  
  for (int i = 0; i < SPO2_ARRAY_SIZE; i++) {
    spo2Values[i] = 0;
  }
  
  // Initialize history buffer
  for (int i = 0; i < HISTORY_BUFFER_SIZE; i++) {
    historyBuffer[i] = {0, 0, 0};
  }
  
  delay(1000);
}

void loop() {
  // Read sensor values
  irValue = particleSensor.getIR();
  redValue = particleSensor.getRed();
  
  // Check for finger presence
  static float irFiltered = 0;
  irFiltered = 0.95 * irFiltered + 0.05 * irValue;
  
  if (irFiltered < 50000) {
    Serial.println("No finger detected. Please place finger on sensor.");
    bufferIndex = 0;
    bufferFull = false;
    spo2Avg = 0;
    delay(500);
    return;
  }
  
  // Store in buffers
  irBuffer[bufferIndex] = irValue;
  redBuffer[bufferIndex] = redValue;
  
  bufferIndex++;
  if (bufferIndex >= BUFFER_SIZE) {
    bufferIndex = 0;
    bufferFull = true;
  }
  
  // Calculate heart rate
  beat(irValue);
  
  // Calculate SpO2 less frequently
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastSpo2Calculation > SPO2_CALCULATION_INTERVAL) {
    if (bufferFull) {
      calculateSpO2();
    }
    lastSpo2Calculation = currentMillis;
  }
  
  // Firebase Live Data Upload (every 2 seconds)
  if (currentMillis - lastLiveUpdate > LIVE_UPDATE_INTERVAL) {
    if (beatAvg > 0 && spo2Avg > 0) {
      uploadLiveData();
    }
    lastLiveUpdate = currentMillis;
  }
  
  // Firebase History Data Upload (every 30 seconds)
  if (currentMillis - lastHistoryUpdate > HISTORY_UPDATE_INTERVAL) {
    if (beatAvg > 0 && spo2Avg > 0) {
      // You can use either uploadHistoryData() or uploadHistoryDataPush()
      uploadHistoryDataPush(); // This auto-generates unique keys
    }
    lastHistoryUpdate = currentMillis;
  }
  
  // Display results on Serial Monitor
  static unsigned long lastDisplay = 0;
  if (currentMillis - lastDisplay > 1000) {
    Serial.print("Avg BPM: ");
    Serial.print(beatAvg);
    Serial.print(" | SpO2: ");
    if (spo2Avg > 0) {
      Serial.print(spo2Avg);
      Serial.print("%");
    } else {
      Serial.print("Calculating...");
    }
    Serial.print(" | Next Firebase Live: ");
    Serial.print((LIVE_UPDATE_INTERVAL - (currentMillis - lastLiveUpdate)) / 1000);
    Serial.print("s | Next History: ");
    Serial.print((HISTORY_UPDATE_INTERVAL - (currentMillis - lastHistoryUpdate)) / 1000);
    Serial.println("s");
    
    lastDisplay = currentMillis;
  }
}
