#pragma once
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

class SDLogger {
public:
    static bool begin();
    static void initSession();
    static uint32_t getSessionId();
    static uint32_t getFrameCounter();
    static void incrementFrameCounter();
    static void logResult(const char* status, float bg, float bh, float bo,
                          uint8_t* jpg_buf, size_t jpg_len,
                          int latency, int x, int y, int w, int h,
                          bool above_threshold);
    static bool isReady();
    static const char* getCsvPath();

private:
    static uint32_t loadAndIncrementBootCount();
    static bool sd_ready;
    static uint32_t session_id;
    static uint32_t frame_counter;
    static char session_folder[16];
    static char csv_path[24];
};

#endif // SD_LOGGER_H
