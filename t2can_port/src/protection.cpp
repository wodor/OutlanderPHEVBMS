/**
 * @file protection.cpp
 * @brief Protection system implementation
 */

#include "protection.h"
#include "bms_data.h"

// Protection fault flags
static bool s_overVoltFault = false;
static bool s_underVoltFault = false;
static bool s_overTempFault = false;
static bool s_underTempFault = false;
static bool s_cellImbalanceFault = false;
static bool s_commFault = false;

// Fault latch times (for debouncing)
static unsigned long s_underVoltTime = 0;
static unsigned long s_overVoltTime = 0;
static const unsigned long FAULT_DEBOUNCE_MS = 1000;  // 1 second debounce

void protectionInit() {
    protectionClearFaults();
    Serial.println("[PROTECTION] System initialized");
}

bool protectionCheck() {
    bool allOk = true;
    
    // Check if we have valid data
    if (!g_bmsState.hasAnyData()) {
        return true;  // No data yet, assume OK
    }
    
    // Update pack statistics first
    g_bmsState.updatePackStatistics();
    
    float lowCellV = g_bmsState.lowestCellMv / 1000.0f;   // Convert to volts
    float highCellV = g_bmsState.highestCellMv / 1000.0f;
    float lowTemp = g_bmsState.lowestTemp;
    float highTemp = g_bmsState.highestTemp;
    
    // SAFETY: Validate voltage readings are in reasonable range
    // Extreme values (>10V per cell) indicate sensor failure or memory corruption
    if (highCellV > 10.0f) {
        Serial.printf("[PROTECTION] CRITICAL: Voltage reading exceeds safety limit: %.3fV\n", highCellV);
        s_overVoltFault = true;
        return false; // Immediate fault
    }
    
    // SAFETY: Check for NaN or infinity in measurements
    if (isnan(highCellV) || isinf(highCellV) || isnan(lowCellV) || isinf(lowCellV)) {
        Serial.println("[PROTECTION] CRITICAL: Invalid voltage measurement");
        return false; // Immediate fault
    }
    
    if (isnan(highTemp) || isinf(highTemp) || isnan(lowTemp) || isinf(lowTemp)) {
        Serial.println("[PROTECTION] CRITICAL: Invalid temperature measurement");
        return false; // Immediate fault
    }
    
    // Check overvoltage
    if (highCellV > g_bmsSettings.overVoltage) {
        if (!s_overVoltFault) {
            s_overVoltFault = true;
            s_overVoltTime = millis();
            Serial.printf("[PROTECTION] OVERVOLTAGE FAULT: %.3fV > %.3fV\n", 
                         highCellV, g_bmsSettings.overVoltage);
        }
        allOk = false;
    } else if (highCellV < (g_bmsSettings.overVoltage - 0.1f)) {
        // Clear with hysteresis
        s_overVoltFault = false;
    }
    
    // Check undervoltage (with debounce to avoid spurious trips during high discharge)
    if (lowCellV < g_bmsSettings.underVoltage) {
        if (s_underVoltTime == 0) {
            s_underVoltTime = millis();
        } else {
            // SAFETY: Handle millis() rollover in debounce calculation
            unsigned long elapsed = millis() - s_underVoltTime;
            if (elapsed > FAULT_DEBOUNCE_MS) {
                if (!s_underVoltFault) {
                    s_underVoltFault = true;
                    Serial.printf("[PROTECTION] UNDERVOLTAGE FAULT: %.3fV < %.3fV\n", 
                                 lowCellV, g_bmsSettings.underVoltage);
                }
                allOk = false;
            }
        }
    } else if (lowCellV > (g_bmsSettings.underVoltage + g_bmsSettings.dischargeHysteresis)) {
        // Clear with hysteresis
        s_underVoltFault = false;
        s_underVoltTime = 0;
    }
    
    // Check overtemperature
    if (highTemp > g_bmsSettings.overTemp) {
        if (!s_overTempFault) {
            s_overTempFault = true;
            Serial.printf("[PROTECTION] OVERTEMPERATURE FAULT: %.1fC > %.1fC\n", 
                         highTemp, g_bmsSettings.overTemp);
        }
        allOk = false;
    } else if (highTemp < (g_bmsSettings.overTemp - g_bmsSettings.tempWarningOffset)) {
        s_overTempFault = false;
    }
    
    // Check undertemperature
    if (lowTemp < g_bmsSettings.underTemp) {
        if (!s_underTempFault) {
            s_underTempFault = true;
            Serial.printf("[PROTECTION] UNDERTEMPERATURE FAULT: %.1fC < %.1fC\n", 
                         lowTemp, g_bmsSettings.underTemp);
        }
        allOk = false;
    } else if (lowTemp > (g_bmsSettings.underTemp + g_bmsSettings.tempWarningOffset)) {
        s_underTempFault = false;
    }
    
    // Check cell imbalance
    float cellDelta = highCellV - lowCellV;
    if (cellDelta > g_bmsSettings.cellGap) {
        if (!s_cellImbalanceFault) {
            s_cellImbalanceFault = true;
            Serial.printf("[PROTECTION] CELL IMBALANCE WARNING: %.3fV gap (%.3fV - %.3fV)\n", 
                         cellDelta, highCellV, lowCellV);
        }
        // Note: Cell imbalance is a warning, not a hard fault
    } else if (cellDelta < (g_bmsSettings.cellGap * 0.8f)) {
        s_cellImbalanceFault = false;
    }
    
    // Check communication with expected CMUs
    bool allCmusOk = true;
    for (int m = 0; m < 10; m++) {
        // Bus A
        if (g_bmsSettings.expectedCmusA & (1 << m)) {
            if (!g_bmsState.modules[m].present || (millis() - g_bmsState.modules[m].lastSeenTime > 5000)) {
                allCmusOk = false;
            }
        }
        // Bus B
        if (g_bmsSettings.expectedCmusB & (1 << m)) {
            int idx = m + 10;
            if (!g_bmsState.modules[idx].present || (millis() - g_bmsState.modules[idx].lastSeenTime > 5000)) {
                allCmusOk = false;
            }
        }
    }

    // Apply a startup grace period of 10 seconds before triggering communication faults
    if (!allCmusOk && millis() > 10000) {
        if (!s_commFault) {
            Serial.println("[PROTECTION] COMMUNICATION FAULT: One or more expected CMUs are offline");
            s_commFault = true;
        }
        allOk = false;
    } else if (allCmusOk) {
        s_commFault = false;
    }

    // Return false if any fault is active (even if latched)
    if (s_overVoltFault || s_underVoltFault || s_overTempFault || s_underTempFault || s_commFault) {
        return false;
    }
    
    return allOk;
}

const char* protectionGetStatus() {
    if (s_overVoltFault) return "OVERVOLTAGE";
    if (s_underVoltFault) return "UNDERVOLTAGE";
    if (s_overTempFault) return "OVERTEMP";
    if (s_underTempFault) return "UNDERTEMP";
    if (s_commFault) return "COMMUNICATION FAULT";
    if (s_cellImbalanceFault) return "IMBALANCE WARNING";
    return "OK";
}

bool protectionCanCharge() {
    // Don't allow charging if:
    // - Overvoltage fault
    // - Over temperature
    // - Under temperature (too cold to charge)
    
    if (s_overVoltFault) return false;
    if (s_overTempFault) return false;
    
    float lowTemp = g_bmsState.lowestTemp;
    float highCellV = g_bmsState.highestCellMv / 1000.0f;
    
    // Check if temperature is below charge temperature limit
    if (lowTemp < g_bmsSettings.chargeTemp) {
        return false;  // Too cold to charge
    }
    
    // Check if voltage is above charge voltage limit
    if (highCellV > g_bmsSettings.chargeVoltage) {
        return false;  // Already at max voltage
    }
    
    return true;
}

bool protectionCanDischarge() {
    // Don't allow discharging if:
    // - Undervoltage fault
    // - Over temperature
    
    if (s_underVoltFault) return false;
    if (s_overTempFault) return false;
    
    float lowCellV = g_bmsState.lowestCellMv / 1000.0f;
    
    // Check if voltage is below discharge voltage limit
    if (lowCellV < g_bmsSettings.dischargeVoltage) {
        return false;  // Too low to discharge
    }
    
    return true;
}

void protectionClearFaults() {
    s_overVoltFault = false;
    s_underVoltFault = false;
    s_overTempFault = false;
    s_underTempFault = false;
    s_commFault = false;
    s_cellImbalanceFault = false;
    s_underVoltTime = 0;
    s_overVoltTime = 0;
    
    Serial.println("[PROTECTION] Faults cleared");
}
