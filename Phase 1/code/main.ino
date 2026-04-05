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

#define TEMP_SENSOR 14
#define MOTION_DETECTOR 12

#define BUZZER_PIN_1 4   
#define BUZZER_PIN_2 16    
#define BUZZER_PIN_3 17
#define BUZZER_PIN_4 5

#define SW_TEMP 32
#define SW_HR 33
#define SW_SPO2 25
#define SW_MOTION 26

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define OLED_ADDRESS 0x3C

#define WIFI_SSID "Wokwi-GUEST"
#define WIFI_PASS ""

#define IO_USERNAME  "adafruit username"
#define IO_KEY       "adafruit api"

#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883

#define TEMP_FEED         IO_USERNAME "/feeds/body-temperature-c"
#define HR_FEED           IO_USERNAME "/feeds/heart-rate-monitor"
#define MOTION_FEED       IO_USERNAME "/feeds/motion-detector"
#define SPO2_FEED         IO_USERNAME "/feeds/spo2-percent"
#define FLAG              IO_USERNAME "/feeds/flag"

WiFiClient client;

Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT,
                          IO_USERNAME, IO_KEY);

Adafruit_MQTT_Publish tempFeed   = Adafruit_MQTT_Publish(&mqtt, TEMP_FEED);
Adafruit_MQTT_Publish hrFeed     = Adafruit_MQTT_Publish(&mqtt, HR_FEED);
Adafruit_MQTT_Publish spo2Feed   = Adafruit_MQTT_Publish(&mqtt, SPO2_FEED);
Adafruit_MQTT_Publish statusFeed = Adafruit_MQTT_Publish(&mqtt, FLAG);
Adafruit_MQTT_Publish motiondetectorFeed = Adafruit_MQTT_Publish(&mqtt, MOTION_FEED);

OneWire oneWire(TEMP_SENSOR);
DallasTemperature sensors(&oneWire);

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

QueueHandle_t hrQueue;
QueueHandle_t spo2Queue;

// Function to check if switch is ON
// DIP switch: HIGH = ON (when switch is flipped to ON position)
bool isSwitchOn(int pin) {
  return digitalRead(pin) == HIGH;
}

void sensorTask(void *param) {
  int heartRate = 0;
  int spo2 = 0;

  while (1) {
    
    if(isSwitchOn(SW_HR)) {
      heartRate = random(60, 100);  // Normal range
    } else {
      heartRate = 0;
    }

    if(isSwitchOn(SW_SPO2)) {
      spo2 = random(90, 100);  // Normal range
    } else {
      spo2 = 0;
    }

    Serial.print("HR Switch: "); Serial.print(digitalRead(SW_HR));
    Serial.print(" | SPO2 Switch: "); Serial.println(digitalRead(SW_SPO2));
    Serial.print("HR: "); Serial.print(heartRate);
    Serial.print(" | SPO2: "); Serial.println(spo2);

    xQueueOverwrite(hrQueue, &heartRate);
    xQueueOverwrite(spo2Queue, &spo2);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void processingTask(void *param) {
  float temperature = 0;
  int heartRate = 0;
  int spo2 = 0;
  const char *statusMsg;

  vTaskDelay(pdMS_TO_TICKS(2000));  // Wait for sensor task to start

  while (1) {
    
    xQueuePeek(hrQueue, &heartRate, portMAX_DELAY);
    xQueuePeek(spo2Queue, &spo2, portMAX_DELAY);

    // Read temperature
    if(isSwitchOn(SW_TEMP)) {
      sensors.requestTemperatures();
      float newTemp = sensors.getTempCByIndex(0);
      if(newTemp != -127.0 && newTemp != 85.0) {
        temperature = newTemp;
      }
    }

    // Debug prints
    Serial.println("=== Processing ===");
    Serial.print("TEMP Switch: "); Serial.println(digitalRead(SW_TEMP));
    Serial.print("Temperature: "); Serial.println(temperature);
    Serial.print("Heart Rate: "); Serial.println(heartRate);
    Serial.print("SpO2: "); Serial.println(spo2);

    // Determine status
    statusMsg = "OK";

    if (isSwitchOn(SW_TEMP) && temperature > 38.0) {
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
    else if (isSwitchOn(SW_MOTION) && digitalRead(MOTION_DETECTOR) == HIGH) {
      statusMsg = "Motion!";
    }

    // Update OLED Display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    // Temperature
    display.setCursor(0, 0);
    display.print("Temp: ");
    if(isSwitchOn(SW_TEMP)) {
      display.print(temperature, 1);
      display.print(" C");
    } else {
      display.print("OFF");
    }

    // Heart Rate
    display.setCursor(0, 16);
    display.print("HR: ");
    if(isSwitchOn(SW_HR)) {
      display.print(heartRate);
      display.print(" bpm");
    } else {
      display.print("OFF");
    }

    // SpO2
    display.setCursor(0, 32);
    display.print("SpO2: ");
    if(isSwitchOn(SW_SPO2)) {
      display.print(spo2);
      display.print(" %");
    } else {
      display.print("OFF");
    }

    // Status
    display.setCursor(0, 48);
    display.print("Sts: ");
    display.print(statusMsg);

    display.display();

    // Buzzer alerts
    if(isSwitchOn(SW_TEMP) && temperature > 38) {
      digitalWrite(BUZZER_PIN_1, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
      digitalWrite(BUZZER_PIN_1, LOW);
    }

    if(isSwitchOn(SW_HR) && heartRate > 0 && (heartRate < 60 || heartRate > 100)) {
      digitalWrite(BUZZER_PIN_2, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
      digitalWrite(BUZZER_PIN_2, LOW);
    }

    if(isSwitchOn(SW_SPO2) && spo2 > 0 && spo2 < 90) {
      digitalWrite(BUZZER_PIN_3, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
      digitalWrite(BUZZER_PIN_3, LOW);
    }

    if(isSwitchOn(SW_MOTION) && digitalRead(MOTION_DETECTOR) == HIGH) {
      digitalWrite(BUZZER_PIN_4, HIGH);
      vTaskDelay(pdMS_TO_TICKS(100));
      digitalWrite(BUZZER_PIN_4, LOW);
    }

    // MQTT
    if(WiFi.status() == WL_CONNECTED) {
      if (!mqtt.connected()) {
        Serial.println("Connecting to MQTT...");
        if(mqtt.connect() == 0) {
          Serial.println("MQTT Connected!");
        }
      }

      if(mqtt.connected()) {
        mqtt.processPackets(10);
        mqtt.ping();

        if(isSwitchOn(SW_TEMP)) tempFeed.publish(temperature);
        if(isSwitchOn(SW_HR)) hrFeed.publish((int32_t)heartRate);
        if(isSwitchOn(SW_SPO2)) spo2Feed.publish((int32_t)spo2);
        if(isSwitchOn(SW_MOTION)) motiondetectorFeed.publish((int32_t)digitalRead(MOTION_DETECTOR));
        statusFeed.publish(statusMsg);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== Health Monitor Starting ===");

  // Buzzer pins
  pinMode(BUZZER_PIN_1, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);
  pinMode(BUZZER_PIN_3, OUTPUT);
  pinMode(BUZZER_PIN_4, OUTPUT);

  // Sensor pins
  pinMode(MOTION_DETECTOR, INPUT);

  // Switch pins - use INPUT_PULLDOWN for DIP switch
  pinMode(SW_TEMP, INPUT_PULLDOWN);
  pinMode(SW_HR, INPUT_PULLDOWN);
  pinMode(SW_SPO2, INPUT_PULLDOWN);
  pinMode(SW_MOTION, INPUT_PULLDOWN);

  // Initialize temperature sensor
  sensors.begin();

  // Initialize I2C and OLED
  Wire.begin(21, 22);
  
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED Failed!");
    while(1);
  }
  Serial.println("OLED OK!");

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Starting...");
  display.display();

  // Connect WiFi
  Serial.print("WiFi connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int timeout = 0;
  while (WiFi.status() != WL_CONNECTED && timeout < 20) {
    delay(500);
    Serial.print(".");
    timeout++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK!");
    display.println("WiFi OK!");
  } else {
    Serial.println("\nWiFi Failed!");
    display.println("WiFi Failed!");
  }
  display.display();
  delay(1000);

  // Create queues
  hrQueue = xQueueCreate(1, sizeof(int));
  spo2Queue = xQueueCreate(1, sizeof(int));

  // Initialize queue with default values
  int initVal = 0;
  xQueueOverwrite(hrQueue, &initVal);
  xQueueOverwrite(spo2Queue, &initVal);

  // Create tasks
  xTaskCreatePinnedToCore(sensorTask, "SensorTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(processingTask, "ProcessTask", 8192, NULL, 2, NULL, 0);

  Serial.println("Tasks started!");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
