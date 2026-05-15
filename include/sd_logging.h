#ifndef SD_LOGGING_H
#define SD_LOGGING_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize SD card and session
 * @return true if successful, false otherwise
 */
bool sd_init();

/**
 * Check if SD card is ready
 * @return true if SD card initialized, false otherwise
 */
bool sd_is_ready();

/**
 * Get current session ID
 * @return Session ID (0 if not initialized)
 */
uint32_t sd_get_session_id();

/**
 * Get current session folder path
 * @return Pointer to session folder path string
 */
const char* sd_get_session_folder();

/**
 * Log inference results to SD card
 * @param status Classification status string
 * @param conf_greening Confidence for greening class
 * @param conf_healthy Confidence for healthy class
 * @param conf_other Confidence for other class
 * @param jpg_buf JPEG buffer data
 * @param jpg_len JPEG buffer length
 * @param inference_ms Inference latency in ms
 * @param bbox_x Bounding box X coordinate
 * @param bbox_y Bounding box Y coordinate
 * @param bbox_w Bounding box width
 * @param bbox_h Bounding box height
 * @param above_threshold Whether detection is above confidence threshold
 */
void sd_log_inference(const char* status, float conf_greening, float conf_healthy, float conf_other,
                      uint8_t* jpg_buf, size_t jpg_len,
                      int inference_ms, int bbox_x, int bbox_y, int bbox_w, int bbox_h,
                      bool above_threshold);

#endif
