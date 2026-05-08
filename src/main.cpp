#include <Arduino.h>
#include <HLB6_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"
#include <SPI.h>
#include <FS.h>
#include <SD.h>

/* ================= PINS & CONFIG ================= */
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

#define SD_CS_PIN         21
#define SD_SCK_PIN        7
#define SD_MISO_PIN       8
#define SD_MOSI_PIN       9

#define LED_PIN           21

#define INFERENCE_INTERVAL_MS      3000
#define CONFIDENCE_THRESHOLD       0.70f

#define RAW_WIDTH                  320
#define RAW_HEIGHT                 240
#define RAW_RGB_SIZE               (RAW_WIDTH * RAW_HEIGHT * 3)

/* ================= GLOBALS ================= */
static bool camera_ready = false;
static bool sd_ready = false;
static uint8_t *snapshot_buf = nullptr;
static unsigned long last_inference = 0;
static uint32_t image_counter = 0;

/* ================= FUNCTION PROTOTYPES ================= */
bool init_camera();
bool run_pipeline();
// Updated log signature to include X, Y, W, H
void log_to_sd(const char* status, float bg, float bh, float bo, uint8_t* jpg_buf, size_t jpg_len, int latency, int x, int y, int w, int h);
void blink_healthy();
void blink_greening();
void blink_other();
void find_next_file_index();

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM, .pin_reset = RESET_GPIO_NUM, .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM, .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM, .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM, .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM, .pin_d1 = Y3_GPIO_NUM, .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM, .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12, .fb_count = 1, .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

void setup() {
    Serial.begin(115200);
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time < 5000)) { delay(10); }

    Serial.println("\n=== HLB System: Spatial Data Edition ===");
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); 

    if (!psramFound()) { Serial.println("FATAL: PSRAM NOT FOUND"); while (1); }

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, SPI, 4000000)) {
        sd_ready = true;
        find_next_file_index();
        
        // Write Header if file is new - Added Bounding Box columns
        if (!SD.exists("/hlb_log.csv")) {
            File logFile = SD.open("/hlb_log.csv", FILE_WRITE);
            if (logFile) {
                logFile.println("Timestamp_ms,Status,Image_File,Conf_Greening,Conf_Healthy,Conf_Other,Inference_ms,BB_X,BB_Y,BB_W,BB_H");
                logFile.close();
            }
        }

        File logFile = SD.open("/hlb_log.csv", FILE_APPEND);
        if (logFile) {
            logFile.println("0,--- NEW_SESSION ---,N/A,0,0,0,0,0,0,0,0");
            logFile.close();
        }
    }

    if (init_camera()) {
        camera_ready = true;
        Serial.println("System Ready.");
    }
}

void loop() {
    if (millis() - last_inference >= INFERENCE_INTERVAL_MS) {
        last_inference = millis();
        run_pipeline();
    }
}

void find_next_file_index() {
    uint32_t index = 0;
    while (index < 99999) {
        char filename[32];
        snprintf(filename, sizeof(filename), "/img_%05lu.jpg", index);
        if (!SD.exists(filename)) {
            image_counter = index;
            return;
        }
        index++;
    }
}

bool init_camera() {
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) return false;
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    return true;
}

bool run_pipeline() {
    if (!camera_ready) return false;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return false;

    snapshot_buf = (uint8_t*) heap_caps_malloc(RAW_RGB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot_buf) {
        esp_camera_fb_return(fb);
        return false;
    }

    fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    
    ei::image::processing::crop_and_interpolate_rgb888(
        snapshot_buf, RAW_WIDTH, RAW_HEIGHT, 
        snapshot_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT
    );

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
        size_t pixel_ix = offset * 3;
        size_t out_ix = 0;
        while (length--) {
            out_ptr[out_ix++] = (float)((uint32_t)snapshot_buf[pixel_ix+2]<<16 | (uint32_t)snapshot_buf[pixel_ix+1]<<8 | (uint32_t)snapshot_buf[pixel_ix]);
            pixel_ix += 3;
        }
        return 0;
    };

    ei_impulse_result_t result = {0};
    unsigned long start_inference = millis();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    int inference_ms = (int)(millis() - start_inference);
    
    if (err != EI_IMPULSE_OK) {
        free(snapshot_buf);
        esp_camera_fb_return(fb);
        return false;
    }

    float bg = 0, bh = 0, bo = 0;
    int bx = 0, by = 0, bw = 0, bh_dim = 0; 
    const char* status = "NOTHING";
    bool any_object_found = false; // NEW FLAG
    
    Serial.printf("--- Inference Result (%d ms) ---\n", inference_ms);
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        auto bb = result.bounding_boxes[i];
        if (bb.value < CONFIDENCE_THRESHOLD) continue;
        
        any_object_found = true; // Mark that we actually saw something valid
        Serial.printf("%s: %.2f [x:%d y:%d w:%d h:%d]\n", bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
        
        if (String(bb.label) == "Greening") { 
            if (bb.value > bg) { 
                bg = bb.value; bx = bb.x; by = bb.y; bw = bb.width; bh_dim = bb.height; 
            }
            status = "GREENING"; 
        } else if (String(bb.label) == "Healthy") { 
            if (bb.value > bh && strcmp(status, "GREENING") != 0) { 
                bh = bb.value; bx = bb.x; by = bb.y; bw = bb.width; bh_dim = bb.height; 
            }
            if(strcmp(status, "GREENING") != 0) status = "HEALTHY"; 
        } else { 
            if (bb.value > bo && strcmp(status, "GREENING") != 0) { 
                bo = bb.value; bx = bb.x; by = bb.y; bw = bb.width; bh_dim = bb.height; 
            }
            if(strcmp(status, "GREENING") != 0) status = "OTHER"; 
        }
    }

    if (!any_object_found) {
        Serial.println("FINAL DECISION: NOTHING DETECTED");
    } else {
        Serial.printf("FINAL DECISION: %s\n", status);
    }

    // Log everything to SD (even "NOTHING" entries are good for data integrity)
    log_to_sd(status, bg, bh, bo, fb->buf, fb->len, inference_ms, bx, by, bw, bh_dim);

    // BLINK ONLY IF SOMETHING WAS ACTUALLY DETECTED
    if (any_object_found) {
        if (strcmp(status, "GREENING") == 0) blink_greening();
        else if (strcmp(status, "OTHER") == 0) blink_other();
        else if (strcmp(status, "HEALTHY") == 0) blink_healthy();
    } else {
        // Optional: Do nothing or a very tiny "pulse" to show the system is alive
        digitalWrite(LED_PIN, HIGH); 
    }

    free(snapshot_buf);
    esp_camera_fb_return(fb);
    return true;
}

void log_to_sd(const char* status, float bg, float bh, float bo, uint8_t* jpg_buf, size_t jpg_len, int latency, int x, int y, int w, int h) {
    if (!sd_ready) return;

    digitalWrite(LED_PIN, HIGH);
    delayMicroseconds(50);

    char filename[32];
    snprintf(filename, sizeof(filename), "/img_%05lu.jpg", image_counter);

    // File imgFile = SD.open(filename, FILE_WRITE);
    // if (imgFile) {
    //     imgFile.write(jpg_buf, jpg_len);
    //     imgFile.close();
    // }

    File logFile = SD.open("/hlb_log.csv", FILE_APPEND);
    if (logFile) {
        // CSV: ms, status, file, green%, healthy%, other%, latency, x, y, w, h
        logFile.printf("%lu,%s,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n", millis(), status, filename, bg, bh, bo, latency, x, y, w, h);
        logFile.close();
        image_counter++;
    }
}

void blink_healthy() { digitalWrite(LED_PIN, LOW); delay(150); digitalWrite(LED_PIN, HIGH); }
void blink_other() { for (int i = 0; i < 2; i++) { digitalWrite(LED_PIN, LOW); delay(250); digitalWrite(LED_PIN, HIGH); delay(250); } }
void blink_greening() { for (int i = 0; i < 6; i++) { digitalWrite(LED_PIN, LOW); delay(80); digitalWrite(LED_PIN, HIGH); delay(80); } }