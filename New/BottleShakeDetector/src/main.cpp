#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsServer.h>
#include <ESPmDNS.h>

#define USE_SERIAL Serial

String ssid = "smiles Inc.";
String password = "smilesinc2013";

const int MPU_ADDR = 0x68;          // MPU-6500 typical I2C address
const float SHAKE_THRESHOLD = 25.0; // Higher value = less sensitive
unsigned long lastShakeTime = 0;
const int SHAKE_COOLDOWN = 500; // ms between sending signals

// ── Pickup / lift detection ────────────────────────────────────────────────────
// When the bottle is resting flat the Z-axis carries ~9.81 m/s² (1g).
// A lift causes a brief dip (unloading) then a spike, but crucially the
// *gravity vector direction* changes. We detect a pickup by watching for
// the low-pass-filtered gravity magnitude changing axis distribution, or more
// simply: a sustained change in the vertical (Z) gravity component.
//
// PICKUP_DELTA: how much the filtered gravity vector must shift from its
//   resting value to count as a pickup. 3.5 m/s² ≈ 0.36g — easily triggered
//   by lifting but not by a gentle table bump.
// PICKUP_SUSTAIN_MS: the change must persist this long to avoid false triggers
//   from a momentary knock.
const float PICKUP_DELTA = 2;                // m/s²
const unsigned long PICKUP_SUSTAIN_MS = 300; // ms

// Low-pass filter coefficient for gravity estimation (0 < α < 1).
// Lower α = slower/smoother tracking of gravity orientation.
// 0.05 at 20 ms loop = ~300 ms time constant — good for separating
// slow gravity reorientation from fast shake dynamics.
const float LP_ALPHA = 0.05f;

// Gravity estimate (initialised to resting flat: Z ≈ 9.81)
float gx = 0.0f, gy = 0.0f, gz = 9.81f;

// Pickup state machine
float restGx = 0.0f, restGy = 0.0f, restGz = 9.81f; // gravity when at rest
bool restCalibrated = false;
unsigned long pickupStartMs = 0;
bool inPickupCandidate = false;

void readAccel(float &ax, float &ay, float &az);
void setupMPU();

WebSocketsServer webSocket = WebSocketsServer(9090);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length);
void setupWIFI();
void sendTXT(const char * payload);
void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ESP32 Bottle Shake Detector ---");

  Serial.println("Initializing I2C on Pins 8(SDA), 9(SCL)...");
  Wire.begin(8, 9);

  // Check if MPU is responding
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0)
  {
    Serial.println("WARNING: MPU-6500 not found at 0x68! Check your wiring.");
  }
  else
  {
    Serial.println("MPU-6500 found! Configuring...");
    setupMPU();
  }
  setupWIFI();
  delay(5000);
  Serial.println("Calibrating gravity reference...");
  for (int i = 0; i < 50; i++)
  {
    float ax, ay, az;
    readAccel(ax, ay, az);
    gx = ax;
    gy = ay;
    gz = az; // hard-set for fast convergence at startup
    delay(20);
  }
  restGx = gx;
  restGy = gy;
  restGz = gz;
  restCalibrated = true;
  Serial.println("Gravity calibrated. Ready.");
}

void loop()
{
  webSocket.loop();
 

  // put your main code here, to run repeatedly:
  // ── Read raw acceleration ──────────────────────────────────────────────────
  float ax, ay, az;
  readAccel(ax, ay, az);

  // ── Update gravity estimate via low-pass filter ────────────────────────────
  // This smoothly tracks slow orientation changes (like tilting the bottle)
  // while ignoring rapid shake dynamics.
  gx = LP_ALPHA * ax + (1.0f - LP_ALPHA) * gx;
  gy = LP_ALPHA * ay + (1.0f - LP_ALPHA) * gy;
  gz = LP_ALPHA * az + (1.0f - LP_ALPHA) * gz;

  // ── Dynamic acceleration = raw − gravity estimate ──────────────────────────
  // This is what's left after removing gravity: pure motion signal.
  float dx = ax - gx;
  float dy = ay - gy;
  float dz = az - gz;
  float dynamicMag = sqrt(dx * dx + dy * dy + dz * dz);

  // ── Shake detection (dynamic acceleration only) ────────────────────────────
  // Picking the bottle straight up doesn't generate high dynamic acceleration
  // unless it's done very abruptly — a slow lift will be ~1–3 m/s² dynamic.
  if (dynamicMag > SHAKE_THRESHOLD)
  {
    if (millis() - lastShakeTime > SHAKE_COOLDOWN)
    {
      lastShakeTime = millis();
      Serial.print("SHAKE DETECTED! Dynamic mag: ");
      Serial.println(dynamicMag);
      sendTXT("SHAKE");
    }
  }

  // ── Pickup / vertical lift detection ──────────────────────────────────────
  // We compare the current gravity vector to the resting reference.
  // A pickup shifts the gravity distribution (the device reorients, and/or
  // the Z component drops as the user lifts and tilts it naturally).
  // We only update the rest reference while the device is genuinely still.
  if (restCalibrated)
  {
    float gdx = gx - restGx;
    float gdy = gy - restGy;
    float gdz = gz - restGz;
    float gravityShift = sqrt(gdx * gdx + gdy * gdy + gdz * gdz);

    if (gravityShift > PICKUP_DELTA)
    {

      // Gravity vector has shifted — possible pickup / reorientation
      if (!inPickupCandidate)
      {
        inPickupCandidate = true;
        pickupStartMs = millis();
      }
      else if (millis() - pickupStartMs >= PICKUP_SUSTAIN_MS)
      {
        // Sustained shift → confirmed pickup event
        inPickupCandidate = false;

        // Update rest reference to the new orientation so we don't
        // re-trigger continuously while the bottle is held tilted.
        restGx = gx;
        restGy = gy;
        restGz = gz;

        Serial.print("PICKUP DETECTED! Gravity shift: ");
        Serial.println(gravityShift);
        sendTXT("PICKUP");
      }
    }
    else
    {
      // Gravity is close to rest — device is still; reset candidate and
      // keep updating the rest reference so it adapts to slow drifts.
      inPickupCandidate = false;
      // Slowly re-calibrate rest reference while at rest
      restGx = 0.99f * restGx + 0.01f * gx;
      restGy = 0.99f * restGy + 0.01f * gy;
      restGz = 0.99f * restGz + 0.01f * gz;
    }
  }

  delay(20); // Small delay for stability
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
  case WStype_DISCONNECTED:
    Serial.printf("[%u] Disconnected!\n", num);
    break;

  case WStype_CONNECTED:
  {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n",
                  num, ip[0], ip[1], ip[2], ip[3], payload);
    webSocket.sendTXT(num, "Connected");
  }
  break;

  case WStype_TEXT:
    Serial.printf("[%u] get Text: %s\n", num, payload);
    break;
  }
}

void setupWIFI()
{
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // mDNS
  if (!MDNS.begin("esp32"))
  {
    Serial.println("Error starting mDNS");
    return;
  }

  Serial.println("mDNS responder started");
  Serial.println("Hostname: esp32.local");

  MDNS.addService("ws", "tcp", 81);

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void setupMPU()
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0);    // Wake up
  Wire.endTransmission(true);

  // Set accelerometer full-scale range to ±4g for better resolution on shakes
  // Register 0x1C (ACCEL_CONFIG), bits [4:3] = 01 → ±4g, sensitivity 8192 LSB/g
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08); // FS_SEL = 01
  Wire.endTransmission(true);
}

// sensitivity changes with FS_SEL — use 8192 for ±4g
void readAccel(float &ax, float &ay, float &az)
{
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  if (Wire.available() >= 6)
  {
    int16_t x = Wire.read() << 8 | Wire.read();
    int16_t y = Wire.read() << 8 | Wire.read();
    int16_t z = Wire.read() << 8 | Wire.read();

    // ±4g range → 8192 LSB/g → convert to m/s²
    ax = (x / 8192.0f) * 9.81f;
    ay = (y / 8192.0f) * 9.81f;
    az = (z / 8192.0f) * 9.81f;
  }
  else
  {
    ax = ay = az = 0;
  }
}

void sendTXT(const char * payload){

   int connectedClients = webSocket.connectedClients(false);
   for (size_t iuser = 0; iuser < connectedClients; iuser++)
   {
    webSocket.sendTXT(iuser, (uint8_t *)payload);
   }
}