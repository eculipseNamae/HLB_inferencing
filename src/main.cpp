#include <Arduino.h>
#include <HLB6_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <Wire.h>

// Module headers
#include "config.h"
#include "camera.h"
#include "display.h"
#include "sd_logging.h"
#include "touch_handler.h"
#include "wifi_server.h"

/* ================= GLOBALS ================= */
static uint8_t *snapshot_buf = nullptr;
static unsigned long last_inference = 0;

/* ================= INFERENCE PIPELINE ================= */
/**
 * Run ML inference on camera frame:
 * 1. Capture JPEG from camera
 * 2. Convert to RGB888 and crop/scale
 * 3. Run Edge Impulse classifier
 * 4. Extract bounding boxes and confidence scores
 * 5. Log results to SD card
 * 6. Update display
 */
bool run_pipeline() {
    if (!camera_is_ready()) return false;

    // Capture frame from camera
    camera_fb_t *fb = nullptr;
    if (!camera_capture_frame((void**)&fb)) return false;

    // Allocate RGB buffer for image processing
    snapshot_buf = (uint8_t*) heap_caps_malloc(RAW_RGB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot_buf) {
        esp_camera_fb_return(fb);
        return false;
    }

    // Convert JPEG to RGB888
    fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    
    // Crop and interpolate to model input size
    ei::image::processing::crop_and_interpolate_rgb888(
        snapshot_buf, RAW_WIDTH, RAW_HEIGHT,
        snapshot_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT
    );

    // Create signal for classifier
    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
        size_t pixel_ix = offset * 3;
        size_t out_ix   = 0;
        while (length--) {
            out_ptr[out_ix++] = (float)(
                (uint32_t)snapshot_buf[pixel_ix + 2] << 16 |
                (uint32_t)snapshot_buf[pixel_ix + 1] << 8  |
                (uint32_t)snapshot_buf[pixel_ix]);
            pixel_ix += 3;
        }
        return 0;
    };

    // Run classifier
    ei_impulse_result_t result = {0};
    unsigned long t0 = millis();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    int inference_ms = (int)(millis() - t0);

    free(snapshot_buf);
    snapshot_buf = nullptr;

    if (err != EI_IMPULSE_OK) {
        esp_camera_fb_return(fb);
        return false;
    }

    // Extract bounding boxes and find highest confidence per class
    float conf_greening = 0, conf_healthy = 0, conf_other = 0;
    int bbox_x = 0, bbox_y = 0, bbox_w = 0, bbox_h = 0;
    const char* status = "NOTHING";

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        auto bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        
        if (String(bb.label) == "Greening") {
            if (bb.value > conf_greening) {
                conf_greening = bb.value;
                bbox_x = bb.x; bbox_y = bb.y;
                bbox_w = bb.width; bbox_h = bb.height;
            }
            status = "GREENING";
        } else if (String(bb.label) == "Healthy") {
            if (bb.value > conf_healthy) conf_healthy = bb.value;
            if (strcmp(status, "GREENING") != 0) {
                status = "HEALTHY";
                bbox_x = bb.x; bbox_y = bb.y;
                bbox_w = bb.width; bbox_h = bb.height;
            }
        } else {
            if (bb.value > conf_other) conf_other = bb.value;
            if (strcmp(status, "GREENING") != 0 && strcmp(status, "HEALTHY") != 0) {
                status = "OTHER";
                bbox_x = bb.x; bbox_y = bb.y;
                bbox_w = bb.width; bbox_h = bb.height;
            }
        }
    }

    // Determine if above threshold for display
    float display_conf = 0.0f;
    const char* disp_status = "NOTHING";
    bool above_threshold = false;

    if (conf_greening >= CONFIDENCE_THRESHOLD && conf_greening >= conf_healthy && conf_greening >= conf_other) {
        disp_status = "GREENING"; 
        display_conf = conf_greening; 
        above_threshold = true;
    } else if (conf_healthy >= CONFIDENCE_THRESHOLD && conf_healthy >= conf_other) {
        disp_status = "HEALTHY";  
        display_conf = conf_healthy; 
        above_threshold = true;
    } else if (conf_other >= CONFIDENCE_THRESHOLD) {
        disp_status = "OTHER";    
        display_conf = conf_other; 
        above_threshold = true;
    }

    // Log to SD card
    sd_log_inference(status, conf_greening, conf_healthy, conf_other,
                     fb->buf, fb->len,
                     inference_ms,
                     bbox_x, bbox_y, bbox_w, bbox_h,
                     above_threshold);

    esp_camera_fb_return(fb);
    
    // Update display
    display_update_inference(disp_status, display_conf, inference_ms);
    
    return true;
}


/* ================= SETUP ================= */
void setup() {
    Serial.begin(115200);
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time < 5000)) { delay(10); }

    // Initialize display
    if (!display_init()) {
        Serial.println(F("SSD1306 allocation failed"));
    }

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    // Check PSRAM availability
    if (!psramFound()) { 
        Serial.println("FATAL: PSRAM NOT FOUND"); 
        while (1); 
    }

    // Initialize SD card
    if (sd_init()) {
        display_show_message("SESSION: OK");
    } else {
        Serial.println("WARN: SD not found — logging disabled");
    }

    // Initialize camera
    if (camera_init()) {
        display_show_message("CAMERA", "READY");
    }

    // Initialize touch handler
    touch_init();
    Serial.println("Hold touch 2s to toggle WiFi dashboard");

    Serial.printf("Ready. Session %lu\r\n", sd_get_session_id());
}

/* ================= MAIN LOOP ================= */
void loop() {
    // Handle touch sensor input
    touch_handle();

    // Check if WiFi mode toggle was requested
    if (touch_is_wifi_toggle_requested()) {
        if (!wifi_is_active()) {
            display_show_message("STARTING WIFI...");
            wifi_start();
        } else {
            wifi_stop();
        }
    }

    // If WiFi mode active, serve requests and pause inference
    if (wifi_is_active()) {
        wifi_handle_requests();
        delay(20);
        return;
    }

    // Normal inference mode
    if (millis() - last_inference >= INFERENCE_INTERVAL_MS) {
        last_inference = millis();
        run_pipeline();
    }
    
    delay(5);
}