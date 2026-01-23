/**
 * @file web_server.cpp
 * @brief Async web server implementation
 *
 * REST API endpoints:
 *   GET  /api/bms       - Full BMS state (all modules)
 *   GET  /api/module/N  - Single module data (N = 1-8)
 *   GET  /api/summary   - Pack summary (lowest cell, balancing status)
 *   POST /api/balancing - Toggle balancing on/off
 *   GET  /              - HTML dashboard
 */

#include "web_server.h"
#include "config.h"
#include "bms_data.h"
#include "protection.h"
#include <ESPAsyncWebServer.h>

// Web server instance on port 80
static AsyncWebServer s_server(80);

// =============================================================================
// JSON BUILDERS
// =============================================================================

/**
 * Build JSON for a single CMU module.
 */
static String buildModuleJson(int moduleIndex) {
    const CmuData& cmu = g_bmsState.modules[moduleIndex];

    String json = "{";
    json += "\"module\":" + String(moduleIndex + 1) + ",";
    json += "\"present\":" + String(cmu.present ? "true" : "false") + ",";

    // Voltages array
    json += "\"voltages\":[";
    for (int i = 0; i < CELLS_PER_MODULE; i++) {
        if (i > 0) json += ",";
        json += String(cmu.voltages[i]);
    }
    json += "],";

    // Temperatures array
    json += "\"temperatures\":[";
    for (int i = 0; i < TEMPS_PER_MODULE; i++) {
        if (i > 0) json += ",";
        // Convert raw value to temperature (divide by 1000 for degrees C)
        json += String(cmu.temperatures[i] / 1000.0, 1);
    }
    json += "],";

    // Balance status bitmask and array
    json += "\"balanceStatus\":" + String(cmu.balanceStatus) + ",";
    json += "\"balancing\":[";
    for (int i = 0; i < CELLS_PER_MODULE; i++) {
        if (i > 0) json += ",";
        json += ((cmu.balanceStatus >> i) & 1) ? "true" : "false";
    }
    json += "]";

    json += "}";
    return json;
}

/**
 * Build JSON for all modules.
 */
static String buildFullBmsJson() {
    String json = "{";
    json += "\"modules\":[";

    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (m > 0) json += ",";
        json += buildModuleJson(m);
    }

    json += "],";
    json += "\"lowestCellMv\":" + String(g_bmsState.lowestCellMv) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.lowestCellMv : 0);
    json += "}";

    return json;
}

/**
 * Build JSON summary with V2 features.
 */
static String buildSummaryJson() {
    int presentCount = 0;
    int balancingCount = 0;

    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (g_bmsState.modules[m].present) {
            presentCount++;
            // Count cells currently balancing
            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                if ((g_bmsState.modules[m].balanceStatus >> c) & 1) {
                    balancingCount++;
                }
            }
        }
    }

    // Calculate seconds since last CAN message
    unsigned long msSinceCan = (g_bmsState.lastCanMessageTime > 0)
        ? (millis() - g_bmsState.lastCanMessageTime)
        : 999999;

    String json = "{";
    json += "\"modulesPresent\":" + String(presentCount) + ",";
    json += "\"lowestCellMv\":" + String(g_bmsState.lowestCellMv) + ",";
    json += "\"highestCellMv\":" + String(g_bmsState.highestCellMv) + ",";
    json += "\"avgCellVoltage\":" + String(g_bmsState.avgCellVoltage, 3) + ",";
    json += "\"packVoltage\":" + String(g_bmsState.packVoltage, 2) + ",";
    json += "\"lowestTemp\":" + String(g_bmsState.lowestTemp, 1) + ",";
    json += "\"highestTemp\":" + String(g_bmsState.highestTemp, 1) + ",";
    json += "\"avgTemp\":" + String(g_bmsState.avgTemp, 1) + ",";
    json += "\"soc\":" + String(g_bmsState.soc) + ",";
    json += "\"currentAmps\":" + String(g_bmsState.currentAmps, 2) + ",";
    json += "\"avgCurrentAmps\":" + String(g_bmsState.avgCurrentAmps, 2) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.lowestCellMv : 0) + ",";
    json += "\"cellsBalancing\":" + String(balancingCount) + ",";
    json += "\"protectionStatus\":\"" + String(protectionGetStatus()) + "\",";
    json += "\"msSinceCanMsg\":" + String(msSinceCan);
    json += "}";

    return json;
}

// =============================================================================
// HTML DASHBOARD
// =============================================================================

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Outlander BMS Monitor</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #1a1a2e;
            color: #eee;
            padding: 20px;
        }
        h1 { text-align: center; margin-bottom: 20px; color: #4ade80; }
        .summary {
            background: #16213e;
            padding: 15px 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: flex;
            justify-content: space-around;
            flex-wrap: wrap;
            gap: 15px;
        }
        .summary-item { text-align: center; }
        .summary-value { font-size: 1.8em; font-weight: bold; color: #4ade80; }
        .summary-label { font-size: 0.9em; color: #888; }
        .modules { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 15px; }
        .module {
            background: #16213e;
            border-radius: 8px;
            padding: 15px;
        }
        .module.offline { opacity: 0.5; }
        .module-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
            border-bottom: 1px solid #333;
            padding-bottom: 8px;
        }
        .module-title { font-weight: bold; }
        .temps { font-size: 0.85em; color: #f59e0b; }
        .cells { display: grid; grid-template-columns: repeat(4, 1fr); gap: 6px; }
        .cell {
            background: #0f3460;
            padding: 8px 4px;
            border-radius: 4px;
            text-align: center;
            font-size: 0.85em;
            position: relative;
        }
        .cell.balancing { background: #4ade80; color: #000; }
        .cell.low { background: #ef4444; }
        .cell.high { background: #3b82f6; }
        .cell-num { font-size: 0.7em; color: #666; display: block; }
        .cell-delta { font-size: 0.7em; color: #888; display: block; }
        .cell.balancing .cell-delta { color: #333; }
        .module-delta { font-size: 0.8em; color: #f59e0b; margin-left: 10px; }
        .controls {
            text-align: center;
            margin-top: 20px;
        }
        button {
            background: #4ade80;
            color: #000;
            border: none;
            padding: 12px 30px;
            border-radius: 6px;
            font-size: 1em;
            cursor: pointer;
            font-weight: bold;
        }
        button:hover { background: #22c55e; }
        button.off { background: #6b7280; color: #fff; }
        .status { text-align: center; margin-top: 10px; color: #666; font-size: 0.85em; }
        .error { color: #ef4444; }
    </style>
</head>
<body>
    <h1>Outlander BMS Monitor</h1>

    <div class="summary">
        <div class="summary-item">
            <div class="summary-value" id="canStatus">--</div>
            <div class="summary-label">CAN Bus</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="soc">--</div>
            <div class="summary-label">SOC (%)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="packVoltage">--</div>
            <div class="summary-label">Pack Voltage (V)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="current">--</div>
            <div class="summary-label">Current (A)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="lowestCell">--</div>
            <div class="summary-label">Lowest Cell (mV)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="highestCell">--</div>
            <div class="summary-label">Highest Cell (mV)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="avgTemp">--</div>
            <div class="summary-label">Avg Temp (°C)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="protection">--</div>
            <div class="summary-label">Protection</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="modulesOnline">--</div>
            <div class="summary-label">Modules Online</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="cellsBalancing">--</div>
            <div class="summary-label">Cells Balancing</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="balanceTarget">--</div>
            <div class="summary-label">Balance Target (mV)</div>
        </div>
    </div>

    <div class="modules" id="modulesContainer"></div>

    <div class="controls">
        <button id="balanceBtn" onclick="toggleBalancing()">Balancing: OFF</button>
    </div>

    <div class="status" id="status">Connecting...</div>

    <script>
        let balancingEnabled = false;
        let lowestCellMv = 5000;

        function updateDashboard(data, summary) {
            lowestCellMv = data.lowestCellMv;
            balancingEnabled = data.balancingEnabled;

            // Update CAN status
            const canEl = document.getElementById('canStatus');
            if (summary.msSinceCanMsg < 2000) {
                canEl.textContent = 'OK';
                canEl.style.color = '#4ade80';
            } else if (summary.msSinceCanMsg < 10000) {
                canEl.textContent = 'SLOW';
                canEl.style.color = '#f59e0b';
            } else {
                canEl.textContent = 'NO DATA';
                canEl.style.color = '#ef4444';
            }

            document.getElementById('lowestCell').textContent = data.lowestCellMv;
            document.getElementById('highestCell').textContent = summary.highestCellMv;
            document.getElementById('packVoltage').textContent = summary.packVoltage;
            document.getElementById('soc').textContent = summary.soc + '%';
            document.getElementById('current').textContent = summary.avgCurrentAmps;
            document.getElementById('avgTemp').textContent = summary.avgTemp;
            
            // Protection status with color
            const protEl = document.getElementById('protection');
            protEl.textContent = summary.protectionStatus;
            if (summary.protectionStatus === 'OK') {
                protEl.style.color = '#4ade80';
            } else if (summary.protectionStatus.includes('WARNING')) {
                protEl.style.color = '#f59e0b';
            } else {
                protEl.style.color = '#ef4444';
            }
            
            document.getElementById('balanceBtn').textContent = 'Balancing: ' + (balancingEnabled ? 'ON' : 'OFF');
            document.getElementById('balanceBtn').className = balancingEnabled ? '' : 'off';
            
            const targetEl = document.getElementById('balanceTarget');
            if (balancingEnabled && summary.balanceTargetMv > 0) {
                targetEl.textContent = summary.balanceTargetMv;
                targetEl.style.color = '#4ade80';
            } else {
                targetEl.textContent = '--';
                targetEl.style.color = '#666';
            }

            let onlineCount = 0;
            let balancingCount = 0;
            let html = '';

            for (const mod of data.modules) {
                if (mod.present) onlineCount++;

                const validVoltages = mod.voltages.filter(v => v > 0);
                const modMin = validVoltages.length > 0 ? Math.min(...validVoltages) : 0;
                const modMax = validVoltages.length > 0 ? Math.max(...validVoltages) : 0;
                const modDelta = modMax - modMin;

                html += `<div class="module ${mod.present ? '' : 'offline'}">`;
                html += `<div class="module-header">`;
                html += `<span class="module-title">CMU ${mod.module}<span class="module-delta">Δ${modDelta}mV</span></span>`;
                html += `<span class="temps">${mod.temperatures.map(t => t.toFixed(1) + '°C').join(' | ')}</span>`;
                html += `</div>`;
                html += `<div class="cells">`;

                for (let i = 0; i < mod.voltages.length; i++) {
                    const v = mod.voltages[i];
                    const isBalancing = mod.balancing[i];
                    if (isBalancing) balancingCount++;
                    const cellDelta = v > 0 ? v - modMin : 0;

                    let cellClass = 'cell';
                    if (isBalancing) cellClass += ' balancing';
                    else if (v > 0 && v <= lowestCellMv + 5) cellClass += ' low';
                    else if (v > 0 && v >= lowestCellMv + 50) cellClass += ' high';

                    html += `<div class="${cellClass}">`;
                    html += `<span class="cell-num">C${i + 1}</span>`;
                    html += `${v}`;
                    html += `<span class="cell-delta">+${cellDelta}</span>`;
                    html += `</div>`;
                }

                html += `</div></div>`;
            }

            document.getElementById('modulesContainer').innerHTML = html;
            document.getElementById('modulesOnline').textContent = onlineCount + '/8';
            document.getElementById('cellsBalancing').textContent = balancingCount;
            document.getElementById('status').textContent = 'Last update: ' + new Date().toLocaleTimeString();
            document.getElementById('status').className = 'status';
        }

        async function fetchData() {
            try {
                const [bmsRes, summaryRes] = await Promise.all([
                    fetch('/api/bms'),
                    fetch('/api/summary')
                ]);
                if (!bmsRes.ok) throw new Error('HTTP ' + bmsRes.status);
                const data = await bmsRes.json();
                const summary = await summaryRes.json();
                updateDashboard(data, summary);
            } catch (err) {
                document.getElementById('status').textContent = 'Error: ' + err.message;
                document.getElementById('status').className = 'status error';
            }
        }

        async function toggleBalancing() {
            try {
                const response = await fetch('/api/balancing', { method: 'POST' });
                if (response.ok) {
                    fetchData();
                }
            } catch (err) {
                console.error('Toggle failed:', err);
            }
        }

        // Initial fetch and auto-refresh every 1 second
        fetchData();
        setInterval(fetchData, 1000);
    </script>
</body>
</html>
)rawliteral";

// =============================================================================
// REQUEST HANDLERS
// =============================================================================

static void handleRoot(AsyncWebServerRequest* request) {
    request->send(200, "text/html", DASHBOARD_HTML);
}

static void handleApiBms(AsyncWebServerRequest* request) {
    String json = buildFullBmsJson();
    request->send(200, "application/json", json);
}

static void handleApiModuleN(AsyncWebServerRequest* request, int moduleNum) {
    if (moduleNum < 1 || moduleNum > BMS_MODULE_COUNT) {
        request->send(400, "application/json", "{\"error\":\"Invalid module number (1-8)\"}");
        return;
    }

    String json = buildModuleJson(moduleNum - 1);  // Convert to 0-indexed
    request->send(200, "application/json", json);
}

static void handleApiSummary(AsyncWebServerRequest* request) {
    String json = buildSummaryJson();
    request->send(200, "application/json", json);
}

static void handleApiBalancing(AsyncWebServerRequest* request) {
    g_bmsState.balancingEnabled = !g_bmsState.balancingEnabled;

    String json = "{\"balancingEnabled\":";
    json += g_bmsState.balancingEnabled ? "true" : "false";
    json += "}";

    Serial.print("[Web] Balancing toggled: ");
    Serial.println(g_bmsState.balancingEnabled ? "ON" : "OFF");

    request->send(200, "application/json", json);
}


static void handleNotFound(AsyncWebServerRequest* request) {
    request->send(404, "application/json", "{\"error\":\"Not found\"}");
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

void webServerInit() {
    // API endpoints
    s_server.on("/", HTTP_GET, handleRoot);
    s_server.on("/api/bms", HTTP_GET, handleApiBms);

    // Register individual module endpoints (avoiding regex dependency)
    s_server.on("/api/module/1", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 1); });
    s_server.on("/api/module/2", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 2); });
    s_server.on("/api/module/3", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 3); });
    s_server.on("/api/module/4", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 4); });
    s_server.on("/api/module/5", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 5); });
    s_server.on("/api/module/6", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 6); });
    s_server.on("/api/module/7", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 7); });
    s_server.on("/api/module/8", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 8); });

    s_server.on("/api/summary", HTTP_GET, handleApiSummary);
    s_server.on("/api/balancing", HTTP_POST, handleApiBalancing);

    // 404 handler
    s_server.onNotFound(handleNotFound);

    // Start server
    s_server.begin();
    Serial.println("[Web] Server started on port 80");
}
