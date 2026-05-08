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

// ─── NEW: WiFi & Web Server ───────────────────────────────────────────────────
#include <WiFi.h>
#include <WebServer.h>
// ─────────────────────────────────────────────────────────────────────────────

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

// Session ID config
#define SESSION_FOLDER_FMT   "/S%05lu"
#define IMAGE_FILE_FMT       "/S%05lu/%04lu.jpg"
#define CSV_FILE_FMT         "/S%05lu/log.csv"

// ─── NEW: WiFi config ─────────────────────────────────────────────────────────
#define WIFI_STA_SSID        "GlobeAtHome_C9A2D_2.4"
#define WIFI_STA_PASS        "Eu8Nhg3p"
#define TOUCH_PIN            2
#define TOUCH_THRESHOLD      22000
#define TOUCH_HOLD_MS  2000
// ─────────────────────────────────────────────────────────────────────────────

/* ================= GLOBALS ================= */
static bool          camera_ready    = false;
static bool          sd_ready        = false;
static uint8_t      *snapshot_buf    = nullptr;
static unsigned long last_inference  = 0;
static bool touch_pressed = false;

static uint32_t session_id      = 0;
static uint32_t frame_counter   = 0;
static char     session_folder[16];
static char     csv_path[24];

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ─── NEW: WiFi globals ────────────────────────────────────────────────────────
static bool          wifi_mode       = false;
static unsigned long touch_start     = 0;
static bool          touch_held      = false;
static unsigned long last_touch_debug = 0;
WebServer            server(80);
// ─────────────────────────────────────────────────────────────────────────────

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

// ─── NEW: WiFi prototypes ─────────────────────────────────────────────────────
void start_wifi_mode();
void stop_wifi_mode();
void handle_touch();
void serve_dashboard();
void serve_csv();
void serve_image();
void serve_sessions();
// ─────────────────────────────────────────────────────────────────────────────

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
uint32_t load_and_increment_boot_count() {
    uint32_t count = 0;
    File f = SD.open("/boot_count.txt", FILE_READ);
    if (f) {
        String line = f.readStringUntil('\n');
        line.trim();
        count = (uint32_t)line.toInt();
        f.close();
    }
    count = (count >= 99999) ? 1 : count + 1;
    f = SD.open("/boot_count.txt", FILE_WRITE);
    if (f) { f.println(count); f.close(); }
    return count;
}

/* ================= SESSION INIT ================= */
void init_session() {
    session_id    = load_and_increment_boot_count();
    frame_counter = 0;
    snprintf(session_folder, sizeof(session_folder), SESSION_FOLDER_FMT, session_id);
    snprintf(csv_path,       sizeof(csv_path),       CSV_FILE_FMT,       session_id);
    if (!SD.exists(session_folder)) SD.mkdir(session_folder);
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
        init_session();
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

    // ─── NEW: touch pin setup ─────────────────────────────────────────────────
    // Capacitive touch needs no pinMode — touchRead() works directly
    Serial.println("Hold touch 2s to toggle WiFi dashboard");
    // ─────────────────────────────────────────────────────────────────────────

    Serial.printf("Ready. Session %lu | CSV: %s\r\n", session_id, csv_path);
}

/* ================= LOOP ================= */
void loop() {
    // ─── NEW: touch handling runs every loop ──────────────────────────────────
    handle_touch();
    // ─────────────────────────────────────────────────────────────────────────

    // ─── NEW: WiFi mode — serve requests, skip inference ─────────────────────
    if (wifi_mode) {
        server.handleClient();
        delay(20);
        return;                  // inference is paused while WiFi is active
    }
    // ─────────────────────────────────────────────────────────────────────────

    if (millis() - last_inference >= INFERENCE_INTERVAL_MS) {
        last_inference = millis();
        run_pipeline();
    }
    delay(5);
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

    log_to_sd(log_status, log_bg, log_bh, log_bo,
              fb->buf, fb->len,
              inference_ms,
              log_bx, log_by, log_bw, log_bh_dim,
              above_threshold);

    esp_camera_fb_return(fb);
    update_display(disp_status, display_conf, inference_ms);
    return true;
}

/* ================= DISPLAY ================= */
void update_display(const char* status, float conf, int latency) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
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
    char img_path[32];
    snprintf(img_path, sizeof(img_path), IMAGE_FILE_FMT, session_id, frame_counter);
    File imgFile = SD.open(img_path, FILE_WRITE);
    if (imgFile) { imgFile.write(jpg_buf, jpg_len); imgFile.close(); }
    else Serial.printf("WARN: Could not write image %s\r\n", img_path);

    File logFile = SD.open(csv_path, FILE_APPEND);
    if (logFile) {
        logFile.printf("%lu,%lu,%s,%d,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
            frame_counter, millis(), status, above_threshold ? 1 : 0,
            img_path, bg, bh, bo, latency, x, y, w, h);
        logFile.close();
        frame_counter++;
    } else Serial.printf("WARN: Could not write CSV %s\r\n", csv_path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// NEW: WiFi / Touch / Web Server — everything below is additive
// ═══════════════════════════════════════════════════════════════════════════════

/* ================= TOUCH HANDLER ================= */
void handle_touch() {

    uint16_t val = touchRead(TOUCH_PIN);



    if (millis() - last_touch_debug > 500) {

        last_touch_debug = millis();

        Serial.printf("Touch value: %d\n", val);
    }

    bool touched = (val > TOUCH_THRESHOLD);

    /* ================= TOUCH START ================= */

    if (touched && !touch_pressed) {

        touch_pressed = true;
        touch_start = millis();

        Serial.println("Touch detected");

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("TOUCH DETECTED");
        display.println("Hold to toggle");
        display.display();
    }

    /* ================= TOUCH HELD ================= */

    if (touched && touch_pressed) {

        unsigned long held_time = millis() - touch_start;

        // Print every ~250ms
        static unsigned long last_print = 0;

        if (millis() - last_print > 250) {

            last_print = millis();

            Serial.printf(
                "Holding... %lu ms / %d ms\n",
                held_time,
                TOUCH_HOLD_MS
            );

            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(0, 0);

            display.println("HOLDING...");
            display.printf("%lu / %d ms\n",
                held_time,
                TOUCH_HOLD_MS
            );

            display.display();
        }
    }

    /* ================= TOUCH RELEASE ================= */

    else if (!touched && touch_pressed) {

        touch_pressed = false;

        unsigned long held_time = millis() - touch_start;

        Serial.printf("Touch released after %lu ms\n", held_time);

        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(0, 0);

        display.println("TOUCH RELEASED");
        display.printf("%lu ms\n", held_time);

        display.display();

        /* ================= LONG HOLD ================= */

        if (held_time >= TOUCH_HOLD_MS) {

            wifi_mode = !wifi_mode;

            if (wifi_mode) {

                Serial.println("=== WIFI MODE ENABLED ===");

                display.clearDisplay();
                display.setCursor(0, 0);
                display.println("STARTING WIFI...");
                display.display();
                
                start_wifi_mode();

            }
            else {

                Serial.println("=== STOPPING WIFI MODE ===");

                stop_wifi_mode();

                Serial.println("Returned to inference mode");
            }
        }

        /* ================= SHORT TOUCH ================= */

        else {

            Serial.println("Touch too short");

            display.clearDisplay();
            display.setCursor(0, 0);
            display.println("HOLD LONGER");
            display.display();

            delay(800);
        }
    }
}

/* ================= START / STOP WiFi ================= */
void start_wifi_mode() {
    wifi_mode = true;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("CONNECTING WiFi...");
    display.display();

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi FAILED — returning to inference mode");
        wifi_mode = false;
        display.clearDisplay();
        display.setCursor(0, 0);
        display.println("WIFI FAILED");
        display.println("Check credentials");
        display.display();
        delay(2000);
        return;
    }

    IPAddress ip = WiFi.localIP();
    Serial.printf("\nWiFi connected. Open http://%s\r\n", ip.toString().c_str());

    server.on("/",         HTTP_GET, serve_dashboard);
    server.on("/sessions", HTTP_GET, serve_sessions);
    server.on("/csv",      HTTP_GET, serve_csv);
    server.on("/image",    HTTP_GET, serve_image);
    server.begin();

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("WIFI MODE");
    display.print("http://");
    display.println(ip.toString());
    display.display();
}

void stop_wifi_mode() {
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_mode = false;
    Serial.println("WiFi disconnected. Resuming inference.");

    delay(500);
    last_inference = millis();

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("INFERENCE MODE");
    display.display();
    delay(1000);
}

/* ─────────────────────────────────────────────────────────────────────────────
   DASHBOARD  GET /
   Renders a self-contained HTML page. Reads the current session's CSV and
   builds a summary table + per-row image links. Auto-refreshes every 10 s
   while the device is in WiFi mode.
───────────────────────────────────────────────────────────────────────────── */
void serve_dashboard() {
    if (!sd_ready) {
        server.send(503, "text/plain", "SD card not available");
        return;
    }

    // ── read CSV into memory (stream line-by-line to avoid large String concat)
    File csvFile = SD.open(csv_path, FILE_READ);
    if (!csvFile) {
        server.send(404, "text/plain", "Log file not found");
        return;
    }

    // Count totals while building rows — single pass
    int total = 0, greening = 0, healthy = 0, other_count = 0;

    // We'll build the table body in a String (acceptable for ~hundreds of rows)
    String rows = "";
    bool   header_skipped = false;

    while (csvFile.available()) {
        String line = csvFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (!header_skipped) { header_skipped = true; continue; }   // skip CSV header

        // Parse just the fields we need: Frame,Timestamp,Status,Above,ImagePath,Cg,Ch,Co,...
        // Split on comma
        int    col     = 0;
        String fields[13];
        int    start   = 0;
        for (int i = 0; i <= (int)line.length(); i++) {
            if (i == (int)line.length() || line[i] == ',') {
                fields[col++] = line.substring(start, i);
                start = i + 1;
                if (col >= 13) break;
            }
        }

        if (col < 5) continue;

        String frame    = fields[0];
        String ts       = fields[1];
        String status   = fields[2];
        String above    = fields[3];   // "1" or "0"
        String imgPath  = fields[4];
        String cg       = (col > 5) ? fields[5] : "-";
        String ch       = (col > 6) ? fields[6] : "-";

        total++;
        if (status == "GREENING") greening++;
        else if (status == "HEALTHY") healthy++;
        else other_count++;

        // Row colour
        String rowClass = "";
        if (status == "GREENING") rowClass = " style='background:#ffe0e0'";
        else if (status == "HEALTHY") rowClass = " style='background:#e0ffe0'";

        rows += "<tr" + rowClass + "><td>" + frame + "</td><td>" + ts +
                "</td><td><b>" + status + "</b></td><td>" + cg +
                "</td><td>" + ch +
                "</td><td><a href='/image?path=" + imgPath +
                "' target='_blank'>view</a></td></tr>\n";
    }
    csvFile.close();

    // ── build full HTML page
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='10'>"
        "<title>HLB Monitor - Session " + String(session_id) + "</title>"
        "<style>"
        "body{font-family:sans-serif;margin:16px;}"
        "h1{color:#2c5f2e;}"
        "table{border-collapse:collapse;width:100%;font-size:13px;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left;}"
        "th{background:#2c5f2e;color:#fff;}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:8px;"
        "  color:#fff;font-weight:bold;}"
        ".g{background:#c0392b;} .h{background:#27ae60;} .o{background:#7f8c8d;}"
        "</style></head><body>"
        "<h1>HLB Detection Monitor</h1>"
        "<p><b>Session:</b> S" + String(session_id) +
        " &nbsp;|&nbsp; <b>Total frames:</b> " + String(total) +
        " &nbsp;|&nbsp; "
        "<span class='badge g'>Greening: " + String(greening) + "</span> "
        "<span class='badge h'>Healthy: "  + String(healthy)  + "</span> "
        "<span class='badge o'>Other: "    + String(other_count) + "</span>"
        "</p>"
        "<p><a href='/sessions'>Browse all sessions</a> "
        "&nbsp;|&nbsp; <a href='/csv?session=" + String(session_id) +
        "'>Download CSV</a></p>"
        "<table><tr>"
        "<th>Frame</th><th>Time (ms)</th><th>Status</th>"
        "<th>Conf G</th><th>Conf H</th><th>Image</th>"
        "</tr>\n" + rows + "</table>"
        "<p style='color:#888;font-size:11px'>Auto-refreshes every 10 s. "
        "Long-press touch to exit WiFi mode.</p>"
        "</body></html>";

    server.send(200, "text/html", html);
}

/* ─────────────────────────────────────────────────────────────────────────────
   SESSION LIST  GET /sessions
   Lists all session folders found on the SD card.
───────────────────────────────────────────────────────────────────────────── */
void serve_sessions() {
    if (!sd_ready) { server.send(503, "text/plain", "SD not available"); return; }

    File root = SD.open("/");
    if (!root) { server.send(500, "text/plain", "Cannot open SD root"); return; }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Sessions</title>"
        "<style>body{font-family:sans-serif;margin:16px;}"
        "a{display:block;margin:4px 0;}</style></head><body>"
        "<h2>All Sessions</h2>";

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("S")) {
                // Extract session number from folder name
                uint32_t sid = (uint32_t)name.substring(1).toInt();
                html += "<a href='/csv?session=" + String(sid) +
                        "'>Session " + name + " — Download CSV</a>";
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    html += "<br><a href='/'>Back to dashboard</a></body></html>";
    server.send(200, "text/html", html);
}

/* ─────────────────────────────────────────────────────────────────────────────
   CSV DOWNLOAD  GET /csv?session=42
   Streams the CSV for the requested session directly to the browser.
───────────────────────────────────────────────────────────────────────────── */
void serve_csv() {
    if (!sd_ready) { server.send(503, "text/plain", "SD not available"); return; }

    uint32_t sid = server.hasArg("session")
                   ? (uint32_t)server.arg("session").toInt()
                   : session_id;

    char path[32];
    snprintf(path, sizeof(path), CSV_FILE_FMT, sid);

    File f = SD.open(path, FILE_READ);
    if (!f) { server.send(404, "text/plain", "CSV not found"); return; }

    // Inline filename header so the browser saves it with a useful name
    char disposition[48];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"S%05lu.csv\"", sid);
    server.sendHeader("Content-Disposition", disposition);
    server.streamFile(f, "text/csv");
    f.close();
}

/* ─────────────────────────────────────────────────────────────────────────────
   IMAGE VIEWER  GET /image?path=/S00042/0003.jpg
   Streams a JPEG from the SD card directly to the browser.
───────────────────────────────────────────────────────────────────────────── */
void serve_image() {
    if (!sd_ready) { server.send(503, "text/plain", "SD not available"); return; }

    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        return;
    }

    String path = server.arg("path");

    // Basic path traversal guard — reject anything with ".."
    if (path.indexOf("..") >= 0) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) { server.send(404, "text/plain", "Image not found"); return; }

    server.streamFile(f, "image/jpeg");
    f.close();
}