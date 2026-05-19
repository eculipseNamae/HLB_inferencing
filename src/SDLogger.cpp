#include "SDLogger.h"
#include "Config.h"
#include <SPI.h>
#include <FS.h>
#include <SD.h>

bool SDLogger::sd_ready = false;
uint32_t SDLogger::session_id = 0;
uint32_t SDLogger::frame_counter = 0;
char SDLogger::session_folder[16] = {0};
char SDLogger::csv_path[24] = {0};

bool SDLogger::begin() {
    SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, SPI, 16000000)) {
        sd_ready = true;
        return true;
    }
    return false;
}

uint32_t SDLogger::loadAndIncrementBootCount() {
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

void SDLogger::initSession() {
    if (!sd_ready) return;
    
    session_id = loadAndIncrementBootCount();
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
}

uint32_t SDLogger::getSessionId() { return session_id; }
uint32_t SDLogger::getFrameCounter() { return frame_counter; }
void SDLogger::incrementFrameCounter() { frame_counter++; }
bool SDLogger::isReady() { return sd_ready; }
const char* SDLogger::getCsvPath() { return csv_path; }

void SDLogger::logResult(const char* status, float bg, float bh, float bo,
                         uint8_t* jpg_buf, size_t jpg_len,
                         int latency, int x, int y, int w, int h,
                         bool above_threshold) {
    if (!sd_ready) return;
    
    char img_path[32];
    snprintf(img_path, sizeof(img_path), IMAGE_FILE_FMT, session_id, frame_counter);
    
    File imgFile = SD.open(img_path, FILE_WRITE);
    if (imgFile) { 
        imgFile.write(jpg_buf, jpg_len); 
        imgFile.close(); 
    } else {
        Serial.printf("WARN: Could not write image %s\r\n", img_path);
    }

    File logFile = SD.open(csv_path, FILE_APPEND);
    if (logFile) {
        logFile.printf("%lu,%lu,%s,%d,%s,%.2f,%.2f,%.2f,%d,%d,%d,%d,%d\n",
            frame_counter, millis(), status, above_threshold ? 1 : 0,
            img_path, bg, bh, bo, latency, x, y, w, h);
        logFile.close();
        frame_counter++;
    } else {
        Serial.printf("WARN: Could not write CSV %s\r\n", csv_path);
    }
}
