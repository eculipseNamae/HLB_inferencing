#ifndef CONFIG_H
#define CONFIG_H

/* ================= PINS & CONFIG ================= */
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

#define SD_CS_PIN         21
#define SD_SCK_PIN        7
#define SD_MISO_PIN       8
#define SD_MOSI_PIN       9

#define LED_PIN           21

// OLED Config
#define SCREEN_WIDTH      128
#define SCREEN_HEIGHT     32
#define OLED_RESET        -1
#define SCREEN_ADDRESS    0x3C

#define INFERENCE_INTERVAL_MS   3000
#define CONFIDENCE_THRESHOLD    0.70f

#define RAW_WIDTH               320
#define RAW_HEIGHT              240
#define RAW_RGB_SIZE            (RAW_WIDTH * RAW_HEIGHT * 3)

// Session ID config
#define SESSION_FOLDER_FMT   "/S%05lu"
#define IMAGE_FILE_FMT       "/S%05lu/%04lu.jpg"
#define CSV_FILE_FMT         "/S%05lu/log.csv"

// WiFi config
#define WIFI_STA_SSID        "GlobeAtHome_C9A2D_2.4"
#define WIFI_STA_PASS        "Eu8Nhg3p"
#define TOUCH_PIN            2
#define TOUCH_THRESHOLD      22000
#define TOUCH_HOLD_MS        2000

#endif
