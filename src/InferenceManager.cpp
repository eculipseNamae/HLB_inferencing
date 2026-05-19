#include "InferenceManager.h"
#include "Config.h"
#include "esp_heap_caps.h"
#include <HLB8_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

// Global pointer needed because signal.get_data requires a function pointer (no captures)
static uint8_t* global_crop_buf = nullptr;
static ei_impulse_result_t global_ei_result = {0};

InferenceResult InferenceManager::runInference(camera_fb_t* fb, unsigned long pipeline_start) {
    InferenceResult res = {0};
    res.success = false;
    res.log_status = "NOTHING";
    res.disp_status = "NOTHING";
    
    if (!fb) return res;
    res.raw_jpg_len = fb->len;

    uint8_t* snapshot_buf = (uint8_t*) heap_caps_malloc(RAW_RGB_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot_buf) return res;

    fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    
    const int CROP_X      = 40;
    const int CROP_Y      = 0;
    const int CROP_W      = 240;
    const int CROP_H      = 240;
    const int CROP_STRIDE = RAW_WIDTH * 3;

    global_crop_buf = (uint8_t*) heap_caps_malloc(CROP_W * CROP_H * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!global_crop_buf) {
        free(snapshot_buf);
        return res;
    }

    for (int row = 0; row < CROP_H; row++) {
        memcpy(
            global_crop_buf + row * CROP_W * 3,
            snapshot_buf + (CROP_Y + row) * CROP_STRIDE + CROP_X * 3,
            CROP_W * 3
        );
    }

    fmt2jpg(global_crop_buf, CROP_W * CROP_H * 3, CROP_W, CROP_H,
            PIXFORMAT_RGB888, 100, &res.crop_jpg_buf, &res.crop_jpg_len);

    ei::image::processing::crop_and_interpolate_rgb888(
        global_crop_buf, CROP_W, CROP_H,
        global_crop_buf, EI_CLASSIFIER_INPUT_WIDTH, EI_CLASSIFIER_INPUT_HEIGHT
    );

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = [](size_t offset, size_t length, float *out_ptr) -> int {
        size_t pixel_ix = offset * 3;
        size_t out_ix   = 0;
        while (length--) {
            out_ptr[out_ix++] = (float)(
                (uint32_t)global_crop_buf[pixel_ix + 2] << 16 |
                (uint32_t)global_crop_buf[pixel_ix + 1] << 8  |
                (uint32_t)global_crop_buf[pixel_ix]);
            pixel_ix += 3;
        }
        return 0;
    };

    unsigned long t0 = millis();
    EI_IMPULSE_ERROR err = run_classifier(&signal, &global_ei_result, false);
    res.inference_ms = (int)(millis() - t0);

    free(snapshot_buf); 
    free(global_crop_buf); 
    global_crop_buf = nullptr;

    if (err != EI_IMPULSE_OK) {
        if (res.crop_jpg_buf) { 
            free(res.crop_jpg_buf); 
            res.crop_jpg_buf = nullptr; 
        }
        return res;
    }

    res.log_bg = 0; res.log_bh = 0; res.log_bo = 0;
    res.bx = 0; res.by = 0; res.bw = 0; res.bh_dim = 0;

    for (uint32_t i = 0; i < global_ei_result.bounding_boxes_count; i++) {
        auto bb = global_ei_result.bounding_boxes[i];
        if (bb.value == 0) continue;
        if (String(bb.label) == "Greening") {
            if (bb.value > res.log_bg) {
                res.log_bg = bb.value;
                res.bx = bb.x; res.by = bb.y;
                res.bw = bb.width; res.bh_dim = bb.height;
            }
            res.log_status = "GREENING";
        } else if (String(bb.label) == "Healthy") {
            if (bb.value > res.log_bh) res.log_bh = bb.value;
            if (strcmp(res.log_status, "GREENING") != 0) {
                res.log_status = "HEALTHY";
                res.bx = bb.x; res.by = bb.y;
                res.bw = bb.width; res.bh_dim = bb.height;
            }
        } else {
            if (bb.value > res.log_bo) res.log_bo = bb.value;
            if (strcmp(res.log_status, "GREENING") != 0 && strcmp(res.log_status, "HEALTHY") != 0) {
                res.log_status = "OTHER";
                res.bx = bb.x; res.by = bb.y;
                res.bw = bb.width; res.bh_dim = bb.height;
            }
        }
    }

    res.display_conf = 0.0f;
    res.above_threshold = false;

    if (res.log_bg >= CONFIDENCE_THRESHOLD && res.log_bg >= res.log_bh && res.log_bg >= res.log_bo) {
        res.disp_status = "GREENING"; res.display_conf = res.log_bg; res.above_threshold = true;
    } else if (res.log_bh >= CONFIDENCE_THRESHOLD && res.log_bh >= res.log_bo) {
        res.disp_status = "HEALTHY";  res.display_conf = res.log_bh; res.above_threshold = true;
    } else if (res.log_bo >= CONFIDENCE_THRESHOLD) {
        res.disp_status = "OTHER";    res.display_conf = res.log_bo; res.above_threshold = true;
    }

    res.pipeline_ms = (int)(millis() - pipeline_start);
    res.success = true;
    
    // Remember to free res.crop_jpg_buf later!
    return res;
}

void InferenceManager::printDebugStats(const InferenceResult& res, int sd_write_ms, uint32_t frame_counter, uint32_t session_id) {
    Serial.println(F("\n========== DEBUG STATS =========="));

    Serial.printf("[MEM] Free PSRAM        : %7u bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    Serial.printf("[MEM] PSRAM largest blk : %7u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    Serial.printf("[MEM] Free heap         : %7u bytes\n", heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
    Serial.printf("[MEM] Heap largest blk  : %7u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        
    Serial.printf("[MEM] Arena size        : %7u bytes  (%.1f KB)\n",
        EI_CLASSIFIER_TFLITE_LEARN_796945_10_ARENA_SIZE,
        EI_CLASSIFIER_TFLITE_LEARN_796945_10_ARENA_SIZE / 1024.0f);

    Serial.printf("[TIME] Inference only   : %5d ms\n", res.inference_ms);
    Serial.printf("[TIME] Full pipeline    : %5d ms  (~%.1f FPS)\n", res.pipeline_ms, 1000.0f / res.pipeline_ms);
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    Serial.printf("[TIME] Anomaly          : %5d ms\n", global_ei_result.timing.anomaly);
#endif
    Serial.printf("[TIME] DSP              : %5d ms\n", global_ei_result.timing.dsp);
    Serial.printf("[TIME] Classification   : %5d ms\n", global_ei_result.timing.classification);

    Serial.printf("[DATA] Raw JPEG size    : %7u bytes  (%.1f KB)\n", res.raw_jpg_len, res.raw_jpg_len / 1024.0f);
    Serial.printf("[DATA] Crop JPEG size   : %7u bytes  (%.1f KB)\n", res.crop_jpg_len, res.crop_jpg_len / 1024.0f);
    Serial.printf("[DATA] SD write time    : %5d ms\n", sd_write_ms);
    Serial.printf("[DATA] Frame #          : %lu\n", frame_counter);
    Serial.printf("[DATA] Session          : %lu\n", session_id);

    Serial.printf("[DET]  BB count         : %u\n", global_ei_result.bounding_boxes_count);
    for (uint32_t i = 0; i < global_ei_result.bounding_boxes_count; i++) {
        auto bb = global_ei_result.bounding_boxes[i];
        if (bb.value == 0) continue;
        Serial.printf("[DET]  [%u] %-10s  conf: %.4f  x:%d y:%d w:%d h:%d\n",
            i, bb.label, bb.value, bb.x, bb.y, bb.width, bb.height);
    }
    if (global_ei_result.bounding_boxes_count == 0) {
        Serial.println("[DET]  No detections");
    }

    Serial.println(F("=================================\n"));
}
