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

    int total = 0, greening = 0, healthy = 0, other_count = 0, nothing_count = 0;
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
        String bbx = (col > 9) ? fields[9] : "-";
        String bby = (col > 10) ? fields[10] : "-";
        String bbw = (col > 11) ? fields[11] : "-";
        String bbh = (col > 12) ? fields[12] : "-";

        total++;
        if (status == "GREENING") greening++;
        else if (status == "HEALTHY") healthy++;
        else if (status == "OTHER") other_count++;
        else nothing_count++;

        String rowClass = "";
        String badgeClass = "bg-neutral";
        if (status == "GREENING") {
            rowClass = " style='background: rgba(254,226,226,0.3)'";
            badgeClass = "bg-red";
        }
        else if (status == "HEALTHY") {
            rowClass = " style='background: rgba(209,250,229,0.3)'";
            badgeClass = "bg-green";
        }
        else if (status == "OTHER") {
            rowClass = " style='background: rgba(254,243,199,0.3)'";
            badgeClass = "bg-orange";
        }
        String aboveText = (above == "1" || above.equalsIgnoreCase("true")) ? "YES" : "NO";

        rows += "<tr" + rowClass + "><td>" + frame + "</td><td>" + ts +
                "</td><td><span class='badge " + badgeClass + "'>" + status + "</span></td><td>" + aboveText + 
                "</td><td>" + cg + "</td><td>" + ch + "</td><td>" + co + "</td><td>" + inf + " ms</td><td>" + 
                bbx + "</td><td>" + bby + "</td><td>" + bbw + "</td><td>" + bbh + 
                "</td><td><a href='/image?path=" + imgPath +
                "' class='img-link' target='_blank'>View Image</a></td></tr>\n";
    }
    csvFile.close();

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HLB Monitor - Session {SID}</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #f3f4f6;
            --text-main: #1f2937;
            --text-muted: #6b7280;
            --card-bg: rgba(255, 255, 255, 0.7);
            --card-border: rgba(255, 255, 255, 0.5);
            --primary: #10b981;
            --danger: #ef4444;
            --neutral: #64748b;
        }
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #e0f2fe 0%, #f0fdf4 100%);
            color: var(--text-main);
            margin: 0;
            padding: 2rem;
            min-height: 100vh;
            box-sizing: border-box;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 2rem;
        }
        h1 {
            font-size: 2rem;
            font-weight: 700;
            margin: 0;
            color: #065f46;
            letter-spacing: -0.02em;
        }
        .glass-card {
            background: var(--card-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--card-border);
            border-radius: 1rem;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.05), 0 2px 4px -1px rgba(0, 0, 0, 0.03);
            padding: 1.5rem;
            margin-bottom: 2rem;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 1rem;
        }
        .stat-box {
            text-align: center;
            padding: 1rem;
            border-radius: 0.75rem;
            background: rgba(255, 255, 255, 0.5);
        }
        .stat-value {
            font-size: 1.8rem;
            font-weight: 700;
            margin-bottom: 0.25rem;
        }
        .stat-label {
            font-size: 0.875rem;
            color: var(--text-muted);
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .text-green { color: var(--primary); }
        .text-red { color: var(--danger); }
        .text-orange { color: #f59e0b; }
        .text-neutral { color: var(--neutral); }
        
        .actions {
            display: flex;
            gap: 1rem;
            margin-bottom: 1.5rem;
        }
        .btn {
            display: inline-block;
            padding: 0.6rem 1.2rem;
            border-radius: 9999px;
            font-weight: 600;
            font-size: 0.875rem;
            text-decoration: none;
            transition: all 0.2s ease;
            cursor: pointer;
            border: none;
        }
        .btn-primary {
            background: var(--primary);
            color: white;
            box-shadow: 0 2px 4px rgba(16, 185, 129, 0.2);
        }
        .btn-primary:hover {
            background: #059669;
            transform: translateY(-1px);
            box-shadow: 0 4px 6px rgba(16, 185, 129, 0.3);
        }
        .btn-secondary {
            background: white;
            color: var(--text-main);
            border: 1px solid #e5e7eb;
        }
        .btn-secondary:hover {
            background: #f9fafb;
            transform: translateY(-1px);
            box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        
        .table-container {
            overflow-x: auto;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            text-align: left;
            font-size: 0.95rem;
        }
        th {
            font-weight: 600;
            color: var(--text-muted);
            padding: 1rem;
            border-bottom: 2px solid #e5e7eb;
        }
        td {
            padding: 1rem;
            border-bottom: 1px solid #f3f4f6;
            transition: background-color 0.15s ease;
        }
        tr:hover td {
            background-color: rgba(255, 255, 255, 0.8);
        }
        .badge {
            display: inline-block;
            padding: 0.25rem 0.75rem;
            border-radius: 9999px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
        }
        .bg-green { background: #d1fae5; color: #065f46; }
        .bg-red { background: #fee2e2; color: #991b1b; }
        .bg-neutral { background: #f1f5f9; color: #334155; }
        
        .img-link {
            color: var(--primary);
            text-decoration: none;
            font-weight: 500;
        }
        .img-link:hover { text-decoration: underline; }
        .footer {
            margin-top: 2rem;
            text-align: center;
            font-size: 0.8rem;
            color: var(--text-muted);
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>HLB Detection Monitor</h1>
            <div class="btn btn-secondary">Session: S{SID}</div>
        </div>
        
        <div class="glass-card stats-grid">
            <div class="stat-box">
                <div class="stat-value">{TOTAL}</div>
                <div class="stat-label">Total Frames</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-red">{GREENING}</div>
                <div class="stat-label">Greening Detected</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-green">{HEALTHY}</div>
                <div class="stat-label">Healthy Detected</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-orange">{OTHER}</div>
                <div class="stat-label">Other</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-neutral">{NOTHING}</div>
                <div class="stat-label">Nothing</div>
            </div>
        </div>

        <div class="actions">
            <a href="/csv?session={SID}" class="btn btn-primary">Download CSV</a>
            <a href="/sessions" class="btn btn-secondary">Browse All Sessions</a>
        </div>

        <div class="glass-card table-container">
            <table>
                <thead>
                    <tr>
                        <th>Frame</th>
                        <th>Time (ms)</th>
                        <th>Status</th>
                        <th>Above</th>
                        <th>Conf G</th>
                        <th>Conf H</th>
                        <th>Conf O</th>
                        <th>Latency</th>
                        <th>x</th>
                        <th>y</th>
                        <th>w</th>
                        <th>h</th>
                        <th>Image</th>
                    </tr>
                </thead>
                <tbody>
                    {ROWS}
                </tbody>
            </table>
        </div>
        
        <div class="footer">
            Long-press touch sensor to exit WiFi mode.
        </div>
    </div>
</body>
</html>
)rawliteral";

    html.replace("{SID}", String(SDLogger::getSessionId()));
    html.replace("{TOTAL}", String(total));
    html.replace("{GREENING}", String(greening));
    html.replace("{HEALTHY}", String(healthy));
    html.replace("{OTHER}", String(other_count));
    html.replace("{NOTHING}", String(nothing_count));
    html.replace("{ROWS}", rows);

    server.send(200, "text/html", html);
}

void WebServerManager::serveSessions() {
    if (!SDLogger::isReady()) { server.send(503, "text/plain", "SD not available"); return; }

    File root = SD.open("/");
    if (!root) { server.send(500, "text/plain", "Cannot open SD root"); return; }

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>All Sessions</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #e0f2fe 0%, #f0fdf4 100%);
            color: #1f2937;
            margin: 0;
            padding: 2rem;
            min-height: 100vh;
            box-sizing: border-box;
        }
        .container {
            max-width: 1000px;
            margin: 0 auto;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 2rem;
        }
        h1 {
            font-size: 2rem;
            font-weight: 700;
            margin: 0;
            color: #065f46;
            letter-spacing: -0.02em;
        }
        .btn-secondary {
            display: inline-block;
            padding: 0.6rem 1.2rem;
            border-radius: 9999px;
            font-weight: 600;
            font-size: 0.875rem;
            text-decoration: none;
            background: white;
            color: #1f2937;
            border: 1px solid #e5e7eb;
            transition: all 0.2s ease;
        }
        .btn-secondary:hover {
            background: #f9fafb;
            transform: translateY(-1px);
            box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        .session-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
            gap: 1.5rem;
        }
        .session-card {
            background: rgba(255, 255, 255, 0.7);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid rgba(255, 255, 255, 0.5);
            border-radius: 1rem;
            padding: 1.5rem;
            text-align: center;
            text-decoration: none;
            color: #1f2937;
            transition: all 0.2s ease;
            box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05);
        }
        .session-card:hover {
            transform: translateY(-3px);
            box-shadow: 0 10px 15px -3px rgba(0,0,0,0.1);
            background: rgba(255, 255, 255, 0.9);
        }
        .session-icon {
            font-size: 2rem;
            margin-bottom: 1rem;
        }
        .session-title {
            font-size: 1.25rem;
            font-weight: 600;
            margin-bottom: 0.5rem;
        }
        .session-subtitle {
            font-size: 0.875rem;
            color: #6b7280;
        }
        @media (max-width: 640px) {
            body { padding: 1rem; }
            .header { flex-direction: column; align-items: flex-start; gap: 1rem; }
            .header .btn-secondary { width: 100%; text-align: center; box-sizing: border-box; }
            .session-grid { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>All Sessions</h1>
            <a href="/" class="btn-secondary">Back to Dashboard</a>
        </div>
        <div class="session-grid">
)rawliteral";

    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            String name = String(entry.name());
            if (name.startsWith("S")) {
                uint32_t sid = (uint32_t)name.substring(1).toInt();
                html += "<a href='/session?session=" + String(sid) + "' class='session-card'>"
                        "<div class='session-icon'>📁</div>"
                        "<div class='session-title'>Session " + String(sid) + "</div>"
                        "<div class='session-subtitle'>Download CSV Data</div>"
                        "</a>";
            }
        }
        entry.close();
        entry = root.openNextFile();
    }
    root.close();

    html += "</div></div></body></html>";
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
    int nothing_count = 0;

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
        else if (status == "OTHER") other_count++;
        else nothing_count++;

        String rowClass = "";
        String badgeClass = "bg-neutral";
        if (status == "GREENING") {
            rowClass = " style='background: rgba(254,226,226,0.3)'";
            badgeClass = "bg-red";
        }
        else if (status == "HEALTHY") {
            rowClass = " style='background: rgba(209,250,229,0.3)'";
            badgeClass = "bg-green";
        }
        else if (status == "OTHER") {
            rowClass = " style='background: rgba(254,243,199,0.3)'";
            badgeClass = "bg-orange";
        }
        String aboveText = (above == "1" || above.equalsIgnoreCase("true")) ? "YES" : "NO";

        rows += "<tr" + rowClass + "><td>" + frame + "</td><td>" + ts +
                "</td><td><span class='badge " + badgeClass + "'>" + status + "</span></td><td>" + aboveText + 
                "</td><td>" + cg + "</td><td>" + ch + "</td><td>" + co + "</td><td>" + inf + " ms</td><td>" + 
                bbx + "</td><td>" + bby + "</td><td>" + bbw + "</td><td>" + bbh + 
                "</td><td><a href='/image?path=" + imgPath +
                "' class='img-link' target='_blank'>View Image</a></td></tr>\n";
    }
    csvFile.close();

    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Session {SID} Details</title>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
    <style>
        :root {
            --bg-color: #f3f4f6;
            --text-main: #1f2937;
            --text-muted: #6b7280;
            --card-bg: rgba(255, 255, 255, 0.7);
            --card-border: rgba(255, 255, 255, 0.5);
            --primary: #10b981;
            --danger: #ef4444;
            --neutral: #64748b;
        }
        body {
            font-family: 'Inter', sans-serif;
            background: linear-gradient(135deg, #e0f2fe 0%, #f0fdf4 100%);
            color: var(--text-main);
            margin: 0;
            padding: 2rem;
            min-height: 100vh;
            box-sizing: border-box;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 2rem;
        }
        h1 {
            font-size: 2rem;
            font-weight: 700;
            margin: 0;
            color: #065f46;
            letter-spacing: -0.02em;
        }
        .glass-card {
            background: var(--card-bg);
            backdrop-filter: blur(16px);
            -webkit-backdrop-filter: blur(16px);
            border: 1px solid var(--card-border);
            border-radius: 1rem;
            box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.05), 0 2px 4px -1px rgba(0, 0, 0, 0.03);
            padding: 1.5rem;
            margin-bottom: 2rem;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 1rem;
        }
        .stat-box {
            text-align: center;
            padding: 1rem;
            border-radius: 0.75rem;
            background: rgba(255, 255, 255, 0.5);
        }
        .stat-value {
            font-size: 1.8rem;
            font-weight: 700;
            margin-bottom: 0.25rem;
        }
        .stat-label {
            font-size: 0.875rem;
            color: var(--text-muted);
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .text-green { color: var(--primary); }
        .text-red { color: var(--danger); }
        .text-orange { color: #f59e0b; }
        .text-neutral { color: var(--neutral); }
        
        .actions {
            display: flex;
            gap: 1rem;
            margin-bottom: 1.5rem;
        }
        .btn {
            display: inline-block;
            padding: 0.6rem 1.2rem;
            border-radius: 9999px;
            font-weight: 600;
            font-size: 0.875rem;
            text-decoration: none;
            transition: all 0.2s ease;
            cursor: pointer;
            border: none;
        }
        .btn-primary {
            background: var(--primary);
            color: white;
            box-shadow: 0 2px 4px rgba(16, 185, 129, 0.2);
        }
        .btn-primary:hover {
            background: #059669;
            transform: translateY(-1px);
            box-shadow: 0 4px 6px rgba(16, 185, 129, 0.3);
        }
        .btn-secondary {
            background: white;
            color: var(--text-main);
            border: 1px solid #e5e7eb;
        }
        .btn-secondary:hover {
            background: #f9fafb;
            transform: translateY(-1px);
            box-shadow: 0 2px 4px rgba(0,0,0,0.05);
        }
        
        .table-container {
            overflow-x: auto;
        }
        table {
            width: 100%;
            border-collapse: collapse;
            text-align: left;
            font-size: 0.95rem;
            min-width: 960px;
        }
        th {
            font-weight: 600;
            color: var(--text-muted);
            padding: 1rem;
            border-bottom: 2px solid #e5e7eb;
            white-space: nowrap;
        }
        td {
            padding: 1rem;
            border-bottom: 1px solid #f3f4f6;
            transition: background-color 0.15s ease;
            white-space: nowrap;
        }
        tr:hover td {
            background-color: rgba(255, 255, 255, 0.8);
        }
        .badge {
            display: inline-block;
            padding: 0.25rem 0.75rem;
            border-radius: 9999px;
            font-size: 0.75rem;
            font-weight: 600;
            text-transform: uppercase;
        }
        .bg-green { background: #d1fae5; color: #065f46; }
        .bg-red { background: #fee2e2; color: #991b1b; }
        .bg-orange { background: #fef3c7; color: #b45309; }
        .bg-neutral { background: #f1f5f9; color: #334155; }
        
        .img-link {
            color: var(--primary);
            text-decoration: none;
            font-weight: 500;
        }
        .img-link:hover { text-decoration: underline; }
        .footer {
            margin-top: 2rem;
            text-align: center;
            font-size: 0.8rem;
            color: var(--text-muted);
        }
        @media (max-width: 640px) {
            body { padding: 1rem; }
            .header { flex-direction: column; align-items: flex-start; gap: 1rem; }
            .header .actions { width: 100%; flex-direction: column; gap: 0.5rem; }
            .header .actions .btn { width: 100%; text-align: center; box-sizing: border-box; }
            .stats-grid { grid-template-columns: 1fr 1fr; }
            .glass-card { padding: 1rem; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Session S{SID}</h1>
            <div class="actions" style="margin-bottom:0;">
                <a href="/sessions" class="btn btn-secondary">Back to Sessions</a>
                <a href="/csv?session={SID}" class="btn btn-primary">Download CSV</a>
            </div>
        </div>
        
        <div class="glass-card stats-grid">
            <div class="stat-box">
                <div class="stat-value">{TOTAL}</div>
                <div class="stat-label">Total Frames</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-red">{GREENING}</div>
                <div class="stat-label">Greening Detected</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-green">{HEALTHY}</div>
                <div class="stat-label">Healthy Detected</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-orange">{OTHER}</div>
                <div class="stat-label">Other</div>
            </div>
            <div class="stat-box">
                <div class="stat-value text-neutral">{NOTHING}</div>
                <div class="stat-label">Nothing</div>
            </div>
        </div>

        <div class="glass-card table-container">
            <table>
                <thead>
                    <tr>
                        <th>Frame</th>
                        <th>Time (ms)</th>
                        <th>Status</th>
                        <th>Above</th>
                        <th>Conf G</th>
                        <th>Conf H</th>
                        <th>Conf O</th>
                        <th>Latency</th>
                        <th>x</th>
                        <th>y</th>
                        <th>w</th>
                        <th>h</th>
                        <th>Image</th>
                    </tr>
                </thead>
                <tbody>
                    {ROWS}
                </tbody>
            </table>
        </div>
        
        <div class="footer">
            Tap an image link to open the JPEG. Use the session summary above to quickly compare detections.
        </div>
    </div>
</body>
</html>
)rawliteral";

    html.replace("{SID}", String(sid));
    html.replace("{TOTAL}", String(total));
    html.replace("{GREENING}", String(greening));
    html.replace("{HEALTHY}", String(healthy));
    html.replace("{OTHER}", String(other_count));
    html.replace("{NOTHING}", String(nothing_count));
    html.replace("{ROWS}", rows);

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
