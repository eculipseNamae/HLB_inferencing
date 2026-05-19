#pragma once
#ifndef CAMERA_MANAGER_H
#define CAMERA_MANAGER_H

#include <Arduino.h>
#include "esp_camera.h"

class CameraManager {
public:
    static bool begin();
    static camera_fb_t* captureFrame();
    static void releaseFrame(camera_fb_t* fb);
};

#endif // CAMERA_MANAGER_H
