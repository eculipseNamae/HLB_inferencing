#include <Arduino.h>
#include <HLB6_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_heap_caps.h"

// --- SD CARD LIBRARIES ---
#include <SPI.h>
#include <FS.h>
#include <SD.h>

// --- OLED LIBRARIES ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

// OLED Config
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     32
#define OLED_RESET        -1
#define SCREEN_ADDRESS    0x3C

#define INFERENCE_INTERVAL_MS   3000
#define CONFIDENCE_THRESHOLD    0.70f

#define RAW_WIDTH               320
#define RAW_HEIGHT              240
#define RAW_RGB_SIZE            (RAW_WIDTH * RAW_HEIGHT * 3)

// ── Session ID config ─────────────────────────────────────────────────────────
// A session ID is generated once at boot from boot count + uptime salt.
// Format: /S00042/ — all images and the CSV for that session live in that folder.
// Boot count is stored in /boot_count.txt so it survives power cycles.
// Max 99999 sessions before the counter wraps (effectively never for field use).
#define SESSION_FOLDER_FMT   "/S%05lu"
#define IMAGE_FILE_FMT       "/S%05lu/%04lu.jpg"
#define CSV_FILE_FMT         "/S%05lu/log.csv"

/* ================= GLOBALS ================= */
static bool          camera_ready    = false;
static bool          sd_ready        = false;
static uint8_t      *snapshot_buf    = nullptr;
static unsigned long last_inference  = 0;

// Session state — set once in setup(), never changes during a run
static uint32_t session_id      = 0;   // e.g. 42
static uint32_t frame_counter   = 0;   // resets to 0 each session, max 9999
static char     session_folder[16];    // "/S00042"
static char     csv_path[24];          // "/S00042/log.csv"

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ================= FUNCTION PROTOTYPES ================= */
bool init_camera();
bool run_pipeline();
void log_to_sd(const char* status, float bg, float bh, float bo,
               uint8_t* jpg_buf, size_t jpg_len,
               int latency, int x, int y, int w, int h,
               bool above_threshold);
uint32_t load_and_increment_boot_count();
void     init_session();
void     update_display(const char* status, float conf, int latency);

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM, .pin_reset = RESET_GPIO_NUM, .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM, .pin_sscb_scl = SIOC_GPIO_NUM,
    .pin_d7 = Y9_GPIO_NUM, .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM, .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM, .pin_d1 = Y3_GPIO_NUM, .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM, .pin_pclk = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000, .ledc_timer = LEDC_TIMER_0, .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG, .frame_size = FRAMESIZE_QVGA,
    .jpeg_quality = 12, .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM, .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

/* ================= BOOT COUNT ================= */
// Reads /boot_count.txt, increments it, writes it back.
// Returns the NEW count (i.e. this session's ID).
// If the file is missing or unreadable, starts from 1.
uint32_t load_and_increment_boot_count() {
    uint32_t count = 0;

    File f = SD.open("/boot_count.txt", FILE_READ);
    if (f) {
        String line = f.readStringUntil('\n');
        line.trim();
        count = (uint32_t)line.toInt();
        f.close();
    }

    count = (count >= 99999) ? 1 : count + 1;  // wrap gracefully

    f = SD.open("/boot_count.txt", FILE_WRITE); // FILE_WRITE truncates
    if (f) {
        f.println(count);
        f.close();
    }

    return count;
}

/* ================= SESSION INIT ================= */
void init_session() {
    session_id    = load_and_increment_boot_count();
    frame_counter = 0;

    snprintf(session_folder, sizeof(session_folder), SESSION_FOLDER_FMT, session_id);
    snprintf(csv_path,       sizeof(csv_path),       CSV_FILE_FMT,       session_id);

    // Create session folder
    if (!SD.exists(session_folder)) {
        SD.mkdir(session_folder);
    }

    // Write CSV header for this session
    File logFile = SD.open(csv_path, FILE_WRITE);
    if (logFile) {
        logFile.println("Frame,Timestamp_ms,Status,Above_Threshold,Image_File,"
                        "Conf_Greening,Conf_Healthy,Conf_Other,"
                        "Inference_ms,BB_X,BB_Y,BB_W,BB_H");
        logFile.close();
    }

    Serial.printf("Session ID: %lu  Folder: %s\r\n", session_id, session_folder);
}

/* ================= SETUP ================= */
void setup() {
    Serial.begin(115200);
    unsigned long start_time = millis();
    while (!Serial && (millis() - start_time < 5000)) { delay(10); }

    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
    } else {
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(0, 0);
        display.println("HLB SYSTEM INIT...");
        display.display();
    }

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);

    if (!psramFound()) { Serial.println("FATAL: PSRAM NOT FOUND"); while (1); }

    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, SPI, 16000000)) {
        sd_ready = true;
        init_session();   // replaces find_next_file_index()

        // Show session ID on OLED
        display.print("SESSION: ");
        display.println(session_id);
        display.display();
    } else {
        Serial.println("WARN: SD not found — logging disabled");
    }

    if (init_camera()) {
        camera_ready = true;
        display.println("CAMERA READY");
        display.display();
    }

    Serial.printf("Ready. Session %lu | CSV: %s\r\n", session_id, csv_path);
}

/* ================= LOOP ================= */
void loop() {
    if (millis() - last_inference >= INFERENCE_INTERVAL_MS) {
        last_inference = millis();
        run_pipeline();
    }
}

/* ================= CAMERA INIT ================= */
bool init_camera() {
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) return false;
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
    return true;
}

/* ================= PIPELINE ================= */
bool run_pipeline() {
    if (!camera_ready) return false;

    // Guard: frame_counter uses 4 digits (0000–9999). If somehow exceeded,
    // log a warning but keep going — the filename will truncate gracefully.
    if (frame_counter > 9999) {
        Serial.println("WARN: frame_counter exceeded 9999 in this session");
    }

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

    // ── Pass 1: collect raw scores (no threshold) ─────────────────────────────
    float log_bg = 0, log_bh = 0, log_bo = 0;
    int   log_bx = 0, log_by = 0, log_bw = 0, log_bh_dim = 0;
    const char* log_status = "NOTHING";

    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        auto bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;

        if (String(bb.label) == "Greening") {
            if (bb.value > log_bg) {
                log_bg = bb.value;
                log_bx = bb.x; log_by = bb.y;
                log_bw = bb.width; log_bh_dim = bb.height;
            }
            log_status = "GREENING";

        } else if (String(bb.label) == "Healthy") {
            if (bb.value > log_bh) log_bh = bb.value;
            if (strcmp(log_status, "GREENING") != 0) {
                log_status = "HEALTHY";
                log_bx = bb.x; log_by = bb.y;
                log_bw = bb.width; log_bh_dim = bb.height;
            }

        } else {
            if (bb.value > log_bo) log_bo = bb.value;
            if (strcmp(log_status, "GREENING") != 0 && strcmp(log_status, "HEALTHY") != 0) {
                log_status = "OTHER";
                log_bx = bb.x; log_by = bb.y;
                log_bw = bb.width; log_bh_dim = bb.height;
            }
        }
    }

    // ── Pass 2: apply threshold for display only ──────────────────────────────
    float       display_conf    = 0.0f;
    const char* disp_status     = "NOTHING";
    bool        above_threshold = false;

    if (log_bg >= CONFIDENCE_THRESHOLD && log_bg >= log_bh && log_bg >= log_bo) {
        disp_status = "GREENING"; display_conf = log_bg; above_threshold = true;
    } else if (log_bh >= CONFIDENCE_THRESHOLD && log_bh >= log_bo) {
        disp_status = "HEALTHY";  display_conf = log_bh; above_threshold = true;
    } else if (log_bo >= CONFIDENCE_THRESHOLD) {
        disp_status = "OTHER";    display_conf = log_bo; above_threshold = true;
    }

    // ── Log to SD (fb->buf still valid here) ──────────────────────────────────
    log_to_sd(log_status, log_bg, log_bh, log_bo,
              fb->buf, fb->len,
              inference_ms,
              log_bx, log_by, log_bw, log_bh_dim,
              above_threshold);

    // Return frame buffer only AFTER log_to_sd is done with fb->buf
    esp_camera_fb_return(fb);

    update_display(disp_status, display_conf, inference_ms);
    return true;
}

/* ================= DISPLAY ================= */
void update_display(const char* status, float conf, int latency) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    // Show session + frame so the user knows exactly which file they're looking at
    display.printf("S%05lu  F:%04lu", session_id, frame_counter > 0 ? frame_counter - 1 : 0);
    display.setTextSize(2);
    display.setCursor(0, 10);
    display.println(status);
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("C:"); display.print((int)(conf * 100)); display.print("% ");
    display.print("L:"); display.print(latency); display.print("ms");
    display.display();
}

/* ================= SD LOGGING ================= */
void log_to_sd(const char* status, float bg, float bh, float bo,
               uint8_t* jpg_buf, size_t jpg_len,
               int latency, int x, int y, int w, int h,
               bool above_threshold) {
    if (!sd_ready) return;

    // Build image path inside the session folder: /S00042/0003.jpg
    char img_path[32];
    snprintf(img_path, sizeof(img_path), IMAGE_FILE_FMT, session_id, frame_counter);

    // Write JPEG
    File imgFile = SD.open(img_path, FILE_WRITE);
    if (imgFile) {
        imgFile.write(jpg_buf, jpg_len);
        imgFile.close();
    } else {
        Serial.printf("WARN: Could not write image %s\r\n", img_path);
    }

    // Append CSV row
    File logFile = SD.open(csv_path, FILE_APPEND);
    if (logFile) {
        logFile.printf("%lu,%lu,%s,%d,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
            frame_counter,
            millis(),
            status,
            above_threshold ? 1 : 0,
            img_path,
            bg, bh, bo,
            latency, x, y, w, h);
        logFile.close();
        frame_counter++;
    } else {
        Serial.printf("WARN: Could not write CSV %s\r\n", csv_path);
    }
}