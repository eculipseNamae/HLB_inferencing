#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_SSD1306.h>

/**
 * Initialize OLED display
 * @return true if successful, false otherwise
 */
bool display_init();

/**
 * Update display with inference results
 * @param status Classification status (e.g., "GREENING", "HEALTHY", "OTHER")
 * @param confidence Confidence score (0.0 - 1.0)
 * @param latency_ms Inference latency in milliseconds
 */
void display_update_inference(const char* status, float confidence, int latency_ms);

/**
 * Show message on display
 * @param line1 First line of text
 * @param line2 Second line of text (optional, can be empty)
 */
void display_show_message(const char* line1, const char* line2 = "");

/**
 * Clear and show WiFi connection info
 * @param ip_str IP address string
 */
void display_show_wifi_connected(const char* ip_str);

/**
 * Get the display object
 * @return Reference to Adafruit_SSD1306 display
 */
Adafruit_SSD1306& display_get_object();

#endif
