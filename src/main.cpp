#include <Arduino.h>
#include <HLB6_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"

/* ================= CAMERA PINS ================= */

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

/* ================= USER CONFIG ================= */

#define LED_PIN                    21

#define INFERENCE_INTERVAL_MS      2000
#define CONFIDENCE_THRESHOLD       0.70f

#define RAW_WIDTH                  320
#define RAW_HEIGHT                 240
#define RAW_RGB_SIZE               (RAW_WIDTH * RAW_HEIGHT * 3)

#define EI_RESIZED_WIDTH           EI_CLASSIFIER_INPUT_WIDTH
#define EI_RESIZED_HEIGHT          EI_CLASSIFIER_INPUT_HEIGHT

/* ================= GLOBALS ================= */

static bool camera_ready = false;

static uint8_t *snapshot_buf = nullptr;

static unsigned long last_inference = 0;

/* ================= CAMERA CONFIG ================= */

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,

    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,

    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

/* ================= FUNCTION DECLARATIONS ================= */

bool init_camera();
bool capture_image();

void blink_healthy();
void blink_greening();
void blink_other();

static int ei_camera_get_data(size_t offset,
                              size_t length,
                              float *out_ptr);

/* ================= SETUP ================= */

void setup() {

    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== HLB Detection System ===");

    pinMode(LED_PIN, OUTPUT);

    // XIAO ESP32S3 LED is active LOW
    digitalWrite(LED_PIN, HIGH);

    if (!psramFound()) {
        Serial.println("ERROR: PSRAM NOT FOUND");
        while (1);
    }

    Serial.printf("Free Heap: %u\n", ESP.getFreeHeap());
    Serial.printf("Free PSRAM: %u\n", ESP.getFreePsram());

    if (!init_camera()) {
        Serial.println("Camera init failed");
        while (1);
    }

    Serial.println("Camera initialized");
    Serial.println("System ready");
}

/* ================= MAIN LOOP ================= */

void loop() {

    if (millis() - last_inference < INFERENCE_INTERVAL_MS) {
        return;
    }

    last_inference = millis();

    snapshot_buf = (uint8_t*) heap_caps_malloc(
        RAW_RGB_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (!snapshot_buf) {
        Serial.println("ERR: snapshot_buf alloc failed");
        return;
    }

    if (!capture_image()) {

        Serial.println("Capture failed");

        free(snapshot_buf);
        snapshot_buf = nullptr;

        return;
    }

    ei::signal_t signal;

    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH *
        EI_CLASSIFIER_INPUT_HEIGHT;

    signal.get_data = &ei_camera_get_data;

    ei_impulse_result_t result = {0};

    EI_IMPULSE_ERROR err =
        run_classifier(&signal, &result, false);

    free(snapshot_buf);
    snapshot_buf = nullptr;

    if (err != EI_IMPULSE_OK) {

        Serial.printf(
            "Inference failed: %d\n",
            err
        );

        return;
    }

#if EI_CLASSIFIER_OBJECT_DETECTION == 1

    bool greening_detected = false;
    bool healthy_detected = false;
    bool other_detected = false;

    float best_greening = 0.0f;
    float best_healthy = 0.0f;
    float best_other = 0.0f;

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {

        auto bb = result.bounding_boxes[i];

        if (bb.value < CONFIDENCE_THRESHOLD) {
            continue;
        }

        Serial.printf(
            "%s %.2f [x:%d y:%d w:%d h:%d]\n",
            bb.label,
            bb.value,
            bb.x,
            bb.y,
            bb.width,
            bb.height
        );

        String label = String(bb.label);

        if (label == "Greening") {

            greening_detected = true;

            if (bb.value > best_greening) {
                best_greening = bb.value;
            }
        }

        else if (label == "Healthy") {

            healthy_detected = true;

            if (bb.value > best_healthy) {
                best_healthy = bb.value;
            }
        }

        else {

            other_detected = true;

            if (bb.value > best_other) {
                best_other = bb.value;
            }
        }
    }

    /*
        PRIORITY SYSTEM

        Greening > Other > Healthy

        Since camera continuously detects objects,
        we only show the MOST IMPORTANT status.
    */

    if (greening_detected) {

        Serial.printf(
            "STATUS: GREENING (%.2f)\n",
            best_greening
        );

        blink_greening();
    }

    else if (other_detected) {

        Serial.printf(
            "STATUS: OTHER ISSUE (%.2f)\n",
            best_other
        );

        blink_other();
    }

    else if (healthy_detected) {

        Serial.printf(
            "STATUS: HEALTHY (%.2f)\n",
            best_healthy
        );

        blink_healthy();
    }

    else {

        Serial.println("STATUS: NOTHING DETECTED");
    }

#endif
}

/* ================= CAMERA ================= */

bool init_camera() {

    esp_err_t err = esp_camera_init(&camera_config);

    if (err != ESP_OK) {

        Serial.printf(
            "Camera error: 0x%x\n",
            err
        );

        return false;
    }

    sensor_t *s = esp_camera_sensor_get();

    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);

    camera_ready = true;

    return true;
}

bool capture_image() {

    if (!camera_ready) {
        return false;
    }

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
        return false;
    }

    bool converted = fmt2rgb888(
        fb->buf,
        fb->len,
        PIXFORMAT_JPEG,
        snapshot_buf
    );

    esp_camera_fb_return(fb);

    if (!converted) {

        Serial.println("RGB conversion failed");

        return false;
    }

    ei::image::processing::crop_and_interpolate_rgb888(
        snapshot_buf,
        RAW_WIDTH,
        RAW_HEIGHT,
        snapshot_buf,
        EI_RESIZED_WIDTH,
        EI_RESIZED_HEIGHT
    );

    return true;
}

/* ================= EI DATA ================= */

static int ei_camera_get_data(size_t offset,
                              size_t length,
                              float *out_ptr)
{
    size_t pixel_ix = offset * 3;
    size_t out_ix = 0;

    while (length--) {

        out_ptr[out_ix++] = (float)(
            ((uint32_t)snapshot_buf[pixel_ix + 2] << 16) |
            ((uint32_t)snapshot_buf[pixel_ix + 1] << 8)  |
            ((uint32_t)snapshot_buf[pixel_ix])
        );

        pixel_ix += 3;
    }

    return 0;
}

/* ================= LED PATTERNS ================= */

/*
    XIAO ESP32S3 LED:
    LOW = ON
    HIGH = OFF
*/

void blink_healthy() {

    // One short blink

    digitalWrite(LED_PIN, LOW);
    delay(120);

    digitalWrite(LED_PIN, HIGH);
}

void blink_other() {

    // Two slow pulses

    for (int i = 0; i < 2; i++) {

        digitalWrite(LED_PIN, LOW);
        delay(250);

        digitalWrite(LED_PIN, HIGH);
        delay(250);
    }
}

void blink_greening() {

    // Rapid urgent strobe

    for (int i = 0; i < 6; i++) {

        digitalWrite(LED_PIN, LOW);
        delay(60);

        digitalWrite(LED_PIN, HIGH);
        delay(60);
    }
}