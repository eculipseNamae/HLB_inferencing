#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

/**
 * Initialize ESP32 camera with configured pins
 * @return true if successful, false otherwise
 */
bool camera_init();

/**
 * Get current camera ready status
 * @return true if camera initialized, false otherwise
 */
bool camera_is_ready();

/**
 * Capture a frame from camera
 * @param[out] fb pointer to frame buffer (caller must call esp_camera_fb_return)
 * @return true if frame captured, false otherwise
 */
bool camera_capture_frame(void** fb);

#endif
