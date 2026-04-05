# 🔹 Phase 1: Basic Health Monitoring System

## 📌 Overview

This phase implements a basic IoT-based health monitoring system using ESP32.
It reads sensor data, processes it using FreeRTOS tasks, displays it on an OLED, and sends it to the cloud via MQTT.

---

## 🚀 Features

* 🌡️ Body Temperature Monitoring (DS18B20)
* ❤️ Heart Rate Simulation (via switch control)
* 🫁 SpO2 Simulation (via switch control)
* 🚶 Motion Detection (PIR Sensor)
* 📟 OLED Display (SSD1306)
* 🔔 Multi-buzzer Alert System
* ☁️ Cloud Integration using Adafruit IO (MQTT)
* ⚡ FreeRTOS-based multitasking

---

## 🧠 System Architecture

* **Sensor Task (Core 1):**

  * Generates Heart Rate & SpO2 values
  * Sends data via FreeRTOS queues

* **Processing Task (Core 0):**

  * Reads temperature sensor
  * Processes all sensor data
  * Updates OLED display
  * Triggers alerts (buzzers)
  * Publishes data to cloud

---

## 🔌 Hardware Components

* ESP32 Dev Board
* DS18B20 Temperature Sensor
* PIR Motion Sensor
* OLED Display (SSD1306, I2C)
* 4 Buzzers
* 4 LEDs with resistors
* DIP Switch (for simulation control)

---

## ⚙️ Pin Configuration

| Component     | GPIO Pin |
| ------------- | -------- |
| Temperature   | 14       |
| PIR Sensor    | 12       |
| Buzzer 1      | 4        |
| Buzzer 2      | 16       |
| Buzzer 3      | 17       |
| Buzzer 4      | 5        |
| OLED SDA      | 21       |
| OLED SCL      | 22       |
| Switch Temp   | 32       |
| Switch HR     | 33       |
| Switch SpO2   | 25       |
| Switch Motion | 26       |

---

## 📊 Working Logic

* Temperature > 38°C → High Temperature Alert
* Heart Rate < 60 or > 100 → Abnormal HR Alert
* SpO2 < 90% → Low Oxygen Alert
* Motion detected → Security Alert

Each condition triggers:

* OLED message update
* Corresponding buzzer alert
* MQTT data publishing

---

## 📸 Circuit Diagram

### 🔹 Wiring Image

![Wiring Diagram](diagram/wiring.png)

### 🔹 Wokwi JSON

Available in: `diagram/diagram.json`

---

## 📊 Cloud Dashboard

![Adafruit Dashboard](screenshots/dashboard.png)

---

## 🔗 Wokwi Simulation

(Add your Wokwi project link here)

---

## 🧪 Technologies Used

* Embedded C / Arduino
* FreeRTOS (Tasks & Queues)
* MQTT Protocol
* ESP32 WiFi
* I2C Communication

---

## ⚠️ Notes

* Heart Rate and SpO2 are simulated using switches
* Replace with real sensors (e.g., MAX30102) in future phases
* Ensure WiFi connection for MQTT functionality

---


## 👨‍💻 Author

**Vishal Kumar**
ECE Student | IoT Enthusiast
