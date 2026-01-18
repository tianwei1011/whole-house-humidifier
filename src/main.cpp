#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans9pt7b.h>  // Custom font ~1.5x size
#include <DHT20.h>            // For DHT20 (robtillaart/DHT20)


// Pin definitions
#define I2C_SDA 21
#define I2C_SCL 22
#define DHTPIN 23  // unused by DHT20 (kept for compatibility)
#define WATER_LEVEL_PIN 35
#define PUMP_PWM_PIN 25
#define VALVE_PIN 26
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

// PWM configuration for motor control
#define PWM_FREQ 1000      // 1 kHz (suitable for motor control)
#define PWM_CHANNEL 0
#define PWM_RESOLUTION 8   // 8-bit resolution (0-255)
#define PWM_DUTY_85 217    // 85% of 255

// Water level detection
#define DEBOUNCE_COUNT 10  // Number of consecutive reads needed to change state// Add calibration offsets at the top of your file
#define HUMIDITY_OFFSET -10.0  // Adjust based on comparison with reference
#define HUMIDITY_PRESET 50.0  // Preset value for humidity


// Sensor objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT20 dht;

// Shared variables (protected by mutex if needed)
float temperature = 0.0;
float humidity = 0.0;
bool waterEmpty = false;
int lowVoltageCount = 0;
int highVoltageCount = 0;
bool valveActive = false;
bool pumpActive = false;
int countdown = 0;
bool valveHasRun = false;  // Track if valve has already run for this empty cycle

// Pump cycle state
enum PumpState { PUMP_IDLE, PUMP_RUNNING, PUMP_WAITING };
PumpState pumpState = PUMP_IDLE;


// Sensor reading task
void sensor_task(void *pvParameters) {
  while (1) {
    // Read DHT20
    int status = dht.read();
    if (status == DHT20_OK) {
      float temp = dht.getTemperature();
      float hum = dht.getHumidity();
      Serial.printf("Raw DHT20 - Temp: %.2f°C, Humidity: %.2f%%\n", temp, hum);
      
      if (!isnan(temp) && !isnan(hum)) {
        temperature = temp;
        humidity = hum + HUMIDITY_OFFSET;
      }
    } else {
      Serial.printf("DHT20 read error: %d\n", status);
    }


    vTaskDelay(2000 / portTICK_PERIOD_MS); // Read every 2 seconds (DHT20 needs >1000ms between reads)
  }
}

// Water level monitoring task
void water_level_task(void *pvParameters) {
  while (1) {
    int voltage = digitalRead(WATER_LEVEL_PIN);
    
    if (voltage == HIGH) {
      // High voltage detected - water empty
      lowVoltageCount++;
      highVoltageCount = 0;  // Reset high voltage counter
      
      if (lowVoltageCount >= DEBOUNCE_COUNT && !waterEmpty) {
        waterEmpty = true;
        Serial.println("WATER EMPTY detected!");
      }
    } else {
      // Low voltage detected - water OK
      highVoltageCount++;
      lowVoltageCount = 0;  // Reset low voltage counter
      
      if (highVoltageCount >= DEBOUNCE_COUNT && waterEmpty) {
        waterEmpty = false;
        Serial.println("Water level OK");
      }
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Check every 1 second
  }
}

// Valve and pump control task
void control_task(void *pvParameters) {
  while (1) {
    // Priority 1: If humidity >= preset, stop everything
    if (humidity >= HUMIDITY_PRESET) {
      if (valveActive) {
        digitalWrite(VALVE_PIN, LOW);
        valveActive = false;
        countdown = 0;
        Serial.println("Valve stopped - humidity reached preset");
      }
      if (pumpActive) {
        ledcWrite(PWM_CHANNEL, 0);
        pumpActive = false;
        countdown = 0;
        pumpState = PUMP_IDLE;
        Serial.println("Pump stopped - humidity reached preset");
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    
    // Priority 2: Valve is active - let it complete regardless of waterEmpty status
    if (valveActive) {
      // Stop pump if running
      if (pumpActive) {
        ledcWrite(PWM_CHANNEL, 0);
        pumpActive = false;
        pumpState = PUMP_IDLE;
        Serial.println("Pump stopped - valve active");
      }
      
      // Continue valve countdown
      if (countdown > 0) {
        countdown--;
      } else {
        digitalWrite(VALVE_PIN, LOW);
        valveActive = false;
        valveHasRun = true;
        Serial.println("Valve stopped after countdown complete");
      }
      
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;  // Skip all other logic while valve is active
    }
    
    // Priority 3: Water is empty and valve not active - start valve
    if (waterEmpty && !valveHasRun) {
      // Stop pump immediately if running
      if (pumpActive) {
        ledcWrite(PWM_CHANNEL, 0);
        pumpActive = false;
        countdown = 0;
        pumpState = PUMP_IDLE;
        Serial.println("Pump stopped - water empty");
      }
      
      // Start valve
      digitalWrite(VALVE_PIN, HIGH);
      valveActive = true;
      countdown = 180;
      Serial.println("Valve started - filling water for 180s");
      
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    
    // Priority 4: Water is OK - reset valve flag and run pump cycles
    if (!waterEmpty) {
      valveHasRun = false;  // Reset flag when water is OK
    }
    
    // Pump state machine - only runs when water is OK, humidity < preset, and valve is not active
    if (!waterEmpty && !valveActive) {
      switch (pumpState) {
        case PUMP_IDLE:
          // Start pump cycle
          ledcWrite(PWM_CHANNEL, PWM_DUTY_85);
          pumpActive = true;
          countdown = 60;
          pumpState = PUMP_RUNNING;
          Serial.println("Pump started for 60s at 85%");
          break;
          
        case PUMP_RUNNING:
          if (countdown > 0) {
            countdown--;
          } else {
            // Pump cycle complete, stop pump
            ledcWrite(PWM_CHANNEL, 0);
            pumpActive = false;
            countdown = 60;
            pumpState = PUMP_WAITING;
            Serial.println("Pump stopped, waiting 60s");
          }
          break;
          
        case PUMP_WAITING:
          if (countdown > 0) {
            countdown--;
          } else {
            // Wait complete, restart cycle
            pumpState = PUMP_IDLE;
          }
          break;
      }
    } else {
      // Water empty or valve active - stop pump if running
      if (pumpActive) {
        ledcWrite(PWM_CHANNEL, 0);
        pumpActive = false;
        pumpState = PUMP_IDLE;
      }
    }
    
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// Display update task
void display_task(void *pvParameters) {
  int scrollOffset = 0;
  const int scrollSpeed = 2; // Pixels per update
  const int maxScroll = 40;  // Maximum scroll distance
  
  while (1) {
    display.clearDisplay();
    display.setTextSize(1);  // Use 1x size to fit 5 lines
    display.setTextColor(SSD1306_WHITE);
    
    // Calculate scroll position (oscillate back and forth)
    int xPos = scrollOffset % (maxScroll * 2);
    if (xPos > maxScroll) {
      xPos = maxScroll * 2 - xPos; // Reverse direction
    }
    
    // Display 5 lines with horizontal scrolling effect
    display.setCursor(xPos, 0);
    display.printf("TEMP: %.1fC", temperature);
    display.setCursor(xPos, 13);
    display.printf("HUMI: %.1f%%", humidity);
    display.setCursor(xPos, 26);
    display.printf("PRESET: %.1f%%", HUMIDITY_PRESET);
    display.setCursor(xPos, 39);
    display.printf("WATER: %s", waterEmpty ? "EMPTY" : "OK");
    display.setCursor(xPos, 52);
    if (humidity >= HUMIDITY_PRESET) {
      display.printf("TARGET REACHED");
    } else if (valveActive) {
      display.printf("VALVE: ON %ds", countdown);
    } else if (pumpActive) {
      display.printf("PUMP: ON %ds", countdown);
    } else if (!waterEmpty) {
      display.printf("WAIT: %ds", countdown);
    } else {
      display.printf("STANDBY");
    }
    display.display();

    scrollOffset += scrollSpeed;
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Update every 1 second
  }
}

void scanI2C() {
  Serial.println("\nScanning I2C bus...");
  byte count = 0;
  for (byte i = 1; i < 127; i++) {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0) {
      Serial.printf("Found device at 0x%02X\n", i);
      count++;
    }
  }
  Serial.printf("Found %d device(s)\n\n", count);
}

void setup() {
  // Initialize Serial for debugging
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nStarting...");

  // Initialize I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Scan I2C bus first
  scanI2C();

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("SSD1306 init failed");
    for (;;);
  }
  display.clearDisplay();
  display.ssd1306_command(0x81);  // Set contrast control
  display.ssd1306_command(50);   // Contrast value ~50% (default 207, range 0-255)
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // Initialize DHT20
  Serial.println("Initializing DHT20 at 0x38...");
  dht.begin();
  delay(100);

  // Initialize water level sensor pin
  pinMode(WATER_LEVEL_PIN, INPUT);
  Serial.printf("Water level sensor initialized on GPIO%d\n", WATER_LEVEL_PIN);

  // Initialize valve pin
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(VALVE_PIN, LOW);
  Serial.printf("Valve initialized on GPIO%d\n", VALVE_PIN);

  // Initialize PWM for pump on GPIO25 (stopped initially)
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(PUMP_PWM_PIN, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 0);  // Start with pump off
  Serial.printf("Pump PWM initialized on GPIO%d (stopped)\n", PUMP_PWM_PIN);

  // Create FreeRTOS tasks
  xTaskCreatePinnedToCore(sensor_task, "SensorTask", 4096, NULL, 5, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(water_level_task, "WaterLevelTask", 4096, NULL, 5, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(control_task, "ControlTask", 4096, NULL, 5, NULL, 0); // Core 0
  xTaskCreatePinnedToCore(display_task, "DisplayTask", 4096, NULL, 5, NULL, 1); // Core 1
}

void loop() {
  // The loop function can remain empty because tasks are running in FreeRTOS
}