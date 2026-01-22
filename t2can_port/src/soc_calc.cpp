/**
 * @file soc_calc.cpp
 * @brief SOC calculation implementation
 */

#include "soc_calc.h"
#include "bms_data.h"
#include <Preferences.h>

// Preferences object for NVS storage
static Preferences s_preferences;

// Constants
static const char* NVS_NAMESPACE = "bms";
static const char* NVS_SOC_KEY = "soc";

void socInit() {
    // Try to load saved SOC
    if (g_bmsSettings.useVoltageSoc) {
        // Voltage-based SOC mode - always calculate from voltage
        Serial.println("[SOC] Using voltage-based SOC calculation");
        g_bmsState.soc = socCalculateFromVoltage();
        g_bmsState.ampSeconds = (g_bmsState.soc * g_bmsSettings.capacityAh * 
                                  g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    } else if (socLoad()) {
        // Loaded from NVS
        Serial.printf("[SOC] Loaded SOC from NVS: %d%%\n", g_bmsState.soc);
        g_bmsState.ampSeconds = (g_bmsState.soc * g_bmsSettings.capacityAh * 
                                  g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    } else {
        // No saved SOC, calculate from voltage
        Serial.println("[SOC] No saved SOC, calculating from voltage");
        g_bmsState.soc = socCalculateFromVoltage();
        g_bmsState.ampSeconds = (g_bmsState.soc * g_bmsSettings.capacityAh * 
                                  g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    }
    
    g_bmsState.socInitialized = true;
    g_bmsState.lastSocUpdate = millis();
    
    Serial.printf("[SOC] Initialized: %d%% (%.2f Ah equivalent)\n", 
                  g_bmsState.soc, g_bmsState.ampSeconds * 0.27777777777778f / 1000.0f);
}

void socUpdate() {
    if (!g_bmsState.socInitialized) {
        return;
    }
    
    unsigned long currentTime = millis();
    unsigned long deltaMs = currentTime - g_bmsState.lastSocUpdate;
    
    if (deltaMs == 0) {
        return; // No time has passed
    }
    
    // Update amp-seconds based on current flow
    // currentAmps is positive for charging, negative for discharging
    // deltaMs is in milliseconds, so divide by 1000 to get seconds
    float deltaSeconds = deltaMs / 1000.0f;
    g_bmsState.ampSeconds += g_bmsState.currentAmps * deltaSeconds;
    
    // Calculate SOC from amp-seconds
    // Formula from V2: SOC = ((ampsecond * 0.27777777777778) / (CAP * Pstrings * 1000)) * 100
    // Where 0.27777777777778 = 1/3600 (converts amp-seconds to amp-hours)
    if (g_bmsSettings.useVoltageSoc || g_bmsSettings.currentSensorType == 0) {
        // Voltage-based SOC or no current sensor
        g_bmsState.soc = socCalculateFromVoltage();
        // Update amp-seconds to match voltage-based SOC
        g_bmsState.ampSeconds = (g_bmsState.soc * g_bmsSettings.capacityAh * 
                                  g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    } else {
        // Coulomb-counting based SOC
        float totalCapacityAs = g_bmsSettings.capacityAh * g_bmsSettings.parallelStrings * 1000.0f;
        g_bmsState.soc = (int)((g_bmsState.ampSeconds * 0.27777777777778f / totalCapacityAs) * 100.0f);
    }
    
    // Limit SOC to 0-100%
    if (g_bmsState.soc > 100) {
        g_bmsState.soc = 100;
        // Reset amp-seconds to full capacity
        g_bmsState.ampSeconds = (g_bmsSettings.capacityAh * g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    }
    
    if (g_bmsState.soc < 0) {
        g_bmsState.soc = 0;
        // Note: amp-seconds can go negative (deep discharge), but we cap displayed SOC at 0%
    }
    
    g_bmsState.lastSocUpdate = currentTime;
}

void socReset(int socPercent) {
    if (socPercent < 0) socPercent = 0;
    if (socPercent > 100) socPercent = 100;
    
    g_bmsState.soc = socPercent;
    // Reset amp-seconds to match new SOC
    g_bmsState.ampSeconds = (socPercent * g_bmsSettings.capacityAh * 
                              g_bmsSettings.parallelStrings * 1000.0f) / 0.27777777777778f;
    
    Serial.printf("[SOC] Reset to %d%%\n", socPercent);
    
    // Save to NVS
    socSave();
}

int socCalculateFromVoltage() {
    // Get lowest cell voltage (in mV)
    long lowCellMv = g_bmsState.lowestCellMv;
    
    if (lowCellMv <= 0 || lowCellMv > 5000) {
        // Invalid voltage, return current SOC or 50% if uninitialized
        return g_bmsState.socInitialized ? g_bmsState.soc : 50;
    }
    
    // Linear interpolation between two points on voltage curve
    // socVoltageCurve = [lowVolt_mV, lowSOC_%, highVolt_mV, highSOC_%]
    int lowVolt = g_bmsSettings.socVoltageCurve[0];
    int lowSoc = g_bmsSettings.socVoltageCurve[1];
    int highVolt = g_bmsSettings.socVoltageCurve[2];
    int highSoc = g_bmsSettings.socVoltageCurve[3];
    
    // map() function: map(value, fromLow, fromHigh, toLow, toHigh)
    int soc = map((int)lowCellMv, lowVolt, highVolt, lowSoc, highSoc);
    
    // Constrain to 0-100%
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    
    return soc;
}

void socSave() {
    s_preferences.begin(NVS_NAMESPACE, false); // Read/write mode
    s_preferences.putInt(NVS_SOC_KEY, g_bmsState.soc);
    s_preferences.end();
    
    Serial.printf("[SOC] Saved to NVS: %d%%\n", g_bmsState.soc);
}

bool socLoad() {
    s_preferences.begin(NVS_NAMESPACE, true); // Read-only mode
    int savedSoc = s_preferences.getInt(NVS_SOC_KEY, -1);
    s_preferences.end();
    
    if (savedSoc >= 0 && savedSoc <= 100) {
        g_bmsState.soc = savedSoc;
        return true;
    }
    
    return false;
}
