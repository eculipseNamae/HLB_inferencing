#include <Arduino.h>
#include "Config.h"
#include "DisplayManager.h"
#include "SDLogger.h"
#include "CameraManager.h"
#include "InferenceManager.h"
#include "WebServerManager.h"

// State variables for main loop
static unsigned long last_inference  = 0;
static bool wifi_mode_active = false;
static unsigned long touch_start = 0;
static bool touch_pressed = false;
static unsigned long last_touch_debug = 0;

void setup() {
    Serial.begin(115200);
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time < 5000)) { delay(10); }

    // Init display
    if (!DisplayManager::begin()) {
        Serial.println(F("SSD1306 allocation failed"));
    } else {
        DisplayManager::showInitStatus();
    }

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    if (!psramFound()) { 
        Serial.println("FATAL: PSRAM NOT FOUND"); 
        while (1); 
    }

    // Init SD logging
    if (SDLogger::begin()) {
        SDLogger::initSession();
        char msg[16];
        snprintf(msg, sizeof(msg), "%lu", SDLogger::getSessionId());
        DisplayManager::showMessage("SESSION: ", msg, nullptr, false);
    } else {
        Serial.println("WARN: SD not found — logging disabled");
    }

    // Init camera
    if (CameraManager::begin()) {
        DisplayManager::showMessage(nullptr, "CAMERA READY", nullptr, false);
    } else {
        Serial.println("FATAL: Camera init failed");
    }

    Serial.println("Hold touch 2s to toggle WiFi dashboard");
    Serial.printf("Ready. Session %lu | CSV: %s\r\n", SDLogger::getSessionId(), SDLogger::getCsvPath());
}

void loop() {
    // 1. Check touch to toggle WiFi mode
    WebServerManager::handleTouch(wifi_mode_active, touch_start, touch_pressed, last_touch_debug);

    // 2. Handle WiFi Server if active
    if (wifi_mode_active) {
        WebServerManager::handleClient();
        delay(20);
        return;
    }

    // 3. Run inference periodically
    if (millis() - last_inference >= INFERENCE_INTERVAL_MS) {
        last_inference = millis();
        unsigned long pipeline_start = millis();
        
        camera_fb_t *fb = CameraManager::captureFrame();
        if (fb) {
            InferenceResult res = InferenceManager::runInference(fb, pipeline_start);
            
            if (res.success) {
                unsigned long sd_t0 = millis();
                SDLogger::logResult(res.log_status, res.log_bg, res.log_bh, res.log_bo,
                                    res.crop_jpg_buf, res.crop_jpg_len,
                                    res.inference_ms, res.bx, res.by, res.bw, res.bh_dim,
                                    res.above_threshold);
                int sd_write_ms = (int)(millis() - sd_t0);
                
                InferenceManager::printDebugStats(res, sd_write_ms, SDLogger::getFrameCounter(), SDLogger::getSessionId());
                
                DisplayManager::updateStatus(SDLogger::getSessionId(), SDLogger::getFrameCounter(), 
                                             res.disp_status, res.display_conf, res.inference_ms);
                
                if (res.crop_jpg_buf) {
                    free(res.crop_jpg_buf);
                }
            }
            CameraManager::releaseFrame(fb);
        }
    }
    delay(5);
}