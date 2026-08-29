/**
 * @file web_server.cpp
 * @brief Async web server implementation
 *
 * REST API endpoints:
 *   GET  /api/bms       - Full BMS state (all modules)
 *   GET  /api/module/N  - Single module data (N = 1-20)
 *   GET  /api/summary   - Pack summary and safety state
 *   POST /api/balancing - Toggle balancing on/off
 *   POST /api/command   - Execute any serial command via the web UI/API
 *   POST /api/reboot    - Acknowledge, then reboot the device
 *   GET  /              - HTML dashboard
 */

#include "web_server.h"
#include "soc_curve_validation.h"
#include "soc_calc.h"
#include "config.h"
#include "bms_data.h"
#include "protection.h"
#include "can_handler.h"
#include "current_taper.h"
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
    json += "\"selectedForPack\":" + String(isModuleSelectedForPack(moduleIndex) ? "true" : "false") + ",";
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
    long lowestModuleVoltageMv = 0;
    long highestModuleVoltageMv = 0;
    String lowestModuleLabel;
    String highestModuleLabel;

    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (g_bmsState.modules[m].present) {
            presentCount++;
            // Unselected CMUs remain diagnostic-only and must not affect pack totals.
            if (isModuleSelectedForPack(m)) {
                for (int c = 0; c < CELLS_PER_MODULE; c++) {
                    if ((g_bmsState.modules[m].balanceStatus >> c) & 1) {
                        balancingCount++;
                    }
                }
            }
        }
    }

    // Count expected CMUs (10 per bus)
    for (int i = 0; i < 10; i++) {
        if (g_bmsSettings.expectedCmusA & (1 << i)) expectedCount++;
        if (g_bmsSettings.expectedCmusB & (1 << i)) expectedCount++;
    }

    for (int module = 0; module < BMS_MODULE_COUNT; ++module) {
        if (!isModuleSelectedForPack(module) || !g_bmsState.modules[module].present) continue;
        const long moduleVoltageMv = g_bmsState.modules[module].getModuleVoltageMv();
        if (moduleVoltageMv <= 0) continue;
        const String label = String("Bus ") + (module < 10 ? "A" : "B") +
                             " CMU " + String((module % 10) + 1);
        if (lowestModuleVoltageMv == 0 || moduleVoltageMv < lowestModuleVoltageMv) {
            lowestModuleVoltageMv = moduleVoltageMv;
            lowestModuleLabel = label;
        }
        if (moduleVoltageMv > highestModuleVoltageMv) {
            highestModuleVoltageMv = moduleVoltageMv;
            highestModuleLabel = label;
        }
    }

    // Calculate seconds since last CAN message
    unsigned long msSinceCan = (g_bmsState.lastCanMessageTime > 0)
        ? (millis() - g_bmsState.lastCanMessageTime)
        : 999999;

    const bool batterySafeToUse = digitalRead(PIN_BATTERY_SAFE_TO_USE) == HIGH;
    const CanStats canStats = canGetStats();
    const CurrentLimits currentLimits = currentTaperCalculate();
    char socCurveText[160] = {};
    formatSocCurvePoints(g_bmsSettings.socCurvePoints,
                         g_bmsSettings.socCurvePointCount,
                         socCurveText, sizeof(socCurveText));

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
    json += "\"socUnfiltered\":" + String(socUnfilteredPercent()) + ",";
    json += "\"socFilteredCellMv\":" + String(socFilteredCellMv()) + ",";
    json += "\"socCurvePoints\":\"" + String(socCurveText) + "\",";
    json += "\"chargeCurrentLimitA\":" + String(currentLimits.chargeDa / 10.0f, 1) + ",";
    json += "\"dischargeCurrentLimitA\":" + String(currentLimits.dischargeDa / 10.0f, 1) + ",";
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
    json += "\"lowestModuleVoltageMv\":" + String(lowestModuleVoltageMv) + ",";
    json += "\"highestModuleVoltageMv\":" + String(highestModuleVoltageMv) + ",";
    json += "\"moduleVoltageDeltaMv\":" +
            String(highestModuleVoltageMv > 0 && lowestModuleVoltageMv > 0
                       ? highestModuleVoltageMv - lowestModuleVoltageMv : 0) + ",";
    json += "\"lowestModuleLabel\":\"" + lowestModuleLabel + "\",";
    json += "\"highestModuleLabel\":\"" + highestModuleLabel + "\",";
    const int seriesCellCount = expectedCount * CELLS_PER_MODULE;
    json += "\"maxDesignVoltage\":" +
            String((g_bmsSettings.socVoltageCurve[2] * seriesCellCount) / 1000.0f, 1) + ",";
    json += "\"minDesignVoltage\":" +
            String((g_bmsSettings.socVoltageCurve[0] * seriesCellCount) / 1000.0f, 1) + ",";
    json += "\"expectedCmusA\":" + String(g_bmsSettings.expectedCmusA) + ",";
    json += "\"expectedCmusB\":" + String(g_bmsSettings.expectedCmusB) + ",";
    json += "\"currentTaper\":{";
    json += "\"chargeFullVoltageV\":" + String(g_bmsSettings.chargeFullVoltageMv / 1000.0f, 3) + ",";
    json += "\"chargeReducedVoltageV\":" + String(g_bmsSettings.chargeReducedVoltageMv / 1000.0f, 3) + ",";
    json += "\"chargeStopVoltageV\":" + String(g_bmsSettings.chargeStopVoltageMv / 1000.0f, 3) + ",";
    json += "\"chargeFullCurrentA\":" + String(g_bmsSettings.chargeFullCurrentDa / 10.0f, 1) + ",";
    json += "\"chargeReducedCurrentA\":" + String(g_bmsSettings.chargeReducedCurrentDa / 10.0f, 1) + ",";
    json += "\"dischargeFullVoltageV\":" + String(g_bmsSettings.dischargeFullVoltageMv / 1000.0f, 3) + ",";
    json += "\"dischargeReducedVoltageV\":" + String(g_bmsSettings.dischargeReducedVoltageMv / 1000.0f, 3) + ",";
    json += "\"dischargeStopVoltageV\":" + String(g_bmsSettings.dischargeStopVoltageMv / 1000.0f, 3) + ",";
    json += "\"dischargeFullCurrentA\":" + String(g_bmsSettings.dischargeFullCurrentDa / 10.0f, 1) + ",";
    json += "\"dischargeReducedCurrentA\":" + String(g_bmsSettings.dischargeReducedCurrentDa / 10.0f, 1) + "},";
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
                ",\"selectedForPack\":" + String(isModuleSelectedForPack(module) ? "true" : "false") +
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
        :root { --bg:#0d1220; --panel:#151d31; --panel2:#10182a; --line:#28344d; --text:#e7edf7; --muted:#8e9bb0; --good:#4ade80; --warn:#f59e0b; --bad:#ef4444; --blue:#60a5fa; }
        * { box-sizing:border-box; margin:0; padding:0; }
        body { max-width:1500px; margin:auto; padding:12px; background:var(--bg); color:var(--text); font:13px/1.35 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif; }
        h1 { margin:2px 0 10px; color:var(--good); text-align:center; font-size:1.35rem; letter-spacing:.02em; }
        .section { margin-bottom:10px; padding:10px; background:var(--panel); border:1px solid var(--line); border-radius:9px; }
        .section-title { margin-bottom:7px; color:#b7c5da; font-size:.72rem; font-weight:700; letter-spacing:.12em; text-transform:uppercase; }
        .metric-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(145px,1fr)); gap:7px; }
        .metric { min-width:0; padding:8px 9px; background:var(--panel2); border-radius:7px; text-align:center; }
        .metric-value { color:var(--good); font-size:1.35rem; font-weight:750; line-height:1.1; }
        .metric-label { margin-top:3px; color:var(--muted); font-size:.72rem; }
        .metric-note { margin-top:3px; color:#738198; font-size:.65rem; }
        .safety { border-color:#29563c; }
        .safety .metric-value { font-size:1.15rem; }
        .spread-row { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:10px; }
        .spread-row .section { min-width:0; }
        .spread { display:grid; grid-template-columns:repeat(3,1fr); gap:1px; overflow:hidden; padding:0; background:var(--line); }
        .spread > div { padding:8px 5px; background:var(--panel2); text-align:center; }
        .spread .metric-value { font-size:1.15rem; }
        .spread.two { grid-template-columns:repeat(2,1fr); }
        .modules { display:grid; grid-template-columns:repeat(auto-fit,minmax(260px,1fr)); gap:9px; }
        .bus-section { grid-column:1/-1; }
        .bus-summary { display:flex; justify-content:space-between; align-items:center; padding:7px 2px; border-bottom:1px solid var(--blue); color:var(--blue); cursor:pointer; font-size:1rem; font-weight:700; }
        .bus-summary-meta { color:var(--muted); font-size:.68rem; font-weight:400; }
        .bus-modules { display:grid; grid-template-columns:repeat(auto-fit,minmax(260px,1fr)); gap:9px; padding-top:9px; }
        .module { padding:9px; background:var(--panel); border:1px solid var(--line); border-radius:8px; }
        .module.offline { opacity:.42; }
        .module-header { margin-bottom:7px; padding-bottom:6px; border-bottom:1px solid var(--line); }
        .module-controls { display:flex; align-items:center; gap:6px; flex-wrap:wrap; }
        .expected-chk { cursor:pointer; }
        .module-title { font-weight:700; }
        .module-voltage { margin-left:8px; color:var(--good); font-size:.78rem; }
        .module-delta { margin-left:7px; color:var(--warn); font-size:.72rem; }
        .temps { display:block; margin-top:4px; color:#d8a84e; font-size:.7rem; }
        .cells { display:grid; grid-template-columns:repeat(4,1fr); gap:4px; }
        .cell { position:relative; padding:6px 3px; background:#142946; border:1px solid transparent; border-radius:5px; text-align:center; font-size:.78rem; }
        .cell.module-low { background:#253047; border-color:#8b6b2e; }
        .cell.pack-low { background:#5a2028; border-color:#fb7185; box-shadow:0 0 0 1px rgba(251,113,133,.18); }
        .cell.balancing { background:#39794e; border-color:var(--good); color:#fff; }
        .cell-num,.cell-delta { display:block; color:#75839a; font-size:.62rem; }
        .cell.pack-low .cell-num,.cell.pack-low .cell-delta,.cell.balancing .cell-num,.cell.balancing .cell-delta { color:#d8e0eb; }
        .controls { display:flex; justify-content:center; align-items:center; flex-wrap:wrap; gap:7px; }
        button { padding:7px 13px; border:0; border-radius:5px; background:var(--good); color:#07130b; font-size:.75rem; font-weight:700; cursor:pointer; }
        button:hover { filter:brightness(.9); }
        button.off { background:#586477; color:#fff; }
        button.recovery { background:var(--warn); color:#1f1603; }
        button.danger { background:#b73542; color:#fff; }
        button:disabled { opacity:.6; cursor:wait; }
        .taper-groups { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:10px; }
        .taper-group { padding:9px; background:var(--panel2); border-radius:7px; }
        .taper-group-title { margin-bottom:7px; color:var(--blue); font-size:.76rem; font-weight:700; }
        .taper-grid { display:grid; grid-template-columns:repeat(5,minmax(70px,1fr)); gap:6px; }
        .taper-field { min-width:0; color:var(--muted); font-size:.65rem; }
        .taper-field input { display:block; width:100%; margin-top:3px; padding:6px; border:1px solid #46536a; border-radius:4px; background:#0b1323; color:#fff; font:inherit; font-size:.74rem; }
        .taper-actions { margin-top:8px; text-align:right; }
        .taper-note { margin-right:8px; color:#69778d; font-size:.65rem; }
        .diagnostics { margin-top:12px; padding-top:9px; border-top:1px solid var(--line); }
        .diagnostics .controls { justify-content:flex-start; }
        .command-output { display:none; width:100%; min-height:80px; max-height:280px; margin-top:7px; overflow:auto; padding:8px; border-radius:6px; background:#080e1a; color:#bac6d8; white-space:pre-wrap; text-align:left; font-size:.68rem; }
        .status { margin-top:5px; color:#69778d; text-align:center; font-size:.66rem; }
        .error { color:var(--bad); }
        @media(max-width:760px) { .spread-row,.taper-groups{grid-template-columns:1fr} }
        @media(max-width:620px) { .taper-grid{grid-template-columns:repeat(2,1fr)} }
        @media(max-width:520px) { body{padding:7px}.metric-grid{grid-template-columns:repeat(2,1fr)}.spread .metric-value{font-size:1rem}.modules,.bus-modules{grid-template-columns:1fr} }
    </style>
</head>
<body>
    <h1>Outlander BMS Monitor</h1>

    <section class="section safety">
        <div class="section-title">Safety</div>
        <div class="metric-grid">
            <div class="metric"><div class="metric-value" id="batterySafeToUse">--</div><div class="metric-label">BATTERY_SAFE_TO_USE · GPIO15</div></div>
            <div class="metric"><div class="metric-value" id="protection">--</div><div class="metric-label">Protection</div></div>
            <div class="metric"><div class="metric-value" id="canStatus">--</div><div class="metric-label">CMU CAN</div></div>
            <div class="metric"><div class="metric-value" id="avgTemp">--</div><div class="metric-label">Average Temperature (°C)</div></div>
            <div class="metric"><div class="metric-value" id="maximumTemp">--</div><div class="metric-label">Maximum Temperature (°C)</div></div>
        </div>
    </section>

    <section class="section">
        <div class="section-title">Pack</div>
        <div class="metric-grid">
            <div class="metric"><div class="metric-value" id="soc">--</div><div class="metric-label">State of Charge</div></div>
            <div class="metric"><div class="metric-value" id="packVoltage">--</div><div class="metric-label">Pack Voltage (V)</div></div>
            <div class="metric"><div class="metric-value" id="modulesOnline">--</div><div class="metric-label">Modules Online</div><div class="metric-note" id="designVoltage">Design --</div></div>
            <div class="metric"><div class="metric-value" id="chargeLimit">--</div><div class="metric-label">Charge Limit (A)</div></div>
            <div class="metric"><div class="metric-value" id="dischargeLimit">--</div><div class="metric-label">Discharge Limit (A)</div></div>
        </div>
    </section>

    <div class="spread-row">
        <section class="section">
            <div class="section-title">Cell Voltage Spread · mV</div>
            <div class="spread">
                <div><div class="metric-value" id="lowestCell">--</div><div class="metric-label">Lowest Cell</div></div>
                <div><div class="metric-value" id="highestCell">--</div><div class="metric-label">Highest Cell</div></div>
                <div><div class="metric-value" id="voltageDelta">--</div><div class="metric-label">All-Cell Delta</div></div>
            </div>
        </section>

        <section class="section">
            <div class="section-title">Module Voltage Spread · mV</div>
            <div class="spread">
                <div><div class="metric-value" id="lowestModule">--</div><div class="metric-label">Lowest Module</div><div class="metric-note" id="lowestModuleLabel">--</div></div>
                <div><div class="metric-value" id="highestModule">--</div><div class="metric-label">Highest Module</div><div class="metric-note" id="highestModuleLabel">--</div></div>
                <div><div class="metric-value" id="moduleDelta">--</div><div class="metric-label">Module Delta</div></div>
            </div>
        </section>
    </div>

    <section class="section">
        <div class="section-title">Balancing</div>
        <div class="metric-grid">
            <div class="metric"><div class="metric-value" id="cellsBalancing">--</div><div class="metric-label">Cells Balancing</div></div>
            <div class="metric"><div class="metric-value" id="balanceTarget">--</div><div class="metric-label">8th-Lowest Target (mV)</div></div>
            <div class="metric"><button id="balanceBtn" onclick="toggleBalancing()">Balancing: OFF</button></div>
        </div>
    </section>

    <section class="section">
        <div class="section-title">Modules</div>
        <div class="modules" id="modulesContainer"></div>
    </section>

    <section class="section">
        <div class="section-title">Configuration & Actions</div>
        <div class="controls">
            <button id="debugBtn" class="off" onclick="sendCommand('d')">Debug: OFF</button>
            <button id="voltageOverrideBtn" class="recovery" onclick="toggleVoltageOverride()">Enable Voltage Override</button>
            <button id="rebootBtn" class="danger" onclick="rebootDevice()">Reboot</button>
        </div>
        <div class="diagnostics">
            <div class="controls">
                <button class="off" onclick="runDiagnostic('r')">Full Report</button>
                <button class="off" onclick="runDiagnostic('s')">Detailed Stats</button>
                <button class="off" onclick="runDiagnostic('c')">CAN Diagnostics</button>
            </div>
            <pre id="commandOutput" class="command-output"></pre>
        </div>
    </section>

    <section class="section">
        <div class="section-title">Current Taper Parameters</div>
        <form id="taperForm" onsubmit="saveTaper(event)">
            <div class="taper-groups">
                <div class="taper-group">
                    <div class="taper-group-title">Charge · governed by highest selected cell</div>
                    <div class="taper-grid">
                        <label class="taper-field">Full voltage (V)<input id="chargeFullVoltage" type="number" min="2.5" max="4.199" step="0.001" required></label>
                        <label class="taper-field">Reduced voltage (V)<input id="chargeReducedVoltage" type="number" min="2.5" max="4.199" step="0.001" required></label>
                        <label class="taper-field">Stop voltage (V)<input id="chargeStopVoltage" type="number" min="2.5" max="4.199" step="0.001" required></label>
                        <label class="taper-field">Full current (A)<input id="chargeFullCurrent" type="number" min="0.1" max="100" step="0.1" required></label>
                        <label class="taper-field">Reduced current (A)<input id="chargeReducedCurrent" type="number" min="0.1" max="100" step="0.1" required></label>
                    </div>
                </div>
                <div class="taper-group">
                    <div class="taper-group-title">Discharge · governed by lowest selected cell</div>
                    <div class="taper-grid">
                        <label class="taper-field">Full voltage (V)<input id="dischargeFullVoltage" type="number" min="2.5" max="4.5" step="0.001" required></label>
                        <label class="taper-field">Reduced voltage (V)<input id="dischargeReducedVoltage" type="number" min="2.5" max="4.5" step="0.001" required></label>
                        <label class="taper-field">Stop voltage (V)<input id="dischargeStopVoltage" type="number" min="2.801" max="4.5" step="0.001" required></label>
                        <label class="taper-field">Full current (A)<input id="dischargeFullCurrent" type="number" min="0.1" max="100" step="0.1" required></label>
                        <label class="taper-field">Reduced current (A)<input id="dischargeReducedCurrent" type="number" min="0.1" max="100" step="0.1" required></label>
                    </div>
                </div>
            </div>
            <div class="taper-actions"><span class="taper-note">Saved atomically and retained across reboot.</span><button type="submit">Save Taper</button></div>
        </form>
    </section>

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
        let supervisedOverrideActive = false;
        const busOpenState = { A: null, B: null };

        function setBusOpen(bus, isOpen) {
            busOpenState[bus] = isOpen;
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
            document.getElementById('maximumTemp').textContent = hasData ? summary.highestTemp : na;
            document.getElementById('chargeLimit').textContent = hasData ? summary.chargeCurrentLimitA.toFixed(1) : na;
            document.getElementById('dischargeLimit').textContent = hasData ? summary.dischargeCurrentLimitA.toFixed(1) : na;
            document.getElementById('designVoltage').textContent =
                `Design ${summary.minDesignVoltage.toFixed(1)}–${summary.maxDesignVoltage.toFixed(1)} V`;
            document.getElementById('lowestModule').textContent = summary.lowestModuleVoltageMv || na;
            document.getElementById('highestModule').textContent = summary.highestModuleVoltageMv || na;
            document.getElementById('moduleDelta').textContent = summary.moduleVoltageDeltaMv || 0;
            document.getElementById('lowestModuleLabel').textContent = summary.lowestModuleLabel || '--';
            document.getElementById('highestModuleLabel').textContent = summary.highestModuleLabel || '--';

            const taper = summary.currentTaper || {};
            setTaperValue('chargeFullVoltage', taper.chargeFullVoltageV, 3);
            setTaperValue('chargeReducedVoltage', taper.chargeReducedVoltageV, 3);
            setTaperValue('chargeStopVoltage', taper.chargeStopVoltageV, 3);
            setTaperValue('chargeFullCurrent', taper.chargeFullCurrentA, 1);
            setTaperValue('chargeReducedCurrent', taper.chargeReducedCurrentA, 1);
            setTaperValue('dischargeFullVoltage', taper.dischargeFullVoltageV, 3);
            setTaperValue('dischargeReducedVoltage', taper.dischargeReducedVoltageV, 3);
            setTaperValue('dischargeStopVoltage', taper.dischargeStopVoltageV, 3);
            setTaperValue('dischargeFullCurrent', taper.dischargeFullCurrentA, 1);
            setTaperValue('dischargeReducedCurrent', taper.dischargeReducedCurrentA, 1);

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
            supervisedOverrideActive = !!summary.supervisedOverrideActive;
            const overrideBtn = document.getElementById('voltageOverrideBtn');
            overrideBtn.textContent = supervisedOverrideActive ? 'Cancel Voltage Override' : 'Enable Voltage Override';
            overrideBtn.className = supervisedOverrideActive ? 'danger' : 'recovery';
            
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

                    if (mod.present && isExpected) onlineCount++;

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
                    const moduleMaximumTemp = mod.maximumTemperature === null || mod.maximumTemperature === undefined
                        ? '--'
                        : mod.maximumTemperature.toFixed(1) + '°C';
                    busHtml += `<span class="temps">${mod.temperatures.map(t => t.toFixed(1) + '°C').join(' | ')} · Max ${moduleMaximumTemp}</span>`;
                    busHtml += `</div>`;
                    busHtml += `<div class="cells">`;

                    for (let i = 0; i < mod.voltages.length; i++) {
                        const v = mod.voltages[i];
                        const isBalancing = mod.balancing[i];
                        const hasV = v > 0;
                        const cellDelta = hasV ? v - modMin : 0;

                        let cellClass = 'cell';
                        if (isBalancing) cellClass += ' balancing';
                        else if (hasV && mod.selectedForPack && v === lowestCellMv) cellClass += ' pack-low';
                        else if (hasV && v === modMin) cellClass += ' module-low';

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
            document.getElementById('cellsBalancing').textContent = summary.cellsBalancing;
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
            let twaiText = 'TWAI B: ' + (twai.state || 'UNAVAILABLE') +
                ' | TX err ' + (twai.txErrorCounter ?? '--') +
                ' | TX failed ' + (twai.txFailedCount ?? '--') +
                ' | bus err ' + (twai.busErrorCount ?? '--') +
                ' | arb lost ' + (twai.arbLostCount ?? '--');
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

        async function sendCommand(command, confirmation, showOutput = false) {
            if (confirmation && !window.confirm(confirmation)) return;
            const output = document.getElementById('commandOutput');
            if (showOutput) output.style.display = 'block';
            try {
                const formData = new FormData();
                formData.append('command', command);
                const response = await fetch('/api/command', { method: 'POST', body: formData });
                const payload = await response.json();
                if (showOutput) output.textContent = JSON.stringify(payload, null, 2);
                if (!response.ok) throw new Error(payload.error || ('HTTP ' + response.status));
                fetchData();
            } catch (err) {
                if (showOutput) output.textContent = 'Command failed: ' + err.message;
                else {
                    document.getElementById('status').textContent = 'Command failed: ' + err.message;
                    document.getElementById('status').className = 'status error';
                }
            }
        }

        function runDiagnostic(command) { sendCommand(command, null, true); }

        function setTaperValue(id, value, digits) {
            const input = document.getElementById(id);
            if (document.activeElement !== input && Number.isFinite(value)) {
                input.value = value.toFixed(digits);
            }
        }

        function toggleVoltageOverride() {
            if (supervisedOverrideActive) {
                sendCommand('O');
                return;
            }
            sendCommand('o', 'Enable the 10-minute supervised voltage recovery override? Confirm fresh CMU data and prepare the charge/discharge procedure first.');
        }

        async function saveTaper(event) {
            event.preventDefault();
            const ids = [
                'chargeFullVoltage', 'chargeReducedVoltage', 'chargeStopVoltage',
                'chargeFullCurrent', 'chargeReducedCurrent',
                'dischargeFullVoltage', 'dischargeReducedVoltage', 'dischargeStopVoltage',
                'dischargeFullCurrent', 'dischargeReducedCurrent'
            ];
            const values = ids.map(id => document.getElementById(id).value.trim());
            if (values.some(value => value === '')) return;
            if (!window.confirm('Save these charge and discharge taper parameters?')) return;

            const formData = new FormData();
            formData.append('currentTaper', values.join(','));
            const status = document.getElementById('status');
            try {
                const response = await fetch('/api/config', { method: 'POST', body: formData });
                const payload = await response.json();
                if (!response.ok) throw new Error(payload.error || ('HTTP ' + response.status));
                status.textContent = 'Current taper saved.';
                status.className = 'status';
                fetchData();
            } catch (err) {
                status.textContent = 'Taper save failed: ' + err.message;
                status.className = 'status error';
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
    const bool hasSocCurvePoints = request->hasParam("socCurvePoints", true);
    if (hasSocCurve && hasSocCurvePoints) {
        request->send(400, "application/json",
                      "{\"error\":\"Specify socCurve or socCurvePoints, not both\"}");
        return;
    }

    SocCurvePoint validatedSocPoints[SOC_CURVE_MAX_POINTS] = {};
    uint8_t validatedSocPointCount = 0;
    if (hasSocCurve) {
        const String curve = request->getParam("socCurve", true)->value();
        if (!parseSocVoltageCurve(curve.c_str(), validatedSocCurve)) {
            request->send(400, "application/json", "{\"error\":\"Invalid SOC curve\"}");
            return;
        }
        validatedSocPoints[0] = {validatedSocCurve[0], validatedSocCurve[1]};
        validatedSocPoints[1] = {validatedSocCurve[2], validatedSocCurve[3]};
        validatedSocPointCount = 2;
    } else if (hasSocCurvePoints) {
        const String curve = request->getParam("socCurvePoints", true)->value();
        if (!parseSocCurvePoints(curve.c_str(), validatedSocPoints,
                                 validatedSocPointCount)) {
            request->send(400, "application/json", "{\"error\":\"Invalid SOC curve points\"}");
            return;
        }
    }

    BmsSettings validatedTaper = g_bmsSettings;
    const bool hasCurrentTaper = request->hasParam("currentTaper", true);
    if (hasCurrentTaper &&
        !parseCurrentTaperConfig(request->getParam("currentTaper", true)->value().c_str(),
                                 validatedTaper)) {
        request->send(400, "application/json", "{\"error\":\"Invalid current taper\"}");
        return;
    }
    if (hasCurrentTaper) {
        g_bmsSettings = validatedTaper;
        Serial.println("[Web] Current taper updated atomically");
    }

    if (request->hasParam("expectedCmusA", true)) {
        g_bmsSettings.expectedCmusA = request->getParam("expectedCmusA", true)->value().toInt();
    }
    if (request->hasParam("expectedCmusB", true)) {
        g_bmsSettings.expectedCmusB = request->getParam("expectedCmusB", true)->value().toInt();
    }

    if (hasSocCurve || hasSocCurvePoints) {
        settingsSetSocCurvePoints(validatedSocPoints, validatedSocPointCount);
        socResetFilter();
        Serial.printf("[Web] SOC curve set to %u validated points\n",
                      static_cast<unsigned>(validatedSocPointCount));
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
