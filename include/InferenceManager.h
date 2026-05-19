#pragma once
#ifndef INFERENCE_MANAGER_H
#define INFERENCE_MANAGER_H

#include <Arduino.h>
#include "esp_camera.h"

struct InferenceResult {
    bool success;
    int inference_ms;
    int pipeline_ms;
    size_t raw_jpg_len;
    size_t crop_jpg_len;
    float log_bg, log_bh, log_bo;
    int bx, by, bw, bh_dim;
    const char* log_status;
    const char* disp_status;
    float display_conf;
    bool above_threshold;
    uint8_t* crop_jpg_buf;
};

class InferenceManager {
public:
    static InferenceResult runInference(camera_fb_t* fb, unsigned long pipeline_start);
    static void printDebugStats(const InferenceResult& res, int sd_write_ms, uint32_t frame_counter, uint32_t session_id);
};

#endif // INFERENCE_MANAGER_H
