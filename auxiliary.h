#include <WiFi.h>
#include <FirebaseESP32.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "secrets.h"

// Firebase stuff
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, PASSWORD);
    Serial.println("Connecting to wifi...");
    for (uint8_t i = 0; i < 7 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        Serial.print('.');
    }
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) {
      Serial.printf("\nConnected to %s\nIP address:\thttp://", WiFi.SSID().c_str());
      Serial.println(WiFi.localIP());
    } else {
      Serial.printf("Couldn't connect to %s\n", WIFI_SSID);
    }
    return connected;
}

void setupFirebase() {
    config.database_url = DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;
    config.signer.tokens.legacy_token = DATABASE_SECRET;

    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    fbdo.setResponseSize(1024);
    Firebase.setReadTimeout(fbdo, 1000 * 60);
    Firebase.setwriteSizeLimit(fbdo, "tiny");
    Firebase.setFloatDigits(2);
    Firebase.setDoubleDigits(6);
}

void enterDeepSleep(uint32_t sleepTime) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(250);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, LOW);
    esp_sleep_enable_timer_wakeup(sleepTime);
    esp_deep_sleep_start();
}

void print_wakeup_reason(){
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
      case ESP_SLEEP_WAKEUP_EXT0  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
      case ESP_SLEEP_WAKEUP_EXT1  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
      case ESP_SLEEP_WAKEUP_TIMER  : Serial.println("Wakeup caused by timer"); break;
      case ESP_SLEEP_WAKEUP_TOUCHPAD  : Serial.println("Wakeup caused by touchpad"); break;
      case ESP_SLEEP_WAKEUP_ULP  : Serial.println("Wakeup caused by ULP program"); break;
      default : Serial.println("Wakeup was not caused by deep sleep"); break;
    }
}
