#include "DisplayManager.h"
#include "Config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool DisplayManager::begin() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        return false;
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    return true;
}

void DisplayManager::showInitStatus() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("HLB SYSTEM INIT...");
    display.display();
}

void DisplayManager::updateStatus(uint32_t sessionId, uint32_t frameCounter, const char* status, float conf, int latency) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.printf("S%05lu  F:%04lu", sessionId, frameCounter > 0 ? frameCounter - 1 : 0);
    
    display.setTextSize(2);
    display.setCursor(0, 10);
    display.println(status);
    
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("C:"); display.print((int)(conf * 100)); display.print("% ");
    display.print("L:"); display.print(latency); display.print("ms");
    
    display.display();
}

void DisplayManager::showMessage(const char* line1, const char* line2, const char* line3, bool clear) {
    if (clear) display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    if (line1) display.println(line1);
    if (line2) display.println(line2);
    if (line3) display.println(line3);
    display.display();
}
