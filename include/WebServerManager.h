#pragma once
#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

class WebServerManager {
public:
    static void handleTouch(bool& wifiMode, unsigned long& touchStart, bool& touchPressed, unsigned long& lastTouchDebug);
    static void startWiFiMode();
    static void stopWiFiMode();
    static void handleClient();
    static bool isWiFiMode();

private:
    static void serveDashboard();
    static void serveSessions();
    static void serveSessionDetails();
    static void serveCsv();
    static void serveImage();
    
    static WebServer server;
    static bool wifi_mode;
};

#endif // WEB_SERVER_MANAGER_H
