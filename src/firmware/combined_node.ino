#include "config.h"

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <TinyGPSPlus.h>

// WiFi Credentials
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Firebase Configuration
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""

// GPS
TinyGPSPlus gps;
HardwareSerial SerialGPS(2);  // RX=16, TX=17

// Firebase Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

void setup() {
  Serial.begin(115200);
  SerialGPS.begin(9600, SERIAL_8N1, 16, 17);  // GPS at 9600 baud
  delay(3000);
  
  Serial.println("============================");
  Serial.println("   GPS FIREBASE TRACKER    ");
  Serial.println("============================");
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\n✅ WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Configure Firebase
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("✅ Firebase Initialized");
  
  Serial.println("\n📡 Waiting for GPS signal...");
  Serial.println("Take device outside/near window");
  Serial.println("============================\n");
}

void loop() {
  // Read GPS data
  while (SerialGPS.available() > 0) {
    gps.encode(SerialGPS.read());
  }
  
  // Check if GPS has new data
  if (gps.location.isUpdated()) {
    displayGPSInfo();
    sendToFirebase();
    delay(10000);  // Send every 10 seconds
  }
  
  // Show searching status every 5 seconds
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    Serial.print(".");
    if (gps.satellites.isValid()) {
      Serial.print(" Satellites: ");
      Serial.println(gps.satellites.value());
    }
  }
  
  delay(100);
}

void displayGPSInfo() {
  Serial.println("\n📍 GPS FIX ACQUIRED!");
  Serial.print("Latitude:  ");
  Serial.println(gps.location.lat(), 6);
  Serial.print("Longitude: ");
  Serial.println(gps.location.lng(), 6);
  Serial.print("Satellites: ");
  Serial.println(gps.satellites.value());
  Serial.print("Speed:     ");
  Serial.print(gps.speed.kmph());
  Serial.println(" km/h");
}

void sendToFirebase() {
  FirebaseJson json;
  json.set("latitude", gps.location.lat());
  json.set("longitude", gps.location.lng());
  json.set("speed", gps.speed.kmph());
  json.set("altitude", gps.altitude.meters());
  json.set("satellites", gps.satellites.value());
  json.set("timestamp", millis());
  
  // Store in latest location (as before)
  if (Firebase.setJSON(fbdo, "/gps/latest", json)) {
    Serial.println("✅ Data sent to Firebase (latest)");
  } else {
    Serial.println("❌ Firebase error: " + fbdo.errorReason());
  }
  
  // NEW: Store in history with timestamp as key
  String historyKey = String(millis());
  String historyPath = "/gps/history/" + historyKey;
  
  if (Firebase.setJSON(fbdo, historyPath.c_str(), json)) {
    Serial.println("✅ Added to history: " + historyKey);
  }
}
