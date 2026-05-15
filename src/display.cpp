#include "display.h"
#include "config.h"
#include "sd_logging.h"
#include <Wire.h>

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

bool display_init() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        return false;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HLB SYSTEM INIT...");
    display.display();
    return true;
}

void display_update_inference(const char* status, float confidence, int latency_ms) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.printf("S%05lu", sd_get_session_id());
    
    display.setTextSize(2);
    display.setCursor(0, 10);
    display.println(status);
    
    display.setTextSize(1);
    display.setCursor(0, 25);
    display.print("C:"); 
    display.print((int)(confidence * 100)); 
    display.print("% ");
    display.print("L:"); 
    display.print(latency_ms); 
    display.print("ms");
    
    display.display();
}

void display_show_message(const char* line1, const char* line2) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(line1);
    if (line2 && strlen(line2) > 0) {
        display.println(line2);
    }
    display.display();
}

void display_show_wifi_connected(const char* ip_str) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("WIFI MODE");
    display.print("http://");
    display.println(ip_str);
    display.display();
}

Adafruit_SSD1306& display_get_object() {
    return display;
}
