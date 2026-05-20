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
    server.on("/session", HTTP_GET, serveSessionDetails);
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
        String co = (col > 7) ? fields[7] : "-";
        String inf = (col > 8) ? fields[8] : "-";

        total++;
        if (status == "GREENING") greening++;
        else if (status == "HEALTHY") healthy++;
        else other_count++;

        String statusClass = status;
        statusClass.toLowerCase();
        String badge = "<span class='status-badge " + statusClass + "'>" + status + "</span>";
        String aboveText = (above == "1" || above.equalsIgnoreCase("true")) ? "YES" : "NO";

        rows += "<tr><td>" + frame + "</td><td>" + ts + "</td><td>" + badge +
                "</td><td>" + aboveText + "</td><td>" + cg + "</td><td>" + ch +
                "</td><td>" + co + "</td><td>" + inf + " ms</td><td>" +
                "<a class='link' href='/image?path=" + imgPath +
                "' target='_blank'>view</a></td></tr>\n";
    }
    csvFile.close();

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>HLB Monitor - Session " + String(SDLogger::getSessionId()) + "</title>"
        "<style>"
        "body{font-family:Helvetica,Arial,sans-serif;margin:0;padding:0;background:#f6f8fb;color:#1f2937;}"
        ".page{max-width:1024px;margin:0 auto;padding:16px;}"
        ".header{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:20px;}"
        ".summary{display:flex;flex-wrap:wrap;gap:8px;align-items:center;}"
        ".summary div{background:#ffffff;border:1px solid #d1d5db;border-radius:10px;padding:10px 14px;box-shadow:0 1px 2px rgba(0,0,0,0.05);}"
        ".title-block{min-width:220px;}"
        "h1{margin:0;font-size:1.6rem;color:#0f172a;}"
        ".actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:10px;}"
        ".button{display:inline-block;padding:10px 16px;border-radius:10px;text-decoration:none;font-weight:600;color:#fff;background:#2563eb;}"
        ".button.secondary{background:#475569;}"
        ".table-wrap{overflow-x:auto;background:#fff;border:1px solid #d1d5db;border-radius:12px;}"
        "table{border-collapse:collapse;width:100%;min-width:760px;font-size:0.94rem;background:#fff;}"
        "th,td{border-bottom:1px solid #e2e8f0;padding:12px 14px;text-align:left;vertical-align:middle;}"
        "th{background:#0f172a;color:#fff;position:sticky;top:0;z-index:2;}"
        ".status-badge{display:inline-flex;align-items:center;justify-content:center;padding:6px 10px;border-radius:999px;font-size:0.85rem;font-weight:700;color:#fff;letter-spacing:.02em;}"
        ".status-badge.greening{background:#dc2626;}"
        ".status-badge.healthy{background:#16a34a;}"
        ".status-badge.other{background:#6b7280;}"
        ".link{color:#2563eb;text-decoration:none;font-weight:600;}"
        ".link:hover{text-decoration:underline;}"
        ".footer{margin-top:18px;font-size:0.86rem;color:#475569;}"
        "@media (max-width: 860px){.header{flex-direction:column;align-items:flex-start;} .summary{flex-direction:column;} .table-wrap{overflow-x:auto;} table{font-size:0.82rem;} th,td{padding:10px 12px;}}"
        "@media (max-width: 600px){.button{width:100%;text-align:center;} .summary div{width:100%;}}"
        "</style></head><body><div class='page'>"
        "<div class='header'><div class='title-block'><h1>HLB Detection Monitor</h1>"
        "<p>Session S" + String(SDLogger::getSessionId()) + "</p></div>"
        "<div class='actions'><a class='button' href='/sessions'>Browse sessions</a>"
        "<a class='button secondary' href='/csv?session=" + String(SDLogger::getSessionId()) + "'>Download CSV</a></div></div>"
        "<div class='summary'><div><strong>Total frames</strong><div>" + String(total) + "</div></div>"
        "<div><strong>Greening</strong><div>" + String(greening) + "</div></div>"
        "<div><strong>Healthy</strong><div>" + String(healthy) + "</div></div>"
        "<div><strong>Other</strong><div>" + String(other_count) + "</div></div></div>"
        "<div class='table-wrap'><table><thead><tr>"
        "<th>Frame</th><th>Time (ms)</th><th>Status</th><th>Above</th>"
        "<th>Conf G</th><th>Conf H</th><th>Conf O</th><th>Latency</th><th>Image</th></tr></thead><tbody>" + rows + "</tbody></table></div>"
        "<p class='footer'>Auto-refreshes every 10 seconds. Long-press touch to exit WiFi mode.</p>"
        "</div></body></html>";

    server.send(200, "text/html", html);
}

void WebServerManager::serveSessions() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    File root = SD.open("/");
    if (!root) { server.send(500, "text/plain", "Cannot open SD root"); return; }

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Sessions</title>"
        "<style>body{font-family:Helvetica,Arial,sans-serif;margin:0;padding:0;background:#f6f8fb;color:#1f2937;}"
        ".page{max-width:980px;margin:0 auto;padding:16px;}"
        "h2{margin-bottom:12px;}"
        ".session-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px;}"
        ".session-card{background:#fff;border:1px solid #d1d5db;border-radius:16px;padding:16px;box-shadow:0 1px 3px rgba(15,23,42,0.08);}"

        ".session-card h3{margin:0 0 10px 0;font-size:1.1rem;}"
        ".card-actions{display:flex;flex-wrap:wrap;gap:8px;}"
        ".button{display:inline-block;padding:9px 14px;border-radius:999px;text-decoration:none;font-weight:600;color:#fff;background:#2563eb;}"
        ".button.secondary{background:#475569;}"
        ".back-link{display:inline-block;margin-top:18px;color:#2563eb;text-decoration:none;font-weight:600;}"
        "@media (max-width: 620px){.page{padding:12px;} .card-actions{flex-direction:column;}}</style></head><body><div class='page'>"
        "<h2>All Sessions</h2><div class='session-grid'>";

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("S")) {
                uint32_t sid = (uint32_t)name.substring(1).toInt();
                html += "<div class='session-card'><h3>Session " + name + "</h3>"
                        "<div class='card-actions'><a class='button' href='/session?session=" + String(sid) + "'>View session</a>"
                        "<a class='button secondary' href='/csv?session=" + String(sid) + "'>Download CSV</a></div></div>";
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    html += "</div><a class='back-link' href='/'>Back to dashboard</a></div></body></html>";
    server.send(200, "text/html", html);
}

void WebServerManager::serveSessionDetails() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    uint32_t sid = server.hasArg("session")
                   ? (uint32_t)server.arg("session").toInt()
                   : SDLogger::getSessionId();

    char csv_path[32];
    snprintf(csv_path, sizeof(csv_path), CSV_FILE_FMT, sid);

    File csvFile = SD.open(csv_path, FILE_READ);
    if (!csvFile) {
        server.send(404, "text/plain", "Session CSV not found");
        return;
    }

    String rows = "";
    bool header_skipped = false;
    int total = 0;
    int greening = 0;
    int healthy = 0;
    int other_count = 0;

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
        String co = (col > 7) ? fields[7] : "-";
        String inf = (col > 8) ? fields[8] : "-";
        String bbx = (col > 9) ? fields[9] : "-";
        String bby = (col > 10) ? fields[10] : "-";
        String bbw = (col > 11) ? fields[11] : "-";
        String bbh = (col > 12) ? fields[12] : "-";

        total++;
        if (status == "GREENING") greening++;
        else if (status == "HEALTHY") healthy++;
        else other_count++;

        String statusClass = status;
        statusClass.toLowerCase();
        String badge = "<span class='status-badge " + statusClass + "'>" + status + "</span>";
        String aboveText = (above == "1" || above.equalsIgnoreCase("true")) ? "YES" : "NO";

        rows += "<tr><td>" + frame + "</td><td>" + ts + "</td><td>" + badge +
                "</td><td>" + aboveText + "</td><td>" + cg + "</td><td>" + ch +
                "</td><td>" + co + "</td><td>" + inf + " ms</td><td>" + bbx +
                "</td><td>" + bby + "</td><td>" + bbw + "</td><td>" + bbh +
                "</td><td><a class='link' href='/image?path=" + imgPath +
                "' target='_blank'>view</a></td></tr>\n";
    }
    csvFile.close();

    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>Session " + String(sid) + "</title>"
        "<style>body{font-family:Helvetica,Arial,sans-serif;margin:0;padding:0;background:#f6f8fb;color:#1f2937;}"
        ".page{max-width:1024px;margin:0 auto;padding:16px;}"
        ".header{display:flex;flex-wrap:wrap;justify-content:space-between;align-items:flex-start;gap:12px;margin-bottom:18px;}"
        ".header h1{margin:0;font-size:1.6rem;}"
        ".header .info{display:flex;flex-wrap:wrap;gap:10px;}"
        ".info div{background:#fff;border:1px solid #d1d5db;border-radius:12px;padding:10px 14px;box-shadow:0 1px 2px rgba(0,0,0,0.05);}"
        ".actions{display:flex;flex-wrap:wrap;gap:10px;margin-top:8px;}"
        ".button{display:inline-block;padding:9px 14px;border-radius:999px;text-decoration:none;font-weight:600;color:#fff;background:#2563eb;}"
        ".button.secondary{background:#475569;}"
        ".table-wrap{overflow-x:auto;background:#fff;border:1px solid #d1d5db;border-radius:12px;}"
        "table{border-collapse:collapse;width:100%;min-width:960px;font-size:0.9rem;background:#fff;}"
        "th,td{border-bottom:1px solid #e2e8f0;padding:12px 14px;text-align:left;vertical-align:middle;}"
        "th{background:#0f172a;color:#fff;position:sticky;top:0;z-index:2;}"
        ".status-badge{display:inline-flex;align-items:center;justify-content:center;padding:6px 10px;border-radius:999px;font-size:0.85rem;font-weight:700;color:#fff;letter-spacing:.02em;}"
        ".status-badge.greening{background:#dc2626;}"
        ".status-badge.healthy{background:#16a34a;}"
        ".status-badge.other{background:#6b7280;}"
        ".link{color:#2563eb;text-decoration:none;font-weight:600;}"
        ".link:hover{text-decoration:underline;}"
        ".footer{margin-top:18px;font-size:0.88rem;color:#475569;}"
        "@media (max-width: 860px){.header{flex-direction:column;align-items:flex-start;} .info{flex-direction:column;} .table-wrap{overflow-x:auto;} table{font-size:0.82rem;} th,td{padding:10px 12px;}}"
        "@media (max-width: 620px){.button{width:100%;text-align:center;} .actions{flex-direction:column;}}</style></head><body><div class='page'>"
        "<div class='header'><div><h1>Session S" + String(sid) + "</h1>"
        "<p>Session detail and image actions</p></div>"
        "<div class='actions'><a class='button secondary' href='/sessions'>Back to sessions</a>"
        "<a class='button' href='/csv?session=" + String(sid) + "'>Download CSV</a></div></div>"
        "<div class='info'><div><strong>Total frames</strong><div>" + String(total) + "</div></div>"
        "<div><strong>Greening</strong><div>" + String(greening) + "</div></div>"
        "<div><strong>Healthy</strong><div>" + String(healthy) + "</div></div>"
        "<div><strong>Other</strong><div>" + String(other_count) + "</div></div></div>"
        "<div class='table-wrap'><table><thead><tr>"
        "<th>Frame</th><th>Time</th><th>Status</th><th>Above</th>"
        "<th>Conf G</th><th>Conf H</th><th>Conf O</th><th>Latency</th>"
        "<th>x</th><th>y</th><th>w</th><th>h</th><th>Image</th></tr></thead><tbody>" + rows + "</tbody></table></div>"
        "<p class='footer'>Tap an image link to open the JPEG. Use the session summary above to quickly compare detections.</p>"
        "</div></body></html>";

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
