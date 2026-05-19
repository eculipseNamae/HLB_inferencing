#pragma once
#ifndef DISPLAY_MANAGER_H
#define DISPLAY_MANAGER_H

#include <Arduino.h>

class DisplayManager {
public:
    static bool begin();
    static void showInitStatus();
    static void updateStatus(uint32_t sessionId, uint32_t frameCounter, const char* status, float conf, int latency);
    static void showMessage(const char* line1, const char* line2 = nullptr, const char* line3 = nullptr, bool clear = true);
};

#endif // DISPLAY_MANAGER_H
