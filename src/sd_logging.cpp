#include "sd_logging.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>

static bool sd_ready = false;
static uint32_t session_id = 0;
static uint32_t frame_counter = 0;
static char session_folder[16];
static char csv_path[24];

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
    if (f) { 
        f.println(count); 
        f.close(); 
    }
    return count;
}

void init_session() {
    session_id = load_and_increment_boot_count();
    frame_counter = 0;
    snprintf(session_folder, sizeof(session_folder), SESSION_FOLDER_FMT, session_id);
    snprintf(csv_path, sizeof(csv_path), CSV_FILE_FMT, session_id);
    
    if (!SD.exists(session_folder)) {
        SD.mkdir(session_folder);
    }
    
    File logFile = SD.open(csv_path, FILE_WRITE);
    if (logFile) {
        logFile.println("Frame,Timestamp_ms,Status,Above_Threshold,Image_File,"
                        "Conf_Greening,Conf_Healthy,Conf_Other,"
                        "Inference_ms,BB_X,BB_Y,BB_W,BB_H");
        logFile.close();
    }
    
    Serial.printf("Session ID: %lu  Folder: %s\r\n", session_id, session_folder);
}

bool sd_init() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, SPI, 16000000)) {
        sd_ready = true;
        init_session();
        return true;
    }
    Serial.println("WARN: SD not found — logging disabled");
    return false;
}

bool sd_is_ready() {
    return sd_ready;
}

uint32_t sd_get_session_id() {
    return session_id;
}

const char* sd_get_session_folder() {
    return session_folder;
}

void sd_log_inference(const char* status, float conf_greening, float conf_healthy, float conf_other,
                      uint8_t* jpg_buf, size_t jpg_len,
                      int inference_ms, int bbox_x, int bbox_y, int bbox_w, int bbox_h,
                      bool above_threshold) {
    if (!sd_ready) return;
    
    // Write JPEG image
    char img_path[32];
    snprintf(img_path, sizeof(img_path), IMAGE_FILE_FMT, session_id, frame_counter);
    File imgFile = SD.open(img_path, FILE_WRITE);
    if (imgFile) { 
        imgFile.write(jpg_buf, jpg_len); 
        imgFile.close(); 
    } else {
        Serial.printf("WARN: Could not write image %s\r\n", img_path);
    }
    
    // Write CSV log
    File logFile = SD.open(csv_path, FILE_APPEND);
    if (logFile) {
        logFile.printf("%lu,%lu,%s,%d,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
            frame_counter, millis(), status, above_threshold ? 1 : 0,
            img_path, conf_greening, conf_healthy, conf_other, 
            inference_ms, bbox_x, bbox_y, bbox_w, bbox_h);
        logFile.close();
        frame_counter++;
    } else {
        Serial.printf("WARN: Could not write CSV %s\r\n", csv_path);
    }
}
