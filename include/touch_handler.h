#ifndef TOUCH_HANDLER_H
#define TOUCH_HANDLER_H

/**
 * Initialize touch sensor
 */
void touch_init();

/**
 * Handle touch sensor input (call every loop iteration)
 * @return true if WiFi mode toggle requested, false otherwise
 */
bool touch_handle();

/**
 * Check if WiFi mode should be active
 * @return true if WiFi mode toggle was triggered, false otherwise
 */
bool touch_is_wifi_toggle_requested();

#endif
