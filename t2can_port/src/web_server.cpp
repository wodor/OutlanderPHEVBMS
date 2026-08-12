/**
 * @file web_server.cpp
 * @brief Async web server implementation
 *
 * REST API endpoints:
 *   GET  /api/bms       - Full BMS state (all modules)
 *   GET  /api/module/N  - Single module data (N = 1-8)
 *   GET  /api/summary   - Pack summary (lowest cell, balancing status)
 *   POST /api/balancing - Toggle balancing on/off
 *   POST /api/reboot    - Acknowledge, then reboot the device
 *   GET  /              - HTML dashboard
 */

#include "web_server.h"
#include "config.h"
#include "bms_data.h"
#include "protection.h"
#include "ess_control.h"
#include "simpbms_can.h"
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
    const SimpBmsDesignVoltageLimits designLimits = simpBmsGetDesignVoltageLimits();
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
    json += "\"simpBmsEnabled\":" + String(g_bmsSettings.simpBmsEnabled ? "true" : "false") + ",";
    json += "\"simpBmsMaxDesignVoltageV\":" + String(designLimits.maxVoltageV, 1) + ",";
    json += "\"simpBmsMinDesignVoltageV\":" + String(designLimits.minVoltageV, 1) + ",";
    json += "\"simpBmsSeriesCells\":" + String(designLimits.seriesCells) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.balanceTargetMv : 0);
    json += "}";

    return json;
}

/**
 * Build JSON summary with V2 features.
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

    // Read IO states (active HIGH)
    const bool acPresent = digitalRead(PIN_INPUT_AC_PRESENT) == HIGH;
    const bool keyOn = digitalRead(PIN_INPUT_KEY_ON) == HIGH;
    const bool auxIn = digitalRead(PIN_INPUT_AUX) == HIGH;
    const bool outMain = digitalRead(PIN_OUT_CONTACTOR_MAIN) == HIGH;
    const bool outPrecharge = digitalRead(PIN_OUT_PRECHARGE) == HIGH;
    const bool outNeg = digitalRead(PIN_OUT_CONTACTOR_NEG) == HIGH;
    const bool outCharger = digitalRead(PIN_OUT_CHARGER_EN) == HIGH;
    const bool outDischarge = digitalRead(PIN_OUT_DISCHARGE_EN) == HIGH;
    const SimpBmsDesignVoltageLimits designLimits = simpBmsGetDesignVoltageLimits();
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
    json += "\"currentAmps\":" + String(g_bmsState.currentAmps, 2) + ",";
    json += "\"avgCurrentAmps\":" + String(g_bmsState.avgCurrentAmps, 2) + ",";
    json += "\"simpBmsEnabled\":" + String(g_bmsSettings.simpBmsEnabled ? "true" : "false") + ",";
    json += "\"simpBmsMaxDesignVoltageV\":" + String(designLimits.maxVoltageV, 1) + ",";
    json += "\"simpBmsMinDesignVoltageV\":" + String(designLimits.minVoltageV, 1) + ",";
    json += "\"simpBmsSeriesCells\":" + String(designLimits.seriesCells) + ",";
    json += "\"balancingEnabled\":" + String(g_bmsState.balancingEnabled ? "true" : "false") + ",";
    json += "\"balanceTargetMv\":" + String(g_bmsState.balancingEnabled ? g_bmsState.balanceTargetMv : 0) + ",";
    json += "\"cellsBalancing\":" + String(balancingCount) + ",";
    json += "\"protectionStatus\":\"" + String(protectionGetStatus()) + "\",";
    json += "\"essState\":\"" + String(essGetStateName()) + "\",";
    json += "\"contactorClosed\":" + String(essIsContactorClosed() ? "true" : "false") + ",";
    json += "\"chargerEnabled\":" + String(g_bmsState.chargerEnabled ? "true" : "false") + ",";
    json += "\"msSinceCanMsg\":" + String(msSinceCan) + ",";
    json += "\"uptimeMs\":" + String(millis()) + ",";
    json += "\"balanceTxAttempts\":" + String(canStats.balanceTxAttempts) + ",";
    json += "\"balanceTxQueued\":" + String(canStats.balanceTxQueued) + ",";
    json += "\"lastBalanceTargetMv\":" + String(canStats.lastBalanceTargetMv) + ",";
    json += "\"lastBalanceBusMask\":" + String(canStats.lastBalanceBusMask) + ",";
    json += "\"msSinceBalanceCommand\":" + String(canStats.lastBalanceCommandTime > 0 ? millis() - canStats.lastBalanceCommandTime : 0) + ",";
    json += "\"hasData\":" + String(presentCount > 0 ? "true" : "false") + ",";
    json += "\"expectedTotal\":" + String(expectedCount) + ",";
    json += "\"expectedCmusA\":" + String(g_bmsSettings.expectedCmusA) + ",";
    json += "\"expectedCmusB\":" + String(g_bmsSettings.expectedCmusB) + ",";
    json += "\"io\":{";
    json += "\"inputs\":{";
    json += "\"acPresent\":" + String(acPresent ? "true" : "false") + ",";
    json += "\"keyOn\":" + String(keyOn ? "true" : "false") + ",";
    json += "\"auxIn\":" + String(auxIn ? "true" : "false") + ",";
    json += "\"curLowAmps\":" + String(g_bmsState.currentSenseLowAmps, 2) + ",";
    json += "\"curHighAmps\":" + String(g_bmsState.currentSenseHighAmps, 2);
    json += "},";
    json += "\"outputs\":{";
    json += "\"main\":" + String(outMain ? "true" : "false") + ",";
    json += "\"precharge\":" + String(outPrecharge ? "true" : "false") + ",";
    json += "\"negContactor\":" + String(outNeg ? "true" : "false") + ",";
    json += "\"chargerEn\":" + String(outCharger ? "true" : "false") + ",";
    json += "\"dischargeEn\":" + String(outDischarge ? "true" : "false");
    json += "}}";
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
        .ess-summary {
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
        button:disabled { cursor: wait; opacity: 0.65; }
        .status { text-align: center; margin-top: 10px; color: #666; font-size: 0.85em; }
        .uptime { margin-top: 2px; }
        .error { color: #ef4444; }
    </style>
</head>
<body>
    <h1>Outlander BMS Monitor</h1>

    <div class="ess-summary">
        <div class="summary-item">
            <div class="summary-value" id="essState">--</div>
            <div class="summary-label">ESS State</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="contactorState">--</div>
            <div class="summary-label">Contactor</div>
        </div>
        <div class="summary-item">
            <div class="summary-value" id="chargerState">--</div>
            <div class="summary-label">Charger</div>
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
        <div class="summary-item">
            <div class="summary-value" id="simpBmsDesignLimits">--</div>
            <div class="summary-label">BE Design Max / Min (V)</div>
        </div>
    </div>

    <div class="protection-row">
        <span class="summary-label">Protection</span>
        <span class="summary-value" id="protection">--</span>
    </div>

    <div class="modules" id="modulesContainer"></div>

    <div class="controls">
        <button id="balanceBtn" onclick="toggleBalancing()">Balancing: OFF</button>
        <button id="rebootBtn" class="danger" onclick="rebootDevice()">Reboot Device</button>
    </div>

    <div class="io-summary">
        <div class="io-block">
            <div class="io-title">Inputs</div>
            <div class="io-grid">
                <div class="io-item"><span class="io-label">AC_PRESENT</span><span class="io-value" id="ioAcPresent">--</span></div>
                <div class="io-item"><span class="io-label">KEY_ON</span><span class="io-value" id="ioKeyOn">--</span></div>
                <div class="io-item"><span class="io-label">AUX_IN</span><span class="io-value" id="ioAuxIn">--</span></div>
                <div class="io-item"><span class="io-label">CUR_LOW (A)</span><span class="io-value" id="ioCurLow">--</span></div>
                <div class="io-item"><span class="io-label">CUR_HIGH (A)</span><span class="io-value" id="ioCurHigh">--</span></div>
            </div>
        </div>
        <div class="io-block">
            <div class="io-title">Outputs</div>
            <div class="io-grid">
                <div class="io-item"><span class="io-label">MAIN</span><span class="io-value" id="ioMain">--</span></div>
                <div class="io-item"><span class="io-label">PRECHG</span><span class="io-value" id="ioPrecharge">--</span></div>
                <div class="io-item"><span class="io-label">NEG_CONT</span><span class="io-value" id="ioNegCont">--</span></div>
                <div class="io-item"><span class="io-label">CHG_EN</span><span class="io-value" id="ioChgEn">--</span></div>
                <div class="io-item"><span class="io-label">DISCHG_EN</span><span class="io-value" id="ioDischgEn">--</span></div>
            </div>
        </div>
    </div>

    <div class="status" id="status">Connecting...</div>
    <div class="status uptime" id="uptime">Uptime: --</div>
    <div class="status uptime" id="balanceDiagnostics">Balance CAN: --</div>

    <script>
        let balancingEnabled = false;
        let lowestCellMv = 5000;
        let expectedMaskA = 0;
        let expectedMaskB = 0;
        let rebooting = false;
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

            const designEl = document.getElementById('simpBmsDesignLimits');
            if (summary.simpBmsEnabled) {
                designEl.textContent = summary.simpBmsMaxDesignVoltageV.toFixed(1) + ' / ' +
                    summary.simpBmsMinDesignVoltageV.toFixed(1);
                designEl.style.color = '#93c5fd';
            } else {
                designEl.textContent = 'OFF';
                designEl.style.color = '#6b7280';
            }
            
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

            // ESS state and contactor/charger status
            const essEl = document.getElementById('essState');
            essEl.textContent = summary.essState;
            if (summary.essState === 'CONTACTOR_ON') {
                essEl.style.color = '#4ade80';
            } else if (summary.essState === 'PRECHARGE') {
                essEl.style.color = '#f59e0b';
            } else if (summary.essState === 'FAULT') {
                essEl.style.color = '#ef4444';
            } else {
                essEl.style.color = '#888';
            }

            const contactorEl = document.getElementById('contactorState');
            contactorEl.textContent = summary.contactorClosed ? 'ON' : 'OFF';
            if (summary.contactorClosed) {
                contactorEl.style.color = '#4ade80';
            } else if (summary.essState === 'FAULT') {
                contactorEl.style.color = '#ef4444';
            } else {
                contactorEl.style.color = '#6b7280';
            }

            const chargerEl = document.getElementById('chargerState');
            chargerEl.textContent = summary.chargerEnabled ? 'ENABLED' : 'DISABLED';
            chargerEl.style.color = summary.chargerEnabled ? '#4ade80' : '#6b7280';

            const io = summary.io || {};
            const inputs = io.inputs || {};
            const outputs = io.outputs || {};
            setIoState('ioAcPresent', !!inputs.acPresent);
            setIoState('ioKeyOn', !!inputs.keyOn);
            setIoState('ioAuxIn', !!inputs.auxIn);
            setIoValue('ioCurLow', inputs.curLowAmps);
            setIoValue('ioCurHigh', inputs.curHighAmps);
            setIoState('ioMain', !!outputs.main);
            setIoState('ioPrecharge', !!outputs.precharge);
            setIoState('ioNegCont', !!outputs.negContactor);
            setIoState('ioChgEn', !!outputs.chargerEn);
            setIoState('ioDischgEn', !!outputs.dischargeEn);
            
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
    if (request->hasParam("expectedCmusA", true)) {
        g_bmsSettings.expectedCmusA = request->getParam("expectedCmusA", true)->value().toInt();
    }
    if (request->hasParam("expectedCmusB", true)) {
        g_bmsSettings.expectedCmusB = request->getParam("expectedCmusB", true)->value().toInt();
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
    s_server.on("/api/balancing", HTTP_POST, handleApiBalancing);
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
