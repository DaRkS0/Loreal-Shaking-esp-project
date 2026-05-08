#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <Wire.h>
#include <math.h>

// UUIDs for BLE Service and Characteristic
#define SERVICE_UUID           "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

Preferences preferences;
String ssid = "";
String password = "";

WiFiUDP udp;
const int udpPort = 5000;
IPAddress broadcastIP(255, 255, 255, 255);

bool requestWiFiScan = false;

const int MPU_ADDR = 0x68; // MPU-6500 typical I2C address
const float SHAKE_THRESHOLD = 25.0; // Higher value = less sensitive
unsigned long lastShakeTime = 0;
const int SHAKE_COOLDOWN = 500; // ms between sending signals

void setupMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0);    // Wake up
  Wire.endTransmission(true);
}

void readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // Starting register for Accel Readings (ACCEL_XOUT_H)
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true); 
  
  if (Wire.available() >= 6) {
    int16_t x = Wire.read() << 8 | Wire.read();
    int16_t y = Wire.read() << 8 | Wire.read();
    int16_t z = Wire.read() << 8 | Wire.read();
    
    // Default MPU range is +/- 2g. Sensitivity is 16384 LSB/g.
    // 1g is ~9.81 m/s^2
    ax = (x / 16384.0) * 9.81;
    ay = (y / 16384.0) * 9.81;
    az = (z / 16384.0) * 9.81;
  } else {
    ax = ay = az = 0;
  }
}

void connectToWiFi() {
  if (ssid == "") return;
  
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFailed to connect to WiFi. Will continue advertising BLE.");
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("BLE Device Connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("BLE Device Disconnected");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      uint8_t* rxData = pChar->getData();
      size_t rxLen = pChar->getLength();

      if (rxLen > 0) {
        String msg = "";
        for (int i = 0; i < rxLen; i++) {
          msg += (char)rxData[i];
        }
        
        Serial.print("Received BLE Message: ");
        Serial.println(msg);
        msg.trim();

        if (msg == "SCAN") {
          Serial.println("WiFi Scan requested via BLE.");
          requestWiFiScan = true;
        }
        else {
          // Expect format: SSID;PASSWORD
          int separatorIdx = msg.indexOf(';');
          if (separatorIdx != -1) {
            ssid = msg.substring(0, separatorIdx);
            password = msg.substring(separatorIdx + 1);
          ssid.trim();
          password.trim();

          Serial.println("Saving WiFi credentials to NVS...");
          preferences.begin("wifi_creds", false); // false = read/write
          preferences.putString("ssid", ssid);
          preferences.putString("password", password);
          preferences.end();
          
          connectToWiFi();
        } else {
          Serial.println("Invalid format received. Use: SSID;PASSWORD or SCAN");
        }
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 Bottle Shake Detector ---");
  
  // 1. START BLE FIRST (to ensure it works even if I2C hangs)
  Serial.println("Starting BLE Server...");
  BLEDevice::init("Bottle");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  
  pAdvertising->setMinPreferred(0x12);
  pAdvertising->start();
  
  Serial.println("--- BLE Ready! Connect to 'Bottle' ---");

  // 2. Initialize I2C for MPU-6500
  Serial.println("Initializing I2C on Pins 8(SDA), 9(SCL)...");
  Wire.begin(8, 9);
  
  // Check if MPU is responding
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.println("WARNING: MPU-6500 not found at 0x68! Check your wiring.");
  } else {
    Serial.println("MPU-6500 found! Configuring...");
    setupMPU();
  }

  // 3. Load WiFi credentials from Non-Volatile Storage (NVS)
  preferences.begin("wifi_creds", true); // true = read-only
  ssid = preferences.getString("ssid", "");
  password = preferences.getString("password", "");
  preferences.end();

  if (ssid != "") {
    connectToWiFi();
  } else {
    Serial.println("No WiFi credentials found in memory.");
  }
}

void loop() {
  // BLE reconnection handling
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); 
      BLEDevice::startAdvertising(); 
      Serial.println("Restarting BLE advertising...");
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }

  // Handle WiFi Scan Request
  if (requestWiFiScan) {
    requestWiFiScan = false;
    Serial.println("Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    String scanResult = "NETWORKS:";
    for (int i = 0; i < n; ++i) {
      scanResult += WiFi.SSID(i);
      if (i < n - 1) {
        scanResult += ",";
      }
    }
    Serial.println("Scan complete. Sending results over BLE...");
    pCharacteristic->setValue(scanResult.c_str());
    pCharacteristic->notify();
  }

  // Read MPU-6500 Acceleration Data
  float ax, ay, az;
  readAccel(ax, ay, az);
  
  // Calculate total acceleration vector magnitude
  float magnitude = sqrt(ax*ax + ay*ay + az*az);
  
  // Log magnitude if there is significant movement (to help you tune the threshold)
  if (magnitude > 15.0) {
    Serial.print("Current Magnitude: ");
    Serial.println(magnitude);
  }
  
  // Normal resting magnitude is around 9.81 (gravity)
  // If we exceed our threshold, it means we are shaking/moving fast
  if (magnitude > SHAKE_THRESHOLD) {
    if (millis() - lastShakeTime > SHAKE_COOLDOWN) {
      lastShakeTime = millis();
      Serial.print("SHAKE DETECTED! Magnitude: ");
      Serial.println(magnitude);
      
      // Send UDP broadcast over WiFi
      if (WiFi.status() == WL_CONNECTED) {
        udp.beginPacket(broadcastIP, udpPort);
        udp.print("SHAKE");
        udp.endPacket();
        Serial.println(">>> Sent UDP 'SHAKE' Broadcast <<<");
      }
    }
  }
  
  delay(20); // Small delay for stability
}
