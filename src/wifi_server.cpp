#include "wifi_server.h"
#include "config.h"
#include "display.h"
#include "sd_logging.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <FS.h>

static bool wifi_active = false;
static WebServer server(80);

// ─────────────────────────────────────────────────────────────────────────────
// HTTP HANDLERS
// ─────────────────────────────────────────────────────────────────────────────

void handle_dashboard() {
    if (!sd_is_ready()) {
        server.send(503, "text/plain", "SD card not available");
        return;
    }

    // Read CSV and build table
    const char* csv_path_str = sd_get_session_folder();
    char full_csv_path[32];
    snprintf(full_csv_path, sizeof(full_csv_path), "%s/log.csv", csv_path_str);
    
    File csvFile = SD.open(full_csv_path, FILE_READ);
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

    uint32_t session_id = sd_get_session_id();
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
        "<meta http-equiv='refresh' content='10'>"
        "<title>HLB Monitor - Session " + String(session_id) + "</title>"
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
        "<p><b>Session:</b> S" + String(session_id) +
        " &nbsp;|&nbsp; <b>Total frames:</b> " + String(total) +
        " &nbsp;|&nbsp; "
        "<span class='badge g'>Greening: " + String(greening) + "</span> "
        "<span class='badge h'>Healthy: "  + String(healthy)  + "</span> "
        "<span class='badge o'>Other: "    + String(other_count) + "</span>"
        "</p>"
        "<p><a href='/sessions'>Browse all sessions</a> "
        "&nbsp;|&nbsp; <a href='/csv?session=" + String(session_id) +
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

void handle_sessions() {
    if (!sd_is_ready()) { 
        server.send(503, "text/plain", "SD not available"); 
        return; 
    }

    File root = SD.open("/");
    if (!root) { 
        server.send(500, "text/plain", "Cannot open SD root"); 
        return; 
    }

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

void handle_csv() {
    if (!sd_is_ready()) { 
        server.send(503, "text/plain", "SD not available"); 
        return; 
    }

    uint32_t sid = server.hasArg("session")
                   ? (uint32_t)server.arg("session").toInt()
                   : sd_get_session_id();

    char path[32];
    snprintf(path, sizeof(path), CSV_FILE_FMT, sid);

    File f = SD.open(path, FILE_READ);
    if (!f) { 
        server.send(404, "text/plain", "CSV not found"); 
        return; 
    }

    char disposition[48];
    snprintf(disposition, sizeof(disposition), "attachment; filename=\"S%05lu.csv\"", sid);
    server.sendHeader("Content-Disposition", disposition);
    server.streamFile(f, "text/csv");
    f.close();
}

void handle_image() {
    if (!sd_is_ready()) { 
        server.send(503, "text/plain", "SD not available"); 
        return; 
    }

    if (!server.hasArg("path")) {
        server.send(400, "text/plain", "Missing 'path' parameter");
        return;
    }

    String path = server.arg("path");

    // Path traversal guard
    if (path.indexOf("..") >= 0) {
        server.send(403, "text/plain", "Forbidden");
        return;
    }

    File f = SD.open(path, FILE_READ);
    if (!f) { 
        server.send(404, "text/plain", "Image not found"); 
        return; 
    }

    server.streamFile(f, "image/jpeg");
    f.close();
}

// ─────────────────────────────────────────────────────────────────────────────
// PUBLIC API
// ─────────────────────────────────────────────────────────────────────────────

bool wifi_start() {
    wifi_active = true;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);

    display_show_message("CONNECTING WiFi...");

    Serial.print("Connecting to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi FAILED — returning to inference mode");
        wifi_active = false;
        display_show_message("WIFI FAILED", "Check credentials");
        delay(2000);
        return false;
    }

    IPAddress ip = WiFi.localIP();
    Serial.printf("\nWiFi connected. Open http://%s\r\n", ip.toString().c_str());

    server.on("/",         HTTP_GET, handle_dashboard);
    server.on("/sessions", HTTP_GET, handle_sessions);
    server.on("/csv",      HTTP_GET, handle_csv);
    server.on("/image",    HTTP_GET, handle_image);
    server.begin();

    display_show_wifi_connected(ip.toString().c_str());
    
    return true;
}

void wifi_stop() {
    server.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifi_active = false;
    Serial.println("WiFi disconnected. Resuming inference.");
    
    delay(500);
    display_show_message("INFERENCE MODE");
    delay(1000);
}

void wifi_handle_requests() {
    if (wifi_active) {
        server.handleClient();
    }
}

bool wifi_is_active() {
    return wifi_active;
}
