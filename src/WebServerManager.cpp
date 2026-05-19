#include "WebServerManager.h"
#include "Config.h"
#include "DisplayManager.h"
#include "SDLogger.h"
#include <SD.h>

WebServer WebServerManager::server(80);
bool WebServerManager::wifi_mode = false;

void WebServerManager::handleTouch(bool& wifiMode, unsigned long& touchStart, bool& touchPressed, unsigned long& lastTouchDebug) {
    uint16_t val = touchRead(TOUCH_PIN);

    if (millis() - lastTouchDebug > 500) {
        lastTouchDebug = millis();
    }

    bool touched = (val > TOUCH_THRESHOLD);

    if (touched && !touchPressed) {
        touchPressed = true;
        touchStart = millis();
        Serial.println("Touch detected");
        DisplayManager::showMessage("TOUCH DETECTED", "Hold to toggle");
    }
    else if (touched && touchPressed) {
        unsigned long held_time = millis() - touchStart;
        static unsigned long last_print = 0;
        if (millis() - last_print > 250) {
            last_print = millis();
            Serial.printf("Holding... %lu ms / %d ms\n", held_time, TOUCH_HOLD_MS);
            
            char line2[32];
            snprintf(line2, sizeof(line2), "%lu / %d ms", held_time, TOUCH_HOLD_MS);
            DisplayManager::showMessage("HOLDING...", line2);
        }
    }
    else if (!touched && touchPressed) {
        touchPressed = false;
        unsigned long held_time = millis() - touchStart;
        Serial.printf("Touch released after %lu ms\n", held_time);
        
        char line2[32];
        snprintf(line2, sizeof(line2), "%lu ms", held_time);
        DisplayManager::showMessage("TOUCH RELEASED", line2);

        if (held_time >= TOUCH_HOLD_MS) {
            wifiMode = !wifiMode;
            if (wifiMode) {
                Serial.println("=== WIFI MODE ENABLED ===");
                DisplayManager::showMessage("STARTING WIFI...");
                startWiFiMode();
            } else {
                Serial.println("=== STOPPING WIFI MODE ===");
                stopWiFiMode();
                Serial.println("Returned to inference mode");
            }
        } else {
            Serial.println("Touch too short");
            DisplayManager::showMessage("HOLD LONGER");
            delay(800);
        }
    }
}

void WebServerManager::startWiFiMode() {
    wifi_mode = true;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);

    DisplayManager::showMessage("CONNECTING WiFi...");

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi FAILED — returning to inference mode");
        wifi_mode = false;
        DisplayManager::showMessage("WIFI FAILED", "Check credentials");
        delay(2000);
        return;
    }

    IPAddress ip = WiFi.localIP();
    Serial.printf("\nWiFi connected. Open http://%s\r\n", ip.toString().c_str());

    server.on("/", HTTP_GET, serveDashboard);
    server.on("/sessions", HTTP_GET, serveSessions);
    server.on("/csv", HTTP_GET, serveCsv);
    server.on("/image", HTTP_GET, serveImage);
    server.begin();

    char line2[32];
    snprintf(line2, sizeof(line2), "http://%s", ip.toString().c_str());
    DisplayManager::showMessage("WIFI MODE", line2);
}

void WebServerManager::stopWiFiMode() {
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_mode = false;
    Serial.println("WiFi disconnected. Resuming inference.");
    
    DisplayManager::showMessage("INFERENCE MODE");
    delay(1000);
}

void WebServerManager::handleClient() {
    if (wifi_mode) {
        server.handleClient();
    }
}

bool WebServerManager::isWiFiMode() {
    return wifi_mode;
}

void WebServerManager::serveDashboard() {
    if (!SDLogger::isReady()) {
        server.send(503, "text/plain", "SD card not available");
        return;
    }

    File csvFile = SD.open(SDLogger::getCsvPath(), FILE_READ);
    if (!csvFile) {
        server.send(404, "text/plain", "Log file not found");
        return;
    }

    int total = 0, greening = 0, healthy = 0, other_count = 0;
    String rows = "";
    bool header_skipped = false;

    while (csvFile.available()) {
        String line = csvFile.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        if (!header_skipped) { header_skipped = true; continue; } 

        int col = 0;
        String fields[13];
        int start = 0;
        for (int i = 0; i <= (int)line.length(); i++) {
            if (i == (int)line.length() || line[i] == ',') {
                fields[col++] = line.substring(start, i);
                start = i + 1;
                if (col >= 13) break;
            }
        }

        if (col < 5) continue;

        String frame = fields[0];
        String ts = fields[1];
        String status = fields[2];
        String above = fields[3]; 
        String imgPath = fields[4];
        String cg = (col > 5) ? fields[5] : "-";
        String ch = (col > 6) ? fields[6] : "-";

        total++;
        if (status == "GREENING") greening++;
        else if (status == "HEALTHY") healthy++;
        else other_count++;

        String rowClass = "";
        if (status == "GREENING") rowClass = " style='background:#ffe0e0'";
        else if (status == "HEALTHY") rowClass = " style='background:#e0ffe0'";

        rows += "<tr" + rowClass + "><td>" + frame + "</td><td>" + ts +
                "</td><td><b>" + status + "</b></td><td>" + cg +
                "</td><td>" + ch +
                "</td><td><a href='/image?path=" + imgPath +
                "' target='_blank'>view</a></td></tr>\n";
    }
    csvFile.close();

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='10'>"
        "<title>HLB Monitor - Session " + String(SDLogger::getSessionId()) + "</title>"
        "<style>"
        "body{font-family:sans-serif;margin:16px;}"
        "h1{color:#2c5f2e;}"
        "table{border-collapse:collapse;width:100%;font-size:13px;}"
        "th,td{border:1px solid #ccc;padding:4px 8px;text-align:left;}"
        "th{background:#2c5f2e;color:#fff;}"
        ".badge{display:inline-block;padding:2px 8px;border-radius:8px;"
        "  color:#fff;font-weight:bold;}"
        ".g{background:#c0392b;} .h{background:#27ae60;} .o{background:#7f8c8d;}"
        "</style></head><body>"
        "<h1>HLB Detection Monitor</h1>"
        "<p><b>Session:</b> S" + String(SDLogger::getSessionId()) +
        " &nbsp;|&nbsp; <b>Total frames:</b> " + String(total) +
        " &nbsp;|&nbsp; "
        "<span class='badge g'>Greening: " + String(greening) + "</span> "
        "<span class='badge h'>Healthy: "  + String(healthy)  + "</span> "
        "<span class='badge o'>Other: "    + String(other_count) + "</span>"
        "</p>"
        "<p><a href='/sessions'>Browse all sessions</a> "
        "&nbsp;|&nbsp; <a href='/csv?session=" + String(SDLogger::getSessionId()) +
        "'>Download CSV</a></p>"
        "<table><tr>"
        "<th>Frame</th><th>Time (ms)</th><th>Status</th>"
        "<th>Conf G</th><th>Conf H</th><th>Image</th>"
        "</tr>\n" + rows + "</table>"
        "<p style='color:#888;font-size:11px'>Auto-refreshes every 10 s. "
        "Long-press touch to exit WiFi mode.</p>"
        "</body></html>";

    server.send(200, "text/html", html);
}

void WebServerManager::serveSessions() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    File root = SD.open("/");
    if (!root) { server.send(500, "text/plain", "Cannot open SD root"); return; }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<title>Sessions</title>"
        "<style>body{font-family:sans-serif;margin:16px;}"
        "a{display:block;margin:4px 0;}</style></head><body>"
        "<h2>All Sessions</h2>";

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("S")) {
                uint32_t sid = (uint32_t)name.substring(1).toInt();
                html += "<a href='/csv?session=" + String(sid) +
                        "'>Session " + name + " — Download CSV</a>";
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    html += "<br><a href='/'>Back to dashboard</a></body></html>";
    server.send(200, "text/html", html);
}

void WebServerManager::serveCsv() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    uint32_t sid = server.hasArg("session")
                   ? (uint32_t)server.arg("session").toInt()
                   : SDLogger::getSessionId();

    char path[32];
    snprintf(path, sizeof(path), CSV_FILE_FMT, sid);

    File f = SD.open(path, FILE_READ);
    if (!f) { server.send(404, "text/plain", "CSV not found"); return; }

    char disposition[48];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"S%05lu.csv\"", sid);
    server.sendHeader("Content-Disposition", disposition);
    server.streamFile(f, "text/csv");
    f.close();
}

void WebServerManager::serveImage() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        return;
    }

    String path = server.arg("path");

    if (path.indexOf("..") >= 0) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) { server.send(404, "text/plain", "Image not found"); return; }

    server.streamFile(f, "image/jpeg");
    f.close();
}
