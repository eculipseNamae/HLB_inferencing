#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

/**
 * Start WiFi mode: connect to network and initialize web server
 * @return true if successful, false otherwise
 */
bool wifi_start();

/**
 * Stop WiFi mode: disconnect and stop server
 */
void wifi_stop();

/**
 * Handle incoming HTTP requests (call in loop when in WiFi mode)
 */
void wifi_handle_requests();

/**
 * Check if WiFi is currently active
 * @return true if WiFi mode enabled, false otherwise
 */
bool wifi_is_active();

#endif
