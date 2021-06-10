#include "auxiliary.h"

#define MIC_PIN 0
#define BATTERY_PIN 36
#define BATTERY_CTRL_PIN 12
#define SERVO_PIN 25

#define PWM_RESOLUTION 8
#define PWM_CHANNEL 0
#define PWM_FREQ 50

// Clap detection times
bool lastDetectionValue;
uint32_t lastDetectionTime = 0;
uint32_t currentDetectionTime = 0;
uint32_t lastClapTime = 0;

// Flag to allow first single clap
bool wakeupSingleClap = false;

// RTC settings
RTC_DATA_ATTR uint16_t singleClapTime = 0;
RTC_DATA_ATTR uint16_t noiseDebounce  = 0;
RTC_DATA_ATTR uint16_t clapTimeout    = 0;
RTC_DATA_ATTR uint16_t clapLength     = 0;
RTC_DATA_ATTR uint8_t  logCounter     = 0;
RTC_DATA_ATTR uint16_t activeTime     = 0;
RTC_DATA_ATTR uint64_t sleepTime      = 0;
RTC_DATA_ATTR bool     lastLeft       = true;

// Deep-sleep cycle
uint32_t activeTimer = 0;
uint8_t clapCounter = 0;

void trigger() {
    uint8_t dutyFrom = 7;
    uint8_t dutyTo = 32;
    for (uint8_t dc = dutyFrom; dc <= dutyTo; dc++) {
        ledcWrite(PWM_CHANNEL, dc);
        delay(5);
    }
    for (uint8_t dc = dutyTo; dc >= dutyFrom; dc--) {
        ledcWrite(PWM_CHANNEL, dc);
        delay(5);
    }
    activeTimer = millis();
    clapCounter++;
    Serial.printf("clapCounter = %d\n", clapCounter);
}

uint16_t getBatteryLevel() {
    digitalWrite(BATTERY_CTRL_PIN, HIGH);
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 5; i++) {
        uint16_t analog = analogRead(BATTERY_PIN);
        uint16_t voltage = float(analog) / 4096.f * 3900;
        sum += voltage;
        delay(100);
    }
    float mean = float(sum) / 5;
    digitalWrite(BATTERY_CTRL_PIN, LOW);
    return mean * 6.15;
}

bool detectClaps() {
    bool doubleClapped = false;
    bool val = digitalRead(MIC_PIN);
    currentDetectionTime = millis();

    if (val == LOW) {
        if (currentDetectionTime    - lastDetectionTime > noiseDebounce  // "Debouncing" long noise as one noise
            && currentDetectionTime - lastDetectionTime < clapLength     // if current clap is less than n milliseconds after the first clap
            && currentDetectionTime - lastClapTime      > clapTimeout    // to avoid taking a third clap as part of a pattern
            && lastDetectionValue == HIGH  // if it was silent before
        ) {
            lastClapTime = currentDetectionTime;
            doubleClapped = true;
        }
        lastDetectionTime = currentDetectionTime;
    }
    lastDetectionValue = val;
    return doubleClapped;
}

void pushData(String node, int value) {
    Firebase.getInt(fbdo, node + "/last_id");
    int id_int;
    if (fbdo.dataType() == "int")
        id_int = fbdo.intData() + 1;
    else 
        id_int = 1;
    String id = String(id_int);
    Firebase.setTimestamp(fbdo, node + "/" + id + "/timestamp");
    Firebase.setIntAsync(fbdo, node + "/" + id + "/value", value);
    Firebase.setIntAsync(fbdo, node + "/last_id", id_int);
    delay(100);
}

void updateSettings() {
    Firebase.getInt(fbdo, "/settings/sleepTime");
    if (fbdo.dataType() == "int") {
        sleepTime = uint64_t(fbdo.intData()) * 1000 * 1000;
        Serial.printf("sleepTime = %d\n", sleepTime);
        }
    Firebase.getInt(fbdo, "/settings/activeTime");
    if (fbdo.dataType() == "int") {
        activeTime = fbdo.intData() * 1000;
        Serial.printf("activeTime = %d\n", activeTime);
    }
    Firebase.getInt(fbdo, "/settings/singleClapTime");
    if (fbdo.dataType() == "int") {
        singleClapTime = fbdo.intData();
        Serial.printf("singleClapTime = %d\n", singleClapTime);
    }
    Firebase.getInt(fbdo, "/settings/noiseDebounce");
    if (fbdo.dataType() == "int") {
        noiseDebounce = fbdo.intData();
        Serial.printf("noiseDebounce = %d\n", noiseDebounce);
    }
    Firebase.getInt(fbdo, "/settings/clapLength");
    if (fbdo.dataType() == "int") {
        clapLength = fbdo.intData();
        Serial.printf("clapLength = %d\n", clapLength);
    }
    Firebase.getInt(fbdo, "/settings/clapTimeout");
    if (fbdo.dataType() == "int") {
        clapTimeout = fbdo.intData();
        Serial.printf("clapTimeout = %d\n", clapTimeout);
    }
    delay(100);
}

void setup() {
    // Voltage check circuit
    pinMode(BATTERY_CTRL_PIN, OUTPUT);
    digitalWrite(BATTERY_CTRL_PIN, LOW);
    // Servo intialization
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
    ledcAttachPin(SERVO_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0);
    // Serial
    Serial.begin(115200);
    delay(10);
    Serial.println("I woke up!");
    print_wakeup_reason();
    // Increment counter for battery logging
    logCounter++;
    // If any of settings is 0, update settings
    if (singleClapTime == 0 ||
         noiseDebounce == 0 ||
           clapTimeout == 0 ||
            clapLength == 0 ||
            activeTime == 0 ||
             sleepTime == 0) {
        if (setupWifi()) {
            delay(250);
            Serial.println("Updating after first boot");
            updateSettings();
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
        } else {
            singleClapTime = 500;
            noiseDebounce  = 100;
            clapTimeout    = 1000;
            clapLength     = 500;
            activeTime     = 10000;
            sleepTime      = 300000;
        }
    }
    // If woke up by sound sensor GPIO, turn on single clap flag
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
        wakeupSingleClap = true;
        Serial.println("Setting single clap flag");
    }
    // If woke up by timer, trigger motor
    else if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        trigger();
    }
}

void loop() {
    // Allow for a single clap after wake up once in singleClapTime period
    if (wakeupSingleClap) {
        // If clapped
        if (digitalRead(MIC_PIN) == LOW) {
            trigger();
            wakeupSingleClap = false;
        } else if (millis() - activeTimer >= singleClapTime)
            wakeupSingleClap = false;
    }
    // Main loop logic
    // if activeTime has passed, turn on wifi and send data
    if (millis() - activeTimer >= activeTime) {
        // Push collected data if clapped or if it's 3rd deep sleep cycle without pushing
        if (clapCounter != 0 || logCounter >= 3) {
            // Data pipeline
            if (setupWifi()) {
                setupFirebase();
                pushData("/data/battery", getBatteryLevel());
                if (clapCounter != 0)
                    pushData("/data/trigger", clapCounter);
                if (logCounter >= 3) {
                    updateSettings();
                    logCounter = 0;
                }
            }
        }
        // Go to sleep
        Serial.println("Sleeping...");
        enterDeepSleep(sleepTime);
    // Else, detect claps
    } else if (detectClaps()) {
        trigger();
    }
}
