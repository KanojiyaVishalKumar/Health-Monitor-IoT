# 🏥 Phase 2 – Advanced Health Monitor IoT

## 🚀 Overview
Phase 2 upgrades the basic health monitoring system into a **multi-parameter, real-time IoT system** using **ESP32 + FreeRTOS + MQTT**.

This phase introduces:
- Blood Pressure Monitoring
- ECG Monitoring
- Multi-page OLED Display
- Intelligent Buzzer Alerts
- Cloud Integration (Adafruit IO)
- Multi-tasking using FreeRTOS

---

## 🧠 Features

### 🔹 Multi-Sensor Monitoring
- Body Temperature (DS18B20)
- Heart Rate (Simulated)
- SpO2 (Simulated)
- Blood Pressure (NEW)
- ECG Status + Rhythm (NEW)
- Motion Detection (PIR Sensor)

---

### 🔹 FreeRTOS Multi-Tasking
Tasks running in parallel:
- Sensor Task (HR + SpO2)
- Blood Pressure Task
- ECG Task
- Display Task
- Processing Task
- System Monitor Task

---

### 🔹 OLED Display (SSD1306)
3 rotating pages:
1. Vitals Page (Temp, HR, SpO2, Motion)
2. Advanced Page (BP + ECG)
3. Status Page (Alerts)

---

### 🔹 Smart Alert System

| Parameter | Condition |
|----------|----------|
| Temperature | > 38°C |
| Heart Rate | < 60 or > 100 |
| SpO2 | < 90% |
| Blood Pressure | High/Low |
| ECG | Abnormal |
| Motion | Detected |

---

### 🔹 IoT Cloud Integration
Data sent to **Adafruit IO via MQTT**

Feeds:
- Temperature
- Heart Rate
- SpO2
- Motion
- Blood Pressure (Systolic/Diastolic)
- ECG Status
- Overall Status

---

## 🧩 Hardware Components

- ESP32 DevKit V4
- DS18B20 Temperature Sensor
- PIR Motion Sensor
- SSD1306 OLED Display
- DIP Switch
- Buzzers (6)
- LEDs + Resistors
- Breadboard

---

## 🔌 Pin Configuration

| Component | Pin |
|----------|-----|
| Temp Sensor | 14 |
| Motion Sensor | 12 |
| Buzzer 1 | 4 |
| Buzzer 2 | 16 |
| Buzzer 3 | 17 |
| Buzzer 4 | 5 |
| Buzzer 5 | 2 |
| Buzzer 6 | 15 |
| OLED SDA | 21 |
| OLED SCL | 22 |

---

## 🎛️ DIP Switch

| Switch | Function |
|-------|--------|
| SW1 | Temperature |
| SW2 | Heart Rate |
| SW3 | SpO2 |
| SW4 | Motion |
| SW5 | Blood Pressure |
| SW6 | ECG |


---

## 🖥️ Simulation
- Built using Wokwi
- Import `wiring.json` to simulate circuit

---

## 📡 Working Flow
1. Sensors generate data
2. FreeRTOS tasks process it
3. OLED displays values
4. Alerts trigger buzzer + LED
5. Data sent to cloud

---

## 📊 Example Output
Temp: 37.5°C
HR: 78 bpm
SpO2: 97%
BP: 118/76 mmHg
ECG: Normal
Status: OK


---

## ⚡ Key Learnings
- FreeRTOS
- MQTT Protocol
- ESP32 Multi-tasking
- IoT System Design

---

## 🔮 Future Scope
- Real sensors integration
- Mobile app
- AI health prediction
- Emergency alerts

---

## 👨‍💻 Author
Kanojiya Vishal Kumar
