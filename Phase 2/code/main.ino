#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

// ============== PIN DEFINITIONS ==============
#define TEMP_SENSOR 14
#define MOTION_DETECTOR 12

// Buzzer pins (6 total - one for each parameter)
#define BUZZER_PIN_1 4    // Temperature alert
#define BUZZER_PIN_2 16   // Heart Rate alert
#define BUZZER_PIN_3 17   // SpO2 alert
#define BUZZER_PIN_4 5    // Motion alert
#define BUZZER_PIN_5 2    // Blood Pressure alert
#define BUZZER_PIN_6 15   // ECG alert

// DIP Switch pins (using 8-position DIP switch)
#define SW_TEMP 32
#define SW_HR 33
#define SW_SPO2 25
#define SW_MOTION 26
#define SW_BP 27          // NEW: Blood Pressure switch
#define SW_ECG 13         // NEW: ECG switch

// Display configuration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define OLED_ADDRESS 0x3C

// WiFi configuration
#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

// Adafruit IO configuration
#define IO_USERNAME  "Adafruituserid"
#define IO_KEY       "API"
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883

// MQTT Feed definitions
#define TEMP_FEED         IO_USERNAME "/feeds/body-temperature-c"
#define HR_FEED           IO_USERNAME "/feeds/heart-rate-monitor"
#define MOTION_FEED       IO_USERNAME "/feeds/motion-detector"
#define SPO2_FEED         IO_USERNAME "/feeds/spo2-percent"
#define FLAG              IO_USERNAME "/feeds/flag"
#define BP_SYS_FEED       IO_USERNAME "/feeds/blood-pressure-systolic"
#define BP_DIA_FEED       IO_USERNAME "/feeds/blood-pressure-diastolic"
#define ECG_FEED          IO_USERNAME "/feeds/ecg-status"

// ============== DATA STRUCTURES ==============

// Blood Pressure data structure
typedef struct {
    int systolic;    // Upper reading (normal: 90-120)
    int diastolic;   // Lower reading (normal: 60-80)
} BloodPressure_t;

// ECG Status enumeration
typedef enum {
    ECG_OFF = 0,
    ECG_NORMAL = 1,
    ECG_BRADYCARDIA = 2,    // Slow heart rhythm
    ECG_TACHYCARDIA = 3,    // Fast heart rhythm
    ECG_ARRHYTHMIA = 4,     // Irregular rhythm
    ECG_ST_ELEVATION = 5    // Possible MI indicator
} ECGStatus_t;

// Combined sensor data structure for thread-safe access
typedef struct {
    float temperature;
    int heartRate;
    int spo2;
    BloodPressure_t bloodPressure;
    ECGStatus_t ecgStatus;
    int ecgRhythmBPM;
    bool motionDetected;
} SensorData_t;

// ============== GLOBAL OBJECTS ==============

WiFiClient client;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, IO_USERNAME, IO_KEY);

// MQTT Publish feeds
Adafruit_MQTT_Publish tempFeed = Adafruit_MQTT_Publish(&mqtt, TEMP_FEED);
Adafruit_MQTT_Publish hrFeed = Adafruit_MQTT_Publish(&mqtt, HR_FEED);
Adafruit_MQTT_Publish spo2Feed = Adafruit_MQTT_Publish(&mqtt, SPO2_FEED);
Adafruit_MQTT_Publish statusFeed = Adafruit_MQTT_Publish(&mqtt, FLAG);
Adafruit_MQTT_Publish motiondetectorFeed = Adafruit_MQTT_Publish(&mqtt, MOTION_FEED);
Adafruit_MQTT_Publish bpSysFeed = Adafruit_MQTT_Publish(&mqtt, BP_SYS_FEED);
Adafruit_MQTT_Publish bpDiaFeed = Adafruit_MQTT_Publish(&mqtt, BP_DIA_FEED);
Adafruit_MQTT_Publish ecgFeed = Adafruit_MQTT_Publish(&mqtt, ECG_FEED);

// Sensor objects
OneWire oneWire(TEMP_SENSOR);
DallasTemperature sensors(&oneWire);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ============== FreeRTOS HANDLES ==============

// Queues for inter-task communication
QueueHandle_t hrQueue;
QueueHandle_t spo2Queue;
QueueHandle_t bpQueue;          // NEW: Blood Pressure queue
QueueHandle_t ecgQueue;         // NEW: ECG status queue
QueueHandle_t ecgBpmQueue;      // NEW: ECG rhythm BPM queue

// Semaphores for resource protection
SemaphoreHandle_t displayMutex;
SemaphoreHandle_t mqttMutex;
SemaphoreHandle_t serialMutex;

// Task handles for monitoring
TaskHandle_t sensorTaskHandle;
TaskHandle_t processingTaskHandle;
TaskHandle_t bpTaskHandle;      // NEW: BP task handle
TaskHandle_t ecgTaskHandle;     // NEW: ECG task handle
TaskHandle_t displayTaskHandle; // NEW: Display task handle

// Display page control
volatile int currentDisplayPage = 0;
#define TOTAL_DISPLAY_PAGES 3

// ============== HELPER FUNCTIONS ==============

// Thread-safe switch reading
bool isSwitchOn(int pin) {
    return digitalRead(pin) == HIGH;
}

// Thread-safe serial print
void safePrint(const char* message) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.print(message);
        xSemaphoreGive(serialMutex);
    }
}

void safePrintln(const char* message) {
    if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Serial.println(message);
        xSemaphoreGive(serialMutex);
    }
}

// Get ECG status string
const char* getECGStatusString(ECGStatus_t status) {
    switch(status) {
        case ECG_NORMAL: return "Normal";
        case ECG_BRADYCARDIA: return "Brady";
        case ECG_TACHYCARDIA: return "Tachy";
        case ECG_ARRHYTHMIA: return "Arrhyth";
        case ECG_ST_ELEVATION: return "ST Elev";
        default: return "OFF";
    }
}

// Get BP status string
const char* getBPStatus(int systolic, int diastolic) {
    if (systolic == 0 || diastolic == 0) return "OFF";
    if (systolic < 90 || diastolic < 60) return "Low";
    if (systolic > 140 || diastolic > 90) return "High";
    if (systolic > 120 || diastolic > 80) return "Elevated";
    return "Normal";
}

// ============== SENSOR TASKS ==============

/**
 * Original Sensor Task - Heart Rate and SpO2
 * Simulates pulse oximeter readings
 */
void sensorTask(void *param) {
    int heartRate = 0;
    int spo2 = 0;
    
    safePrintln("[SENSOR] HR/SpO2 sensor task started");

    while (1) {
        // Simulate Heart Rate (60-100 normal, can spike occasionally)
        if (isSwitchOn(SW_HR)) {
            // Simulate realistic heart rate variations
            int baseHR = 75;
            int variation = random(-15, 26);
            heartRate = baseHR + variation;
            
            // Occasionally simulate abnormal readings (5% chance)
            if (random(100) < 5) {
                heartRate = random(2) ? random(40, 55) : random(105, 130);
            }
        } else {
            heartRate = 0;
        }

        // Simulate SpO2 (95-100 normal)
        if (isSwitchOn(SW_SPO2)) {
            int baseSpo2 = 97;
            int variation = random(-2, 4);
            spo2 = baseSpo2 + variation;
            spo2 = constrain(spo2, 85, 100);
            
            // Occasionally simulate low SpO2 (3% chance)
            if (random(100) < 3) {
                spo2 = random(85, 89);
            }
        } else {
            spo2 = 0;
        }

        // Update queues with new values
        xQueueOverwrite(hrQueue, &heartRate);
        xQueueOverwrite(spo2Queue, &spo2);

        if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            Serial.printf("[SENSOR] HR: %d bpm, SpO2: %d%%\n", heartRate, spo2);
            xSemaphoreGive(serialMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/**
 * NEW: Blood Pressure Sensor Task
 * Simulates a blood pressure monitor with systolic/diastolic readings
 * Runs as an independent FreeRTOS task
 */
void bpSensorTask(void *param) {
    BloodPressure_t bp = {0, 0};
    int readingCycle = 0;
    
    safePrintln("[BP TASK] Blood Pressure sensor task started");
    
    while (1) {
        if (isSwitchOn(SW_BP)) {
            // Simulate BP measurement (takes time like real BP monitors)
            // Measurement cycle: inflate, hold, deflate, calculate
            
            readingCycle++;
            
            // Base values for normal BP
            int baseSystolic = 115;
            int baseDiastolic = 75;
            
            // Add realistic variations based on time/cycle
            int sysVariation = random(-10, 15);
            int diaVariation = random(-5, 10);
            
            // Circadian rhythm simulation (slight variations)
            if (readingCycle % 10 < 3) {
                // Morning surge simulation
                sysVariation += 5;
                diaVariation += 3;
            }
            
            bp.systolic = baseSystolic + sysVariation;
            bp.diastolic = baseDiastolic + diaVariation;
            
            // Ensure diastolic < systolic (pulse pressure validation)
            if (bp.diastolic >= bp.systolic) {
                bp.diastolic = bp.systolic - 30;
            }
            
            // Occasionally simulate hypertension (8% chance)
            if (random(100) < 8) {
                bp.systolic = random(145, 165);
                bp.diastolic = random(92, 100);
            }
            
            // Occasionally simulate hypotension (5% chance)
            if (random(100) < 5) {
                bp.systolic = random(85, 95);
                bp.diastolic = random(55, 62);
            }
            
            // Constrain to realistic ranges
            bp.systolic = constrain(bp.systolic, 70, 200);
            bp.diastolic = constrain(bp.diastolic, 40, 130);
            
        } else {
            bp.systolic = 0;
            bp.diastolic = 0;
        }
        
        // Send to queue
        xQueueOverwrite(bpQueue, &bp);
        
        if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            Serial.printf("[BP TASK] Blood Pressure: %d/%d mmHg (%s)\n", 
                         bp.systolic, bp.diastolic, 
                         getBPStatus(bp.systolic, bp.diastolic));
            xSemaphoreGive(serialMutex);
        }
        
        // BP readings typically every 3 seconds in continuous mode
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/**
 * NEW: ECG Monitoring Task
 * Simulates an ECG monitor with rhythm analysis
 * Detects various cardiac conditions
 * Runs as an independent FreeRTOS task
 */
void ecgSensorTask(void *param) {
    ECGStatus_t ecgStatus = ECG_OFF;
    int rhythmBPM = 0;
    int beatCounter = 0;
    unsigned long lastBeatTime = 0;
    
    safePrintln("[ECG TASK] ECG monitoring task started");
    
    while (1) {
        if (isSwitchOn(SW_ECG)) {
            // Simulate ECG analysis
            beatCounter++;
            
            // Calculate rhythm BPM from simulated R-R intervals
            int baseRhythm = 72;
            int variation = random(-8, 12);
            rhythmBPM = baseRhythm + variation;
            
            // Determine ECG status based on rhythm and random events
            int statusRoll = random(100);
            
            if (statusRoll < 75) {
                // 75% Normal sinus rhythm
                ecgStatus = ECG_NORMAL;
                rhythmBPM = random(60, 100);
            } 
            else if (statusRoll < 85) {
                // 10% Bradycardia
                ecgStatus = ECG_BRADYCARDIA;
                rhythmBPM = random(40, 58);
            }
            else if (statusRoll < 92) {
                // 7% Tachycardia
                ecgStatus = ECG_TACHYCARDIA;
                rhythmBPM = random(102, 150);
            }
            else if (statusRoll < 98) {
                // 6% Arrhythmia (irregular rhythm)
                ecgStatus = ECG_ARRHYTHMIA;
                rhythmBPM = random(50, 120);
            }
            else {
                // 2% ST Elevation (potential MI)
                ecgStatus = ECG_ST_ELEVATION;
                rhythmBPM = random(80, 110);
            }
            
            // Simulate waveform analysis metrics
            int prInterval = random(120, 200);   // Normal: 120-200ms
            int qrsWidth = random(80, 120);      // Normal: 80-120ms
            int qtInterval = random(350, 450);   // Normal: 350-450ms
            
            if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Serial.printf("[ECG TASK] Status: %s, Rhythm: %d bpm\n", 
                             getECGStatusString(ecgStatus), rhythmBPM);
                Serial.printf("[ECG TASK] PR: %dms, QRS: %dms, QT: %dms\n",
                             prInterval, qrsWidth, qtInterval);
                xSemaphoreGive(serialMutex);
            }
            
        } else {
            ecgStatus = ECG_OFF;
            rhythmBPM = 0;
        }
        
        // Update queues
        xQueueOverwrite(ecgQueue, &ecgStatus);
        xQueueOverwrite(ecgBpmQueue, &rhythmBPM);
        
        // ECG analysis every 2.5 seconds
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

/**
 * NEW: Display Task
 * Handles OLED display with multiple pages
 * Cycles through different parameter views
 */
void displayTask(void *param) {
    float temperature = 0;
    int heartRate = 0;
    int spo2 = 0;
    BloodPressure_t bp = {0, 0};
    ECGStatus_t ecgStatus = ECG_OFF;
    int ecgBpm = 0;
    int pageTimer = 0;
    
    safePrintln("[DISPLAY] Display task started");
    
    while (1) {
        // Read all sensor data from queues
        xQueuePeek(hrQueue, &heartRate, 0);
        xQueuePeek(spo2Queue, &spo2, 0);
        xQueuePeek(bpQueue, &bp, 0);
        xQueuePeek(ecgQueue, &ecgStatus, 0);
        xQueuePeek(ecgBpmQueue, &ecgBpm, 0);
        
        // Read temperature
        if (isSwitchOn(SW_TEMP)) {
            sensors.requestTemperatures();
            float newTemp = sensors.getTempCByIndex(0);
            if (newTemp != -127.0 && newTemp != 85.0) {
                temperature = newTemp;
            }
        }
        
        // Take display mutex
        if (xSemaphoreTake(displayMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            
            // Header with page indicator
            display.setCursor(0, 0);
            display.print("=== HEALTH MON ");
            display.print(currentDisplayPage + 1);
            display.print("/");
            display.print(TOTAL_DISPLAY_PAGES);
            display.println(" ===");
            
            switch (currentDisplayPage) {
                case 0:  // Page 1: Temp, HR, SpO2
                    // Temperature
                    display.setCursor(0, 12);
                    display.print("Temp: ");
                    if (isSwitchOn(SW_TEMP)) {
                        display.print(temperature, 1);
                        display.print(" C");
                        if (temperature > 38.0) display.print(" !");
                    } else {
                        display.print("OFF");
                    }
                    
                    // Heart Rate
                    display.setCursor(0, 24);
                    display.print("HR: ");
                    if (isSwitchOn(SW_HR)) {
                        display.print(heartRate);
                        display.print(" bpm");
                        if (heartRate < 60 || heartRate > 100) display.print(" !");
                    } else {
                        display.print("OFF");
                    }
                    
                    // SpO2
                    display.setCursor(0, 36);
                    display.print("SpO2: ");
                    if (isSwitchOn(SW_SPO2)) {
                        display.print(spo2);
                        display.print(" %");
                        if (spo2 < 90) display.print(" !");
                    } else {
                        display.print("OFF");
                    }
                    
                    // Motion
                    display.setCursor(0, 48);
                    display.print("Motion: ");
                    if (isSwitchOn(SW_MOTION)) {
                        display.print(digitalRead(MOTION_DETECTOR) ? "DETECTED!" : "None");
                    } else {
                        display.print("OFF");
                    }
                    break;
                    
                case 1:  // Page 2: Blood Pressure & ECG (NEW PARAMETERS)
                    // Blood Pressure
                    display.setCursor(0, 12);
                    display.print("BP: ");
                    if (isSwitchOn(SW_BP)) {
                        display.print(bp.systolic);
                        display.print("/");
                        display.print(bp.diastolic);
                        display.print(" mmHg");
                    } else {
                        display.print("OFF");
                    }
                    
                    display.setCursor(0, 24);
                    display.print("BP Status: ");
                    if (isSwitchOn(SW_BP)) {
                        display.print(getBPStatus(bp.systolic, bp.diastolic));
                    } else {
                        display.print("OFF");
                    }
                    
                    // ECG
                    display.setCursor(0, 36);
                    display.print("ECG: ");
                    if (isSwitchOn(SW_ECG)) {
                        display.print(getECGStatusString(ecgStatus));
                    } else {
                        display.print("OFF");
                    }
                    
                    display.setCursor(0, 48);
                    display.print("ECG Rate: ");
                    if (isSwitchOn(SW_ECG)) {
                        display.print(ecgBpm);
                        display.print(" bpm");
                    } else {
                        display.print("OFF");
                    }
                    break;
                    
                case 2:  // Page 3: Status Summary
                    display.setCursor(0, 12);
                    display.print("--- STATUS ---");
                    
                    display.setCursor(0, 24);
                    int alertCount = 0;
                    
                    // Check all alerts
                    if (isSwitchOn(SW_TEMP) && temperature > 38.0) {
                        display.print("! HIGH TEMP");
                        alertCount++;
                    }
                    
                    display.setCursor(0, 36);
                    if (isSwitchOn(SW_HR) && heartRate > 0 && (heartRate < 60 || heartRate > 100)) {
                        display.print("! HR ABNORMAL");
                        alertCount++;
                    } else if (isSwitchOn(SW_BP) && (bp.systolic > 140 || bp.diastolic > 90)) {
                        display.print("! HIGH BP");
                        alertCount++;
                    }
                    
                    display.setCursor(0, 48);
                    if (isSwitchOn(SW_ECG) && ecgStatus != ECG_NORMAL && ecgStatus != ECG_OFF) {
                        display.print("! ECG ALERT");
                        alertCount++;
                    } else if (isSwitchOn(SW_SPO2) && spo2 > 0 && spo2 < 90) {
                        display.print("! LOW SpO2");
                        alertCount++;
                    }
                    
                    if (alertCount == 0) {
                        display.setCursor(0, 24);
                        display.print("All vitals OK");
                    }
                    break;
            }
            
            display.display();
            xSemaphoreGive(displayMutex);
        }
        
        // Page rotation every 4 seconds
        pageTimer++;
        if (pageTimer >= 8) {  // 8 x 500ms = 4 seconds
            currentDisplayPage = (currentDisplayPage + 1) % TOTAL_DISPLAY_PAGES;
            pageTimer = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/**
 * Processing Task - Handles alerts, buzzer, and MQTT
 * Consolidated alert management and cloud communication
 */
void processingTask(void *param) {
    float temperature = 0;
    int heartRate = 0;
    int spo2 = 0;
    BloodPressure_t bp = {0, 0};
    ECGStatus_t ecgStatus = ECG_OFF;
    int ecgBpm = 0;
    const char *statusMsg;
    
    vTaskDelay(pdMS_TO_TICKS(3000));  // Wait for other tasks to initialize
    
    safePrintln("[PROCESS] Processing task started");

    while (1) {
        // Collect all sensor data
        xQueuePeek(hrQueue, &heartRate, portMAX_DELAY);
        xQueuePeek(spo2Queue, &spo2, 0);
        xQueuePeek(bpQueue, &bp, 0);
        xQueuePeek(ecgQueue, &ecgStatus, 0);
        xQueuePeek(ecgBpmQueue, &ecgBpm, 0);

        // Read temperature
        if (isSwitchOn(SW_TEMP)) {
            sensors.requestTemperatures();
            float newTemp = sensors.getTempCByIndex(0);
            if (newTemp != -127.0 && newTemp != 85.0) {
                temperature = newTemp;
            }
        }

        // Determine overall status (priority-based)
        statusMsg = "OK";
        
        // Critical alerts first
        if (isSwitchOn(SW_ECG) && ecgStatus == ECG_ST_ELEVATION) {
            statusMsg = "ECG CRITICAL!";
        }
        else if (isSwitchOn(SW_TEMP) && temperature > 39.0) {
            statusMsg = "HIGH FEVER!";
        }
        else if (isSwitchOn(SW_SPO2) && spo2 > 0 && spo2 < 85) {
            statusMsg = "SpO2 CRITICAL!";
        }
        else if (isSwitchOn(SW_BP) && bp.systolic > 160) {
            statusMsg = "BP CRISIS!";
        }
        // Warning alerts
        else if (isSwitchOn(SW_TEMP) && temperature > 38.0) {
            statusMsg = "High Temp!";
        }
        else if (isSwitchOn(SW_HR) && heartRate > 0 && heartRate < 60) {
            statusMsg = "HR Low!";
        }
        else if (isSwitchOn(SW_HR) && heartRate > 100) {
            statusMsg = "HR High!";
        }
        else if (isSwitchOn(SW_SPO2) && spo2 > 0 && spo2 < 90) {
            statusMsg = "SpO2 Low!";
        }
        else if (isSwitchOn(SW_BP) && (bp.systolic > 140 || bp.diastolic > 90)) {
            statusMsg = "BP High!";
        }
        else if (isSwitchOn(SW_BP) && (bp.systolic < 90 || bp.diastolic < 60)) {
            statusMsg = "BP Low!";
        }
        else if (isSwitchOn(SW_ECG) && ecgStatus == ECG_ARRHYTHMIA) {
            statusMsg = "Arrhythmia!";
        }
        else if (isSwitchOn(SW_ECG) && ecgStatus == ECG_BRADYCARDIA) {
            statusMsg = "ECG Brady!";
        }
        else if (isSwitchOn(SW_ECG) && ecgStatus == ECG_TACHYCARDIA) {
            statusMsg = "ECG Tachy!";
        }
        else if (isSwitchOn(SW_MOTION) && digitalRead(MOTION_DETECTOR) == HIGH) {
            statusMsg = "Motion!";
        }

        // Debug output
        if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            Serial.println("\n========== PROCESSING ==========");
            Serial.printf("Temp: %.1f C | HR: %d | SpO2: %d%%\n", temperature, heartRate, spo2);
            Serial.printf("BP: %d/%d | ECG: %s (%d bpm)\n", bp.systolic, bp.diastolic, 
                         getECGStatusString(ecgStatus), ecgBpm);
            Serial.printf("Status: %s\n", statusMsg);
            Serial.println("================================\n");
            xSemaphoreGive(serialMutex);
        }

        // ============== BUZZER ALERTS ==============
        
        // Temperature alert
        if (isSwitchOn(SW_TEMP) && temperature > 38) {
            digitalWrite(BUZZER_PIN_1, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN_1, LOW);
        }

        // Heart Rate alert
        if (isSwitchOn(SW_HR) && heartRate > 0 && (heartRate < 60 || heartRate > 100)) {
            digitalWrite(BUZZER_PIN_2, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN_2, LOW);
        }

        // SpO2 alert
        if (isSwitchOn(SW_SPO2) && spo2 > 0 && spo2 < 90) {
            digitalWrite(BUZZER_PIN_3, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN_3, LOW);
        }

        // Motion alert
        if (isSwitchOn(SW_MOTION) && digitalRead(MOTION_DETECTOR) == HIGH) {
            digitalWrite(BUZZER_PIN_4, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(BUZZER_PIN_4, LOW);
        }

        // NEW: Blood Pressure alert
        if (isSwitchOn(SW_BP) && (bp.systolic > 140 || bp.diastolic > 90 || 
                                   bp.systolic < 90 || bp.diastolic < 60)) {
            digitalWrite(BUZZER_PIN_5, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(BUZZER_PIN_5, LOW);
        }

        // NEW: ECG alert
        if (isSwitchOn(SW_ECG) && ecgStatus != ECG_NORMAL && ecgStatus != ECG_OFF) {
            // Double beep for critical ECG alerts
            if (ecgStatus == ECG_ST_ELEVATION) {
                digitalWrite(BUZZER_PIN_6, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                digitalWrite(BUZZER_PIN_6, LOW);
                vTaskDelay(pdMS_TO_TICKS(50));
                digitalWrite(BUZZER_PIN_6, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                digitalWrite(BUZZER_PIN_6, LOW);
            } else {
                digitalWrite(BUZZER_PIN_6, HIGH);
                vTaskDelay(pdMS_TO_TICKS(100));
                digitalWrite(BUZZER_PIN_6, LOW);
            }
        }

        // ============== MQTT PUBLISHING ==============
        
        if (WiFi.status() == WL_CONNECTED) {
            if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                if (!mqtt.connected()) {
                    safePrintln("[MQTT] Connecting...");
                    if (mqtt.connect() == 0) {
                        safePrintln("[MQTT] Connected!");
                    }
                }

                if (mqtt.connected()) {
                    mqtt.processPackets(10);
                    mqtt.ping();

                    // Publish original sensors
                    if (isSwitchOn(SW_TEMP)) tempFeed.publish(temperature);
                    if (isSwitchOn(SW_HR)) hrFeed.publish((int32_t)heartRate);
                    if (isSwitchOn(SW_SPO2)) spo2Feed.publish((int32_t)spo2);
                    if (isSwitchOn(SW_MOTION)) {
                        motiondetectorFeed.publish((int32_t)digitalRead(MOTION_DETECTOR));
                    }
                    
                    // NEW: Publish Blood Pressure
                    if (isSwitchOn(SW_BP)) {
                        bpSysFeed.publish((int32_t)bp.systolic);
                        bpDiaFeed.publish((int32_t)bp.diastolic);
                    }
                    
                    // NEW: Publish ECG Status
                    if (isSwitchOn(SW_ECG)) {
                        ecgFeed.publish((int32_t)ecgStatus);
                    }
                    
                    // Publish overall status
                    statusFeed.publish(statusMsg);
                }
                xSemaphoreGive(mqttMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

/**
 * System Monitor Task
 * Monitors task health and system resources
 */
void systemMonitorTask(void *param) {
    while (1) {
        if (xSemaphoreTake(serialMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Serial.println("\n----- SYSTEM STATUS -----");
            Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
            Serial.printf("WiFi Status: %s\n", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
            Serial.printf("MQTT Status: %s\n", mqtt.connected() ? "Connected" : "Disconnected");
            Serial.printf("Active Tasks: 6\n");
            Serial.println("------------------------\n");
            xSemaphoreGive(serialMutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(10000));  // Every 10 seconds
    }
}

// ============== SETUP ==============

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n========================================");
    Serial.println("  MULTI-SENSOR HEALTH MONITOR v2.0");
    Serial.println("  With Blood Pressure & ECG Monitoring");
    Serial.println("========================================\n");

    // Initialize all buzzer pins
    pinMode(BUZZER_PIN_1, OUTPUT);
    pinMode(BUZZER_PIN_2, OUTPUT);
    pinMode(BUZZER_PIN_3, OUTPUT);
    pinMode(BUZZER_PIN_4, OUTPUT);
    pinMode(BUZZER_PIN_5, OUTPUT);  // NEW: BP buzzer
    pinMode(BUZZER_PIN_6, OUTPUT);  // NEW: ECG buzzer

    // Initialize sensor pins
    pinMode(MOTION_DETECTOR, INPUT);

    // Initialize all switch pins with pull-down
    pinMode(SW_TEMP, INPUT_PULLDOWN);
    pinMode(SW_HR, INPUT_PULLDOWN);
    pinMode(SW_SPO2, INPUT_PULLDOWN);
    pinMode(SW_MOTION, INPUT_PULLDOWN);
    pinMode(SW_BP, INPUT_PULLDOWN);   // NEW: BP switch
    pinMode(SW_ECG, INPUT_PULLDOWN);  // NEW: ECG switch

    // Initialize temperature sensor
    sensors.begin();
    Serial.println("[INIT] Temperature sensor initialized");

    // Initialize I2C and OLED
    Wire.begin(21, 22);
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("[ERROR] OLED Failed!");
        while(1);
    }
    Serial.println("[INIT] OLED initialized");

    // Startup display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Multi-Sensor v2.0");
    display.println("Initializing...");
    display.println("");
    display.println("New Sensors:");
    display.println("- Blood Pressure");
    display.println("- ECG Monitor");
    display.display();

    // Connect WiFi
    Serial.print("[WIFI] Connecting");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
        delay(500);
        Serial.print(".");
        timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WIFI] Connected!");
        Serial.print("[WIFI] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WIFI] Connection failed - continuing offline");
    }

    delay(2000);

    // Create synchronization primitives
    displayMutex = xSemaphoreCreateMutex();
    mqttMutex = xSemaphoreCreateMutex();
    serialMutex = xSemaphoreCreateMutex();
    
    Serial.println("[INIT] Mutexes created");

    // Create queues for all sensors
    hrQueue = xQueueCreate(1, sizeof(int));
    spo2Queue = xQueueCreate(1, sizeof(int));
    bpQueue = xQueueCreate(1, sizeof(BloodPressure_t));    // NEW
    ecgQueue = xQueueCreate(1, sizeof(ECGStatus_t));       // NEW
    ecgBpmQueue = xQueueCreate(1, sizeof(int));            // NEW

    // Initialize queues with default values
    int initInt = 0;
    BloodPressure_t initBP = {0, 0};
    ECGStatus_t initECG = ECG_OFF;
    
    xQueueOverwrite(hrQueue, &initInt);
    xQueueOverwrite(spo2Queue, &initInt);
    xQueueOverwrite(bpQueue, &initBP);
    xQueueOverwrite(ecgQueue, &initECG);
    xQueueOverwrite(ecgBpmQueue, &initInt);
    
    Serial.println("[INIT] Queues created and initialized");

    // Create all FreeRTOS tasks
    // Original tasks
    xTaskCreatePinnedToCore(sensorTask, "HR_SpO2_Task", 4096, NULL, 2, &sensorTaskHandle, 1);
    xTaskCreatePinnedToCore(processingTask, "ProcessTask", 8192, NULL, 3, &processingTaskHandle, 0);
    
    // NEW: Blood Pressure task on Core 1
    xTaskCreatePinnedToCore(bpSensorTask, "BP_Task", 4096, NULL, 2, &bpTaskHandle, 1);
    
    // NEW: ECG task on Core 1
    xTaskCreatePinnedToCore(ecgSensorTask, "ECG_Task", 4096, NULL, 2, &ecgTaskHandle, 1);
    
    // NEW: Display task on Core 0
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 4096, NULL, 1, &displayTaskHandle, 0);
    
    // System monitor task
    xTaskCreatePinnedToCore(systemMonitorTask, "SysMonitor", 2048, NULL, 1, NULL, 0);

    Serial.println("\n[INIT] All tasks created successfully!");
    Serial.println("========================================");
    Serial.println("Task Distribution:");
    Serial.println("  Core 0: Processing, Display, SysMonitor");
    Serial.println("  Core 1: HR/SpO2, BP, ECG sensors");
    Serial.println("========================================\n");
}

void loop() {
    // Main loop is empty - all work done by FreeRTOS tasks
    vTaskDelay(portMAX_DELAY);
}