/**
 * @file web_server.cpp
 * @brief Async web server implementation
 *
 * REST API endpoints:
 *   GET  /api/bms       - Full BMS state (all modules)
 *   GET  /api/module/N  - Single module data (N = 1-20)
 *   GET  /api/summary   - Pack summary and safety state
 *   GET  /api/help      - Serial command to web API mapping
 *   POST /api/balancing - Toggle balancing on/off
 *   POST /api/balancing/restart - Send a 2-second balance-disable pulse
 *   POST /api/command   - Execute any serial command via the web UI/API
 *   POST /api/reboot    - Acknowledge, then reboot the device
 *   GET  /              - HTML dashboard
 */

#include "web_server.h"
#include "soc_curve_validation.h"
#include "config.h"
#include "bms_data.h"
#include "protection.h"
#include "can_handler.h"
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Web server instance on port 80
static AsyncWebServer s_server(80);
static volatile bool s_rebootTaskScheduled = false;
static constexpr TickType_t REBOOT_DELAY_TICKS = pdMS_TO_TICKS(1000);

/**
 * ESPAsyncWebServer request callbacks do not run in the Arduino loop. Run the
 * restart from a dedicated task so an acknowledgement can leave the TCP stack
 * before reset, even if the main loop is busy or stalled on a peripheral.
 */
static void rebootTask(void *) {
    vTaskDelay(REBOOT_DELAY_TICKS);
    Serial.println("[Web] Rebooting device now");
    Serial.flush();
    ESP.restart();
}

// =============================================================================
// JSON BUILDERS
// =============================================================================

/**
 * Build JSON for a single CMU module.
 */
static String buildModuleJson(int moduleIndex) {
    const CmuData& cmu = g_bmsState.modules[moduleIndex];
    int busIndex = (moduleIndex < 10) ? 0 : 1;
    int cmuId = (moduleIndex % 10) + 1;
    const long moduleVoltageMv = cmu.getModuleVoltageMv();

    String json = "{";
    json += "\"module\":" + String(moduleIndex + 1) + ",";
    json += "\"cmuId\":" + String(cmuId) + ",";
    json += "\"bus\":\"" + String(busIndex == 0 ? "A" : "B") + "\",";
    json += "\"present\":" + String(cmu.present ? "true" : "false") + ",";
    json += "\"moduleVoltageMv\":";
    json += moduleVoltageMv > 0 ? String(moduleVoltageMv) : "null";
    json += ",";

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

    long maximumTemperatureRaw = -70000;
    for (int i = 0; i < TEMPS_PER_MODULE; ++i) {
        const long raw = cmu.temperatures[i];
        if (raw > -70000 && raw < 100000 && raw > maximumTemperatureRaw) maximumTemperatureRaw = raw;
    }
    json += "\"maximumTemperature\":";
    json += maximumTemperatureRaw > -70000 ? String(maximumTemperatureRaw / 1000.0f, 1) : "null";
    json += ",";

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
    json += "\"highestCellMv\":" + String(g_bmsState.highestCellMv) + ",";
    json += "\"medianCellMv\":" + String(g_bmsState.medianCellMv) + ",";
    json += "\"cellVoltageDeltaMv\":" + String(g_bmsState.cellVoltageDeltaMv) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.balanceTargetMv : 0);
    json += "}";

    return json;
}

/**
 * Build the full summary and safety state JSON.
 */
static String buildSummaryJson() {
    int presentCount = 0;
    int balancingCount = 0;
    int expectedCount = 0;

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

    // Count expected CMUs (10 per bus)
    for (int i = 0; i < 10; i++) {
        if (g_bmsSettings.expectedCmusA & (1 << i)) expectedCount++;
        if (g_bmsSettings.expectedCmusB & (1 << i)) expectedCount++;
    }

    // Calculate seconds since last CAN message
    unsigned long msSinceCan = (g_bmsState.lastCanMessageTime > 0)
        ? (millis() - g_bmsState.lastCanMessageTime)
        : 999999;

    const bool batterySafeToUse = digitalRead(PIN_BATTERY_SAFE_TO_USE) == HIGH;
    const CanStats canStats = canGetStats();

    String json = "{";
    json += "\"modulesPresent\":" + String(presentCount) + ",";
    json += "\"lowestCellMv\":" + String(g_bmsState.lowestCellMv) + ",";
    json += "\"highestCellMv\":" + String(g_bmsState.highestCellMv) + ",";
    json += "\"medianCellMv\":" + String(g_bmsState.medianCellMv) + ",";
    json += "\"cellVoltageDeltaMv\":" + String(g_bmsState.cellVoltageDeltaMv) + ",";
    json += "\"avgCellVoltage\":" + String(g_bmsState.avgCellVoltage, 3) + ",";
    json += "\"packVoltage\":" + String(g_bmsState.packVoltage, 2) + ",";
    json += "\"lowestTemp\":" + String(g_bmsState.lowestTemp, 1) + ",";
    json += "\"highestTemp\":" + String(g_bmsState.highestTemp, 1) + ",";
    json += "\"avgTemp\":" + String(g_bmsState.avgTemp, 1) + ",";
    json += "\"soc\":" + String(g_bmsState.soc) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.balanceTargetMv : 0) + ",";
    json += "\"cellsBalancing\":" + String(balancingCount) + ",";
    json += "\"protectionStatus\":\"" + String(protectionGetStatus()) + "\",";
    json += "\"debugMode\":" + String(g_bmsState.debugMode ? "true" : "false") + ",";
    json += "\"supervisedOverrideActive\":" + String(protectionSupervisedOverrideActive() ? "true" : "false") + ",";
    json += "\"supervisedOverrideRemainingMs\":" + String(protectionSupervisedOverrideRemainingMs()) + ",";
    json += "\"msSinceCanMsg\":" + String(msSinceCan) + ",";
    json += "\"uptimeMs\":" + String(millis()) + ",";
    json += "\"balanceTxAttempts\":" + String(canStats.balanceTxAttempts) + ",";
    json += "\"balanceTxQueued\":" + String(canStats.balanceTxQueued) + ",";
    json += "\"lastBalanceTargetMv\":" + String(canStats.lastBalanceTargetMv) + ",";
    json += "\"lastBalanceBusMask\":" + String(canStats.lastBalanceBusMask) + ",";
    json += "\"msSinceBalanceCommand\":" + String(canStats.lastBalanceCommandTime > 0 ? millis() - canStats.lastBalanceCommandTime : 0) + ",";
    json += "\"balanceRecovery\":{";
    json += "\"active\":" + String(canStats.balanceRecoveryActive ? "true" : "false") + ",";
    json += "\"remainingMs\":" + String(canStats.balanceRecoveryRemainingMs) + ",";
    json += "\"count\":" + String(canStats.balanceRecoveryCount);
    json += "},";
    json += "\"twai\":{";
    json += "\"enabled\":" + String(canIsBusBEnabled() ? "true" : "false") + ",";
    json += "\"statusValid\":" + String(canStats.twaiStatusValid ? "true" : "false") + ",";
    json += "\"state\":\"" + String(canStats.twaiStatusValid ? canGetTwaiStateName(canStats.twaiState) : "UNAVAILABLE") + "\",";
    json += "\"txErrorCounter\":" + String(canStats.twaiTxErrorCounter) + ",";
    json += "\"rxErrorCounter\":" + String(canStats.twaiRxErrorCounter) + ",";
    json += "\"txFailedCount\":" + String(canStats.twaiTxFailedCount) + ",";
    json += "\"rxMissedCount\":" + String(canStats.twaiRxMissedCount) + ",";
    json += "\"arbLostCount\":" + String(canStats.twaiArbLostCount) + ",";
    json += "\"busErrorCount\":" + String(canStats.twaiBusErrorCount);
    json += "},";
    json += "\"hasData\":" + String(presentCount > 0 ? "true" : "false") + ",";
    json += "\"expectedTotal\":" + String(expectedCount) + ",";
    json += "\"expectedCmusA\":" + String(g_bmsSettings.expectedCmusA) + ",";
    json += "\"expectedCmusB\":" + String(g_bmsSettings.expectedCmusB) + ",";
    json += "\"io\":{\"batterySafeToUse\":" + String(batterySafeToUse ? "true" : "false") + "}";
    json += "}";

    return json;
}

static String buildReportJson() {
    return "{\"bms\":" + buildFullBmsJson() +
           ",\"summary\":" + buildSummaryJson() + "}";
}

static String buildDetailedStatsJson() {
    g_bmsState.updatePackStatistics();
    String json = "{\"balancingEnabled\":" +
                  String(g_bmsState.balancingEnabled ? "true" : "false") +
                  ",\"modules\":[";
    bool firstModule = true;
    for (int module = 0; module < BMS_MODULE_COUNT; ++module) {
        const CmuData& cmu = g_bmsState.modules[module];
        if (!cmu.present) continue;
        if (!firstModule) json += ",";
        firstModule = false;
        json += "{\"bus\":\"" + String(module < 10 ? "A" : "B") +
                "\",\"cmuId\":" + String((module % 10) + 1) +
                ",\"balanceStatus\":" + String(cmu.balanceStatus) +
                ",\"voltages\":[";
        for (int cell = 0; cell < CELLS_PER_MODULE; ++cell) {
            if (cell) json += ",";
            json += String(cmu.voltages[cell]);
        }
        json += "],\"temperatures\":[";
        for (int temp = 0; temp < TEMPS_PER_MODULE; ++temp) {
            if (temp) json += ",";
            json += String(cmu.temperatures[temp] / 1000.0f, 1);
        }
        json += "]}";
    }
    json += "],\"summary\":" + buildSummaryJson() + "}";
    return json;
}

static String buildCanDiagnosticsJson() {
    const CanStats stats = canGetStats();
    const CanHardwareDiagnostics hardware = canGetHardwareDiagnostics();
    String json = "{";
    json += "\"spiOk\":" + String(hardware.spiOk ? "true" : "false") + ",";
    json += "\"mcp2515\":{";
    json += "\"status\":" + String(hardware.status) + ",";
    json += "\"errorFlags\":" + String(hardware.errorFlags) + ",";
    json += "\"interrupts\":" + String(hardware.interrupts) + ",";
    json += "\"txErrorCount\":" + String(hardware.txErrorCount) + ",";
    json += "\"rxErrorCount\":" + String(hardware.rxErrorCount) + "},";
    json += "\"twai\":{";
    json += "\"enabled\":" + String(canIsBusBEnabled() ? "true" : "false") + ",";
    json += "\"statusValid\":" + String(stats.twaiStatusValid ? "true" : "false") + ",";
    json += "\"state\":\"" + String(stats.twaiStatusValid ? canGetTwaiStateName(stats.twaiState) : "UNAVAILABLE") + "\",";
    json += "\"txErrorCounter\":" + String(stats.twaiTxErrorCounter) + ",";
    json += "\"rxErrorCounter\":" + String(stats.twaiRxErrorCounter) + ",";
    json += "\"txFailedCount\":" + String(stats.twaiTxFailedCount) + ",";
    json += "\"rxMissedCount\":" + String(stats.twaiRxMissedCount) + ",";
    json += "\"arbLostCount\":" + String(stats.twaiArbLostCount) + ",";
    json += "\"busErrorCount\":" + String(stats.twaiBusErrorCount) + "},";
    json += "\"messagesReceived\":" + String(stats.messagesReceived) + ",";
    json += "\"messagesDecoded\":" + String(stats.messagesDecoded) + ",";
    json += "\"readAttempts\":" + String(stats.readAttempts) + ",";
    json += "\"txAttempts\":" + String(stats.txAttempts) + ",";
    json += "\"txSuccess\":" + String(stats.txSuccess) + ",";
    json += "\"balanceTxAttempts\":" + String(stats.balanceTxAttempts) + ",";
    json += "\"balanceTxQueued\":" + String(stats.balanceTxQueued) + ",";
    json += "\"lastBalanceTargetMv\":" + String(stats.lastBalanceTargetMv) + ",";
    json += "\"lastBalanceBusMask\":" + String(stats.lastBalanceBusMask) + ",";
    json += "\"lastMessageTime\":" + String(stats.lastMessageTime) + ",";
    json += "\"lastBalanceCommandTime\":" + String(stats.lastBalanceCommandTime);
    json += "}";
    return json;
}

static String buildHelpJson() {
    return "{\"commands\":["
           "{\"serial\":\"b\",\"web\":\"POST /api/command command=b\",\"description\":\"Toggle balancing\"},"
           "{\"serial\":\"d\",\"web\":\"POST /api/command command=d\",\"description\":\"Toggle raw-CAN debug mode\"},"
           "{\"serial\":\"r\",\"web\":\"POST /api/command command=r\",\"description\":\"Full BMS report\"},"
           "{\"serial\":\"o\",\"web\":\"POST /api/command command=o\",\"description\":\"Enable 10-minute supervised voltage recovery override\"},"
           "{\"serial\":\"O\",\"web\":\"POST /api/command command=O\",\"description\":\"Cancel supervised recovery override\"},"
           "{\"serial\":\"s\",\"web\":\"POST /api/command command=s\",\"description\":\"Detailed module statistics\"},"
           "{\"serial\":\"c\",\"web\":\"POST /api/command command=c\",\"description\":\"CAN diagnostics\"},"
           "{\"serial\":\"A/B\",\"web\":\"POST /api/command command=A|B&mask=HEX\",\"description\":\"Set expected CMU mask\"},"
           "{\"serial\":\"h/?\",\"web\":\"GET /api/help or POST /api/command command=h\",\"description\":\"Show command help\"}]}";
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
        .io-summary {
            background: #121b34;
            padding: 12px 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
            gap: 18px;
        }
        .io-block { background: #0f172a; border-radius: 8px; padding: 12px; }
        .io-title { font-weight: bold; color: #93c5fd; margin-bottom: 8px; }
        .io-grid { display: grid; grid-template-columns: repeat(2, 1fr); gap: 8px; }
        .io-item { background: #0b1220; border-radius: 6px; padding: 8px; text-align: center; }
        .io-label { display: block; font-size: 0.75em; color: #94a3b8; margin-bottom: 4px; }
        .io-value { font-weight: bold; color: #6b7280; }
        .safety-summary {
            background: #121b34;
            padding: 12px 20px;
            border-radius: 8px;
            margin-bottom: 20px;
            display: flex;
            justify-content: center;
            flex-wrap: wrap;
            gap: 18px;
        }
        .protection-row {
            background: #121b34;
            border-radius: 8px;
            margin-bottom: 20px;
            padding: 12px 20px;
            text-align: center;
        }
        .protection-row .summary-label { display: inline; margin-right: 8px; }
        .summary-item { text-align: center; }
        .summary-value { font-size: 1.8em; font-weight: bold; color: #4ade80; }
        .summary-label { font-size: 0.9em; color: #888; }
        .modules { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 15px; }
        .bus-section { grid-column: 1 / -1; }
        .bus-summary {
            display: flex;
            justify-content: space-between;
            align-items: center;
            cursor: pointer;
            margin-top: 20px;
            color: #3b82f6;
            border-bottom: 2px solid #3b82f6;
            padding: 8px 4px;
            font-size: 1.5em;
            font-weight: bold;
        }
        .bus-summary-meta { color: #94a3b8; font-size: 0.55em; font-weight: normal; }
        .bus-modules {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 15px;
            padding-top: 15px;
        }
        .module {
            background: #16213e;
            border-radius: 8px;
            padding: 15px;
        }
        .module.offline { opacity: 0.5; }
        .module-header {
            display: block;
            margin-bottom: 10px;
            border-bottom: 1px solid #333;
            padding-bottom: 8px;
        }
        .module-controls { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
        .expected-chk { cursor: pointer; }
        .module-title { font-weight: bold; }
        .temps { display: block; margin-top: 6px; font-size: 0.85em; color: #f59e0b; }
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
        .module-voltage { font-size: 0.9em; color: #4ade80; margin-left: 10px; }
        .controls {
            display: flex;
            justify-content: center;
            flex-wrap: wrap;
            gap: 12px;
            margin-top: 20px;
        }
        .mask-control { display: inline-flex; align-items: center; gap: 6px; color: #cbd5e1; font-size: 0.85em; }
        .mask-control input { width: 4.5em; padding: 8px 6px; border: 1px solid #475569; border-radius: 4px; background: #0f172a; color: #f8fafc; text-transform: uppercase; }
        .mask-control button { padding: 8px 12px; font-size: 0.85em; }
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
        button.danger { background: #dc2626; color: #fff; }
        button.danger:hover { background: #b91c1c; }
        button.recovery { background: #f59e0b; color: #111827; }
        button.recovery:hover { background: #d97706; }
        button:disabled { cursor: wait; opacity: 0.65; }
        .status { text-align: center; margin-top: 10px; color: #666; font-size: 0.85em; }
        .uptime { margin-top: 2px; }
        .error { color: #ef4444; }
        .command-output {
            width: 100%;
            box-sizing: border-box;
            min-height: 120px;
            max-height: 360px;
            overflow: auto;
            background: #0b1220;
            color: #cbd5e1;
            border-radius: 8px;
            padding: 12px;
            white-space: pre-wrap;
            text-align: left;
            font-size: 0.78em;
        }
    </style>
</head>
<body>
    <h1>Outlander BMS Monitor</h1>

    <div class="safety-summary">
        <div class="summary-item">
            <div class="summary-value" id="batterySafeToUse">--</div>
            <div class="summary-label">Battery Safe To Use (GPIO15)</div>
        </div>
    </div>

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
            <div class="summary-value" id="lowestCell">--</div>
            <div class="summary-label">Lowest Cell (mV)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="highestCell">--</div>
            <div class="summary-label">Highest Cell (mV)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="voltageDelta">--</div>
            <div class="summary-label">All-Cell Delta (mV)</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="avgTemp">--</div>
            <div class="summary-label">Avg Temp (°C)</div>
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
            <div class="summary-label">8th-Lowest Balance Target (mV)</div>
        </div>
    </div>

    <div class="protection-row">
        <span class="summary-label">Protection</span>
        <span class="summary-value" id="protection">--</span>
    </div>

    <div class="modules" id="modulesContainer"></div>

    <div class="controls">
        <button id="balanceBtn" onclick="toggleBalancing()">Balancing: OFF</button>
        <button id="balanceRecoveryBtn" class="recovery" onclick="restartBalancing()">Restart Balancing</button>
        <button id="debugBtn" class="off" onclick="sendCommand('d')">Debug: OFF</button>
        <button class="recovery" onclick="sendCommand('o', 'Enable the 10-minute supervised voltage recovery override? Confirm fresh CMU data and prepare the charge/discharge procedure first.')">Enable Voltage Override</button>
        <button class="off" onclick="sendCommand('O')">Cancel Voltage Override</button>
        <button onclick="sendCommand('r')">Full Report</button>
        <button onclick="sendCommand('s')">Detailed Stats</button>
        <button onclick="sendCommand('c')">CAN Diagnostics</button>
        <button onclick="sendCommand('h')">Command Help</button>
        <label class="mask-control">Expected A mask (hex)
            <input id="expectedMaskA" maxlength="3" inputmode="text" aria-label="Expected Bus A CMU mask">
            <button onclick="setExpectedMask('A')">Set A</button>
        </label>
        <label class="mask-control">Expected B mask (hex)
            <input id="expectedMaskB" maxlength="3" inputmode="text" aria-label="Expected Bus B CMU mask">
            <button onclick="setExpectedMask('B')">Set B</button>
        </label>
        <button id="rebootBtn" class="danger" onclick="rebootDevice()">Reboot Device</button>
    </div>

    <pre id="commandOutput" class="command-output">Web command output appears here.</pre>

    <div class="io-summary">
        <div class="io-block">
            <div class="io-title">Output</div>
            <div class="io-grid">
                <div class="io-item"><span class="io-label">BATTERY_SAFE_TO_USE</span><span class="io-value" id="ioBatterySafeToUse">--</span></div>
            </div>
        </div>
    </div>

    <div class="status" id="status">Connecting...</div>
    <div class="status uptime" id="uptime">Uptime: --</div>
    <div class="status uptime" id="balanceDiagnostics">Balance CAN: --</div>
    <div class="status uptime" id="twaiDiagnostics">TWAI B: --</div>

    <script>
        let balancingEnabled = false;
        let lowestCellMv = 5000;
        let expectedMaskA = 0;
        let expectedMaskB = 0;
        let rebooting = false;
        let restartingBalancing = false;
        const busOpenState = { A: null, B: null };

        function setBusOpen(bus, isOpen) {
            busOpenState[bus] = isOpen;
        }

        function setIoState(id, isHigh) {
            const el = document.getElementById(id);
            if (!el) return;
            el.textContent = isHigh ? 'HIGH' : 'LOW';
            el.style.color = isHigh ? '#4ade80' : '#6b7280';
        }

        function setIoValue(id, value) {
            const el = document.getElementById(id);
            if (!el) return;
            if (value === null || value === undefined) {
                el.textContent = '--';
                el.style.color = '#6b7280';
                return;
            }
            el.textContent = value.toFixed(2);
            el.style.color = '#e2e8f0';
        }

        function updateDashboard(data, summary) {
            lowestCellMv = data.lowestCellMv;
            balancingEnabled = data.balancingEnabled;
            expectedMaskA = summary.expectedCmusA;
            expectedMaskB = summary.expectedCmusB;
            const maskAInput = document.getElementById('expectedMaskA');
            const maskBInput = document.getElementById('expectedMaskB');
            if (document.activeElement !== maskAInput) maskAInput.value = expectedMaskA.toString(16).toUpperCase().padStart(3, '0');
            if (document.activeElement !== maskBInput) maskBInput.value = expectedMaskB.toString(16).toUpperCase().padStart(3, '0');

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

            const hasData = summary.hasData;
            const na = 'N/A';
            document.getElementById('lowestCell').textContent = hasData ? data.lowestCellMv : na;
            document.getElementById('highestCell').textContent = hasData ? summary.highestCellMv : na;
            document.getElementById('voltageDelta').textContent = hasData ? summary.cellVoltageDeltaMv : na;
            document.getElementById('packVoltage').textContent = hasData ? summary.packVoltage : na;
            document.getElementById('soc').textContent = hasData ? (summary.soc + '%') : na;
            document.getElementById('avgTemp').textContent = hasData ? summary.avgTemp : na;

            const debugBtn = document.getElementById('debugBtn');
            debugBtn.textContent = 'Debug: ' + (summary.debugMode ? 'ON' : 'OFF');
            debugBtn.className = summary.debugMode ? '' : 'off';

            
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

            const io = summary.io || {};
            const batterySafe = !!io.batterySafeToUse;
            const batterySafeEl = document.getElementById('batterySafeToUse');
            batterySafeEl.textContent = batterySafe ? 'SAFE' : 'UNSAFE';
            batterySafeEl.style.color = batterySafe ? '#4ade80' : '#ef4444';
            setIoState('ioBatterySafeToUse', batterySafe);
            
            document.getElementById('balanceBtn').textContent = 'Balancing: ' + (balancingEnabled ? 'ON' : 'OFF');
            document.getElementById('balanceBtn').className = balancingEnabled ? '' : 'off';
            
            const targetEl = document.getElementById('balanceTarget');
            if (summary.balanceTargetMv > 0 && hasData) {
                targetEl.textContent = summary.balanceTargetMv;
                targetEl.style.color = balancingEnabled ? '#4ade80' : '#f59e0b';
            } else {
                targetEl.textContent = '--';
                targetEl.style.color = '#666';
            }

            let onlineCount = 0;
            let balancingCount = 0;

            // Group modules by bus
            const busA = data.modules.filter(m => m.bus === 'A');
            const busB = data.modules.filter(m => m.bus === 'B');

            let html = '';

            const renderBus = (busName, modules) => {
                const expectedMask = busName === 'A' ? expectedMaskA : expectedMaskB;
                const selectedCount = modules.filter(mod => expectedMask & (1 << (mod.cmuId - 1))).length;
                const presentCount = modules.filter(mod => mod.present).length;
                const isOpen = busOpenState[busName] === null
                    ? expectedMask !== 0
                    : busOpenState[busName];
                const selectionText = selectedCount > 0
                    ? `${selectedCount} selected · ${presentCount} online`
                    : `none selected · expand to configure`;
                let busHtml = `<details class="bus-section" ${isOpen ? 'open' : ''} ontoggle="setBusOpen('${busName}', this.open)">`;
                busHtml += `<summary class="bus-summary"><span>Bus ${busName}</span><span class="bus-summary-meta">${selectionText}</span></summary>`;
                busHtml += `<div class="bus-modules">`;
                for (const mod of modules) {
                    const isExpected = (busName === 'A')
                        ? (expectedMaskA & (1 << (mod.cmuId - 1)))
                        : (expectedMaskB & (1 << (mod.cmuId - 1)));

                    if (mod.present) onlineCount++;

                    const validVoltages = mod.voltages.filter(v => v > 0);
                    const modMin = validVoltages.length > 0 ? Math.min(...validVoltages) : 0;
                    const modMax = validVoltages.length > 0 ? Math.max(...validVoltages) : 0;
                    const modDelta = modMax - modMin;
                    const moduleVoltage = mod.moduleVoltageMv
                        ? (mod.moduleVoltageMv / 1000).toFixed(3) + 'V'
                        : '--.---V';

                    busHtml += `<div class="module ${mod.present ? '' : 'offline'}">`;
                    busHtml += `<div class="module-header">`;
                    busHtml += `<div class="module-controls">`;
                    busHtml += `<input type="checkbox" class="expected-chk" title="Expected CMU" ${isExpected ? 'checked' : ''} onchange="updateExpected('${busName}', ${mod.cmuId}, this.checked)">`;
                    busHtml += `<span class="module-title">CMU ${mod.cmuId}<span class="module-voltage">${moduleVoltage}</span><span class="module-delta">Δ${modDelta}mV</span></span>`;
                    busHtml += `</div>`;
                    busHtml += `<span class="temps">${mod.temperatures.map(t => t.toFixed(1) + '°C').join(' | ')}</span>`;
                    busHtml += `</div>`;
                    busHtml += `<div class="cells">`;

                    for (let i = 0; i < mod.voltages.length; i++) {
                        const v = mod.voltages[i];
                        const isBalancing = mod.balancing[i];
                        if (isBalancing) balancingCount++;
                        const hasV = v > 0;
                        const cellDelta = hasV ? v - modMin : 0;

                        let cellClass = 'cell';
                        if (isBalancing) cellClass += ' balancing';
                        else if (hasV && v <= lowestCellMv + 5) cellClass += ' low';
                        else if (hasV && v >= lowestCellMv + 50) cellClass += ' high';

                        busHtml += `<div class="${cellClass}">`;
                        busHtml += `<span class="cell-num">C${i + 1}</span>`;
                        busHtml += hasV ? `${v}` : '--';
                        busHtml += `<span class="cell-delta">${hasV ? ('+' + cellDelta) : '--'}</span>`;
                        busHtml += `</div>`;
                    }
                    busHtml += `</div></div>`;
                }
                busHtml += `</div></details>`;
                return busHtml;
            };

            html += renderBus('A', busA);
            html += renderBus('B', busB);

            document.getElementById('modulesContainer').innerHTML = html;
            const expectedTotal = summary.expectedTotal > 0 ? summary.expectedTotal : 20;
            document.getElementById('modulesOnline').textContent = onlineCount + '/' + expectedTotal;
            document.getElementById('cellsBalancing').textContent = balancingCount;
            document.getElementById('status').textContent = 'Last update: ' + new Date().toLocaleTimeString();
            document.getElementById('status').className = 'status';
            const uptimeSeconds = Math.floor(summary.uptimeMs / 1000);
            const days = Math.floor(uptimeSeconds / 86400);
            const hours = Math.floor((uptimeSeconds % 86400) / 3600);
            const minutes = Math.floor((uptimeSeconds % 3600) / 60);
            const seconds = uptimeSeconds % 60;
            const uptimePrefix = days > 0 ? days + 'd ' : '';
            document.getElementById('uptime').textContent =
                'Uptime: ' + uptimePrefix + hours + 'h ' + minutes + 'm ' + seconds + 's';
            const busName = summary.lastBalanceBusMask === 1 ? 'A' :
                summary.lastBalanceBusMask === 2 ? 'B' :
                summary.lastBalanceBusMask === 3 ? 'A+B' : '--';
            document.getElementById('balanceDiagnostics').textContent =
                'Balance CAN: ' + summary.balanceTxQueued + '/' + summary.balanceTxAttempts +
                ' queued, Bus ' + busName + ', target ' +
                (summary.lastBalanceTargetMv || '--') + ' mV';
            const twai = summary.twai || {};
            const recovery = summary.balanceRecovery || {};
            let twaiText = 'TWAI B: ' + (twai.state || 'UNAVAILABLE') +
                ' | TX err ' + (twai.txErrorCounter ?? '--') +
                ' | TX failed ' + (twai.txFailedCount ?? '--') +
                ' | bus err ' + (twai.busErrorCount ?? '--') +
                ' | arb lost ' + (twai.arbLostCount ?? '--');
            if (recovery.active) {
                twaiText += ' | balance restart: ' + Math.ceil((recovery.remainingMs || 0) / 1000) + 's';
            } else if (recovery.count) {
                twaiText += ' | balance restarts: ' + recovery.count;
            }
            document.getElementById('twaiDiagnostics').textContent = twaiText;
        }

        async function fetchData() {
            if (rebooting) return;
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

        async function sendCommand(command, confirmation) {
            if (confirmation && !window.confirm(confirmation)) return;
            const output = document.getElementById('commandOutput');
            try {
                const formData = new FormData();
                formData.append('command', command);
                const response = await fetch('/api/command', { method: 'POST', body: formData });
                const payload = await response.json();
                output.textContent = JSON.stringify(payload, null, 2);
                if (!response.ok) throw new Error(payload.error || ('HTTP ' + response.status));
                fetchData();
            } catch (err) {
                output.textContent = 'Command failed: ' + err.message;
            }
        }

        async function setExpectedMask(bus) {
            const input = document.getElementById(bus === 'A' ? 'expectedMaskA' : 'expectedMaskB');
            const mask = input.value.trim();
            if (!/^[0-9a-fA-F]{1,3}$/.test(mask)) {
                document.getElementById('commandOutput').textContent = 'Mask must be 1-3 hexadecimal digits.';
                return;
            }
            const formData = new FormData();
            formData.append('command', bus);
            formData.append('mask', mask);
            try {
                const response = await fetch('/api/command', { method: 'POST', body: formData });
                const payload = await response.json();
                document.getElementById('commandOutput').textContent = JSON.stringify(payload, null, 2);
                if (!response.ok) throw new Error(payload.error || ('HTTP ' + response.status));
                fetchData();
            } catch (err) {
                document.getElementById('commandOutput').textContent = 'Command failed: ' + err.message;
            }
        }

        async function restartBalancing() {
            if (!window.confirm('Send a 2-second balance-disable pulse, then re-enable balancing? This does not reboot the BMS or reset CMUs.')) return;

            restartingBalancing = true;
            const button = document.getElementById('balanceRecoveryBtn');
            const status = document.getElementById('status');
            button.disabled = true;
            button.textContent = 'Restarting...';
            status.textContent = 'Balance restart requested: disable pulse in progress.';
            status.className = 'status';

            try {
                const response = await fetch('/api/balancing/restart', { method: 'POST' });
                if (!response.ok) throw new Error('HTTP ' + response.status);
                setTimeout(fetchData, 2300);
            } catch (err) {
                status.textContent = 'Balance restart was not started: ' + err.message;
                status.className = 'status error';
            } finally {
                setTimeout(() => {
                    restartingBalancing = false;
                    button.disabled = false;
                    button.textContent = 'Restart Balancing';
                }, 2300);
            }
        }

        async function rebootDevice() {
            if (!window.confirm('Reboot the Outlander BMS device now?')) return;

            rebooting = true;
            const button = document.getElementById('rebootBtn');
            const status = document.getElementById('status');
            button.disabled = true;
            button.textContent = 'Rebooting...';
            status.textContent = 'Reboot requested. Waiting for the device to restart...';
            status.className = 'status';

            try {
                const response = await fetch('/api/reboot', { method: 'POST' });
                if (!response.ok) throw new Error('HTTP ' + response.status);
            } catch (err) {
                status.textContent = 'Reboot request may have interrupted the connection. Waiting for restart...';
                status.className = 'status';
            }
        }

        async function updateExpected(bus, cmuId, isChecked) {
            try {
                let mask = (bus === 'A') ? expectedMaskA : expectedMaskB;
                if (isChecked) {
                    mask |= (1 << (cmuId - 1));
                } else {
                    mask &= ~(1 << (cmuId - 1));
                }
                if (mask === 0) busOpenState[bus] = false;

                const formData = new FormData();
                formData.append(bus === 'A' ? 'expectedCmusA' : 'expectedCmusB', mask);

                const response = await fetch('/api/config', {
                    method: 'POST',
                    body: formData
                });

                if (response.ok) {
                    fetchData();
                }
            } catch (err) {
                console.error('Update expected failed:', err);
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
        request->send(400, "application/json", "{\"error\":\"Invalid module number (1-20)\"}");
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

static void handleApiBalanceRestart(AsyncWebServerRequest* request) {
    if (!canRequestBalanceRecovery()) {
        request->send(409, "application/json",
                      "{\"error\":\"Balancing must be on and no restart may already be active\"}");
        return;
    }

    request->send(202, "application/json",
                  "{\"status\":\"balance_disable_pulse_started\",\"durationMs\":2000}");
}

static void handleApiCommand(AsyncWebServerRequest* request) {
    if (!request->hasParam("command", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing command\"}");
        return;
    }

    const String command = request->getParam("command", true)->value();
    if (command == "b") {
        g_bmsState.balancingEnabled = !g_bmsState.balancingEnabled;
        request->send(200, "application/json",
                      String("{\"command\":\"b\",\"balancingEnabled\":") +
                      (g_bmsState.balancingEnabled ? "true}" : "false}"));
        return;
    }
    if (command == "d") {
        g_bmsState.debugMode = !g_bmsState.debugMode;
        request->send(200, "application/json",
                      String("{\"command\":\"d\",\"debugMode\":") +
                      (g_bmsState.debugMode ? "true}" : "false}"));
        return;
    }
    if (command == "r") {
        request->send(200, "application/json", buildReportJson());
        return;
    }
    if (command == "s") {
        request->send(200, "application/json", buildDetailedStatsJson());
        return;
    }
    if (command == "c") {
        request->send(200, "application/json", buildCanDiagnosticsJson());
        return;
    }
    if (command == "h" || command == "?") {
        request->send(200, "application/json", buildHelpJson());
        return;
    }
    if (command == "o") {
        if (!protectionEnableSupervisedOverride()) {
            request->send(409, "application/json",
                          "{\"error\":\"Override refused: fresh selected-CMU CAN data and safe temperature are required\",\"status\":\"" +
                          String(protectionGetStatus()) + "\"}");
            return;
        }
        request->send(200, "application/json",
                      "{\"command\":\"o\",\"status\":\"SUPERVISED OVERRIDE\",\"remainingMs\":600000}");
        return;
    }
    if (command == "O") {
        protectionCancelSupervisedOverride();
        request->send(200, "application/json",
                      "{\"command\":\"O\",\"status\":\"override_cancelled\"}");
        return;
    }
    if (command == "A" || command == "B") {
        if (!request->hasParam("mask", true)) {
            request->send(400, "application/json",
                          "{\"error\":\"A/B requires mask in hexadecimal or decimal\"}");
            return;
        }
        const uint16_t mask = static_cast<uint16_t>(
            strtoul(request->getParam("mask", true)->value().c_str(), nullptr, 16)) & 0x03FF;
        if (command == "A") g_bmsSettings.expectedCmusA = mask;
        else g_bmsSettings.expectedCmusB = mask;
        settingsSave();
        String response = "{\"command\":\"" + command + "\",\"mask\":" + String(mask) + "}";
        request->send(200, "application/json", response);
        return;
    }

    request->send(400, "application/json", "{\"error\":\"Unknown serial command\"}");
}

static void handleApiReboot(AsyncWebServerRequest* request) {
    if (s_rebootTaskScheduled) {
        request->send(202, "application/json", "{\"status\":\"restarting\"}");
        return;
    }

    // Schedule before responding. The one-second delay gives AsyncTCP time to
    // transmit the 202 response, while no main-loop cooperation is required.
    if (xTaskCreate(rebootTask, "web_reboot", 2048, nullptr, 1, nullptr) != pdPASS) {
        request->send(503, "application/json", "{\"error\":\"Unable to schedule reboot\"}");
        return;
    }

    s_rebootTaskScheduled = true;
    Serial.println("[Web] Device reboot requested");
    request->send(202, "application/json", "{\"status\":\"restarting\"}");
}

static void handleApiConfig(AsyncWebServerRequest* request) {
    int validatedSocCurve[4];
    const bool hasSocCurve = request->hasParam("socCurve", true);
    if (hasSocCurve) {
        const String curve = request->getParam("socCurve", true)->value();
        if (!parseSocVoltageCurve(curve.c_str(), validatedSocCurve)) {
            request->send(400, "application/json", "{\"error\":\"Invalid SOC curve\"}");
            return;
        }
    }

    if (request->hasParam("expectedCmusA", true)) {
        g_bmsSettings.expectedCmusA = request->getParam("expectedCmusA", true)->value().toInt();
    }
    if (request->hasParam("expectedCmusB", true)) {
        g_bmsSettings.expectedCmusB = request->getParam("expectedCmusB", true)->value().toInt();
    }

    if (hasSocCurve) {
        for (int i = 0; i < 4; ++i) g_bmsSettings.socVoltageCurve[i] = validatedSocCurve[i];
        Serial.printf("[Web] SOC curve set to [%d,%d,%d,%d]\n",
                      g_bmsSettings.socVoltageCurve[0], g_bmsSettings.socVoltageCurve[1],
                      g_bmsSettings.socVoltageCurve[2], g_bmsSettings.socVoltageCurve[3]);
    }

    if (request->hasParam("useVoltageSoc", true)) {
        String val = request->getParam("useVoltageSoc", true)->value();
        g_bmsSettings.useVoltageSoc = (val == "1" || val == "true");
        Serial.printf("[Web] useVoltageSoc set to %s\n", g_bmsSettings.useVoltageSoc ? "YES" : "NO");
    }

    settingsSave();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
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
    s_server.on("/api/module/9", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 9); });
    s_server.on("/api/module/10", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 10); });
    s_server.on("/api/module/11", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 11); });
    s_server.on("/api/module/12", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 12); });
    s_server.on("/api/module/13", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 13); });
    s_server.on("/api/module/14", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 14); });
    s_server.on("/api/module/15", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 15); });
    s_server.on("/api/module/16", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 16); });
    s_server.on("/api/module/17", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 17); });
    s_server.on("/api/module/18", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 18); });
    s_server.on("/api/module/19", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 19); });
    s_server.on("/api/module/20", HTTP_GET, [](AsyncWebServerRequest* r) { handleApiModuleN(r, 20); });

    s_server.on("/api/summary", HTTP_GET, handleApiSummary);
    s_server.on("/api/balancing/restart", HTTP_POST, handleApiBalanceRestart);
    // ESPAsyncWebServer resolves matching routes in registration order. Keep
    // the longer recovery route before the /api/balancing prefix.
    s_server.on("/api/balancing", HTTP_POST, handleApiBalancing);
    s_server.on("/api/command", HTTP_POST, handleApiCommand);
    s_server.on("/api/help", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildHelpJson());
    });
    s_server.on("/api/reboot", HTTP_POST, handleApiReboot);
    s_server.on("/api/config", HTTP_POST, handleApiConfig);

    // 404 handler
    s_server.onNotFound(handleNotFound);

    // Start server
    s_server.begin();
    Serial.println("[Web] Server started on port 80");
}

void webServerTick() {
    // Kept as a no-op for callers built against older firmware revisions.
}
