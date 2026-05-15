#include "touch_handler.h"
#include "config.h"
#include "display.h"

static bool touch_pressed = false;
static unsigned long touch_start = 0;
static unsigned long last_touch_debug = 0;
static bool wifi_toggle_requested = false;

void touch_init() {
    Serial.println("Touch handler initialized");
}

bool touch_handle() {
    wifi_toggle_requested = false;
    
    uint16_t val = touchRead(TOUCH_PIN);
    
    // Debug output every 500ms
    if (millis() - last_touch_debug > 500) {
        last_touch_debug = millis();
        Serial.printf("Touch value: %d\n", val);
    }
    
    bool touched = (val > TOUCH_THRESHOLD);
    
    // Touch start
    if (touched && !touch_pressed) {
        touch_pressed = true;
        touch_start = millis();
        Serial.println("Touch detected");
    }
    
    // Touch held
    if (touched && touch_pressed) {
        unsigned long held_time = millis() - touch_start;
        
        // Print every ~250ms
        static unsigned long last_print = 0;
        if (millis() - last_print > 250) {
            last_print = millis();
            Serial.printf("Holding... %lu ms / %d ms\n", held_time, TOUCH_HOLD_MS);
        }
    }
    
    // Touch release
    else if (!touched && touch_pressed) {
        touch_pressed = false;
        unsigned long held_time = millis() - touch_start;
        
        Serial.printf("Touch released after %lu ms\n", held_time);
        
        // Long hold detected - toggle WiFi mode
        if (held_time >= TOUCH_HOLD_MS) {
            wifi_toggle_requested = true;
            return true;
        }
        // Short touch - show message
        else {
            Serial.println("Touch too short");
            display_show_message("HOLD LONGER");
            delay(800);
        }
    }
    
    return false;
}

bool touch_is_wifi_toggle_requested() {
    return wifi_toggle_requested;
}
