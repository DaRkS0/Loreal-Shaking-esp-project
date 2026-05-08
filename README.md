# 🍾 Bottle Shake Detector (ESP32 + Python)

This project uses an **ESP32 Super Mini** and an **MPU-6500** accelerometer to detect when a bottle is being shaken. When a shake is detected, the ESP32 sends a signal over WiFi to a Python application on your PC, which triggers a "Bottle Shaking" animation.

The project features **Bluetooth Low Energy (BLE) Provisioning**, allowing you to configure your WiFi credentials directly from the Python app without hardcoding them.

---

## 🛠 Hardware Setup

### Components
- **ESP32 Super Mini** (ESP32-C3)
- **MPU-6500** (6-Axis IMU)

### Wiring Diagram
| MPU-6500 Pin | ESP32 GPIO | Description |
| :--- | :--- | :--- |
| **VCC** | **3.3V** | Power |
| **GND** | **GND** | Ground |
| **SCL** | **GPIO 9** | I2C Clock |
| **SDA** | **GPIO 8** | I2C Data |
| **NCS** | **3.3V** | Enables I2C mode on MPU-6500 |

---

## 💻 Software Components

### 1. ESP32 Firmware (`esp32_code.ino`)
- **BLE Server**: Advertises as "Bottle".
- **WiFi Provisioning**: Receives `SCAN` to list networks or `SSID;PASS` to connect.
- **Shake Logic**: Calculates the G-force magnitude using the MPU-6500.
- **UDP Reporting**: Sends a "SHAKE" packet to the PC on port `5000`.

### 2. Python Host App (`host_app.py`)
- **Tkinter GUI**: Displays a bottle that shakes when the signal is received.
- **BLE Configurator**: A built-in tool to scan for the ESP32 and configure its WiFi.
- **UDP Listener**: Listens for incoming signals from the ESP32.

---

## 🚀 Installation & Usage

### 1. ESP32 Setup
1. Open `esp32_code/esp32_code.ino` in the Arduino IDE.
2. Install the ESP32 board support (Select **ESP32C3 Dev Module**).
3. Connect your ESP32 and click **Upload**.
   - *Note: If uploading fails, lower the "Upload Speed" to 115200 in the Tools menu.*

### 2. Python Setup
1. Install the required Bluetooth library:
   ```bash
   pip install bleak
   ```
2. Run the application:
   ```bash
   python host_app.py
   ```

### 3. Connecting to WiFi
1. Click the **⚙ Configure WiFi (BLE)** button in the Python app.
2. Click **1. Scan ESP32 Networks**.
3. Select your WiFi from the list, enter your password, and click **2. Send Credentials**.
4. The ESP32 will now connect to your WiFi and remember it for next time!

---

## ⚡ Tuning Sensitivity

The shake detection is based on the acceleration magnitude. In the `.ino` file, you will find:
```cpp
const float SHAKE_THRESHOLD = 25.0;
```

### How to fine-tune:
1. Open the **Serial Monitor** in the Arduino IDE (115200 baud).
2. Move or shake the bottle. You will see `Current Magnitude: XX.XX` printed.
3. **If it's too sensitive**: Increase the `SHAKE_THRESHOLD` (e.g., to `35.0`).
4. **If it's too hard to trigger**: Decrease the `SHAKE_THRESHOLD` (e.g., to `20.0`).

*Resting gravity is ~9.8. Anything above that is motion!*

---

## 📁 Project Structure
```text
.
├── esp32_code/
│   └── esp32_code.ino    # ESP32 C++ Firmware
├── host_app.py           # Main Python Application
├── test_udp.py           # Script to test UI without ESP32
└── README.md             # This file!
```
