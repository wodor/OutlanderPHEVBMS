/**
 * @file bms_data.h
 * @brief Data structures and state management for BMS readings
 *
 * EMBEDDED CONCEPT: Separation of Data and Logic
 * -----------------------------------------------
 * Similar to Models in MVC, we separate data structures from I/O logic.
 * This makes testing easier and keeps the code organized.
 */
#pragma once

#include <Arduino.h>
#include "config.h"
#include "soc_curve_validation.h"

/**
 * EMBEDDED CONCEPT: Structs for Data Organization
 * -----------------------------------------------
 * In PHP you might use arrays or objects. In C++, structs group related data.
 * Unlike PHP arrays, structs have fixed memory layout - the compiler knows
 * exactly how much RAM each instance needs.
 *
 * This matters because embedded systems have limited RAM (ESP32-S3 has 320KB).
 */

/**
 * Data for a single CMU (Cell Monitoring Unit)
 * Each Outlander battery module has one CMU that monitors 8 cells.
 */
struct CmuData {
    long voltages[CELLS_PER_MODULE];    // Cell voltages in millivolts (mV)
    long temperatures[TEMPS_PER_MODULE]; // Temperatures (raw value, multiply by 0.001 for °C)
    int  balanceStatus;                  // Bitmask: which cells are balancing (1=balancing)
    bool present;                        // Have we received data from this CMU?
    unsigned long lastSeenTime;          // millis() when last message was received

    // Constructor - initializes all values to safe defaults
    CmuData() : balanceStatus(0), present(false), lastSeenTime(0) {
        // Zero-initialize arrays
        // memset is a C function that fills memory with a value (here: 0)
        memset(voltages, 0, sizeof(voltages));
        memset(temperatures, 0, sizeof(temperatures));
    }

    /**
     * Return the complete eight-cell module voltage in millivolts.
     * A partial total is misleading, so return 0 until all eight cells are valid.
     */
    long getModuleVoltageMv() const {
        long totalMv = 0;
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            const long voltageMv = voltages[c];
            if (voltageMv < 1500 || voltageMv > 4500) return 0;
            totalMv += voltageMv;
        }
        return totalMv;
    }
};

/**
 * BMS Configuration Settings
 * These values control thresholds, limits, and behavior.
 * In V2, these were stored in EEPROM. On ESP32-S3, we'll use NVS (flash storage).
 */
struct BmsSettings {
    // The only pack-level trip configured here is the high-temperature hard
    // stop. Cell-voltage emergency stops are fixed safety backstops in
    // protection.cpp; charge/discharge limits belong to PowerWall-Gateway.
    float overTemp;             // Overheat fault threshold (default: 65°C)
    // Legacy endpoints remain available to existing APIs and design-voltage
    // consumers. The authoritative telemetry mapping is the monotonic point
    // table below.
    int socVoltageCurve[4];
    SocCurvePoint socCurvePoints[SOC_CURVE_MAX_POINTS];
    uint8_t socCurvePointCount;
    bool useVoltageSoc;         // Retained configuration compatibility; SOC is voltage-derived
    
    // Expected CMUs configuration (bitmask for IDs 1-10)
    uint16_t expectedCmusA;     // Expected CMUs on Bus A
    uint16_t expectedCmusB;     // Expected CMUs on Bus B

    // Constructor with defaults
    BmsSettings() :
        overTemp(65.0f),
        socVoltageCurve{3200, 0, 4050, 100},
        socCurvePoints{{3200, 0}, {3490, 10}, {3580, 11}, {3625, 13},
                       {3710, 18}, {3795, 37}, {3840, 46}, {3880, 56},
                       {3925, 68}, {3965, 87}, {4020, 100}, {4050, 100}},
        socCurvePointCount(12),
        useVoltageSoc(true),
        expectedCmusA(0x3FF),   // Default: expect all 10 CMUs on Bus A
        expectedCmusB(0x3FF)    // Default: expect all 10 CMUs on Bus B
    {}
};

bool settingsSetSocCurvePoints(const SocCurvePoint* points, uint8_t count);
int settingsSocCurveLowVoltageMv();
int settingsSocCurveHighVoltageMv();

// A detected CMU may be left unselected while a second CAN segment is being
// surveyed. Its data remains available for diagnostics, but must never affect
// pack-derived telemetry or protection.
extern BmsSettings g_bmsSettings;

inline bool isModuleSelectedForPack(int moduleIndex) {
    if (moduleIndex < 0 || moduleIndex >= BMS_MODULE_COUNT) return false;
    const uint16_t expectedMask = moduleIndex < 10
        ? g_bmsSettings.expectedCmusA
        : g_bmsSettings.expectedCmusB;
    return (expectedMask & (1U << (moduleIndex % 10))) != 0;
}

/**
 * Complete BMS state - holds all data from the battery pack
 *
 * EMBEDDED CONCEPT: Global State
 * ------------------------------
 * In web apps, you avoid globals. In embedded, a single global state
 * object is common and practical - there's only one battery pack,
 * and multiple parts of code need access to its state.
 */
struct BmsState {
    CmuData modules[BMS_MODULE_COUNT];  // Data for all 8 CMUs
    
    // Basic measurements
    long    lowestCellMv;               // Lowest cell voltage across entire pack
    long    highestCellMv;              // Highest cell voltage across entire pack
    long    medianCellMv;               // Median valid cell voltage across the pack
    long    balanceTargetMv;            // Eighth-lowest valid cell voltage; lower seven are left untouched
    long    cellVoltageDeltaMv;         // Highest minus lowest valid cell voltage
    float   packVoltage;                // Total pack voltage in volts
    float   avgCellVoltage;             // Average cell voltage in volts
    
    // Temperature tracking
    float   lowestTemp;                 // Lowest temperature across pack (°C)
    float   highestTemp;                // Highest temperature across pack (°C)
    float   avgTemp;                    // Average temperature (°C)
    
    // SOC (State of Charge) tracking
    int     soc;                        // State of charge 0-100%
    // Control flags
    bool    balancingEnabled;           // Are we sending balance commands?
    bool    debugMode;                  // Print raw CAN frames?
    
    // Timing
    unsigned long lastCanMessageTime;   // millis() when last CAN message received

    BmsState() : 
        lowestCellMv(DEFAULT_LOW_CELL_MV), 
        highestCellMv(0),
        medianCellMv(0),
        balanceTargetMv(0),
        cellVoltageDeltaMv(0),
        packVoltage(0.0f),
        avgCellVoltage(0.0f),
        lowestTemp(999.0f),
        highestTemp(-999.0f),
        avgTemp(0.0f),
        soc(100),
        balancingEnabled(false), 
        debugMode(false), 
        lastCanMessageTime(0)
    {}

    /**
     * Update pack statistics (voltages, temps, etc.)
     * Called periodically after processing CAN data
     */
    void updatePackStatistics() {
        lowestCellMv = DEFAULT_LOW_CELL_MV;
        highestCellMv = 0;
        medianCellMv = 0;
        balanceTargetMv = 0;
        cellVoltageDeltaMv = 0;
        packVoltage = 0.0f;
        avgCellVoltage = 0.0f;
        lowestTemp = 999.0f;
        highestTemp = -999.0f;
        avgTemp = 0.0f;
        
        int cellCount = 0;
        int tempCount = 0;
        float tempSum = 0.0f;
        long validCellVoltages[BMS_MODULE_COUNT * CELLS_PER_MODULE];

        for (int m = 0; m < BMS_MODULE_COUNT; m++) {
            if (!modules[m].present || !isModuleSelectedForPack(m)) continue;

            // Process cell voltages
            // Valid Li-ion cell voltage range: 1500mV - 4500mV
            // Values like 0xFFFD (65533) or 0 indicate "no data" from CMU
            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                long v = modules[m].voltages[c];
                if (v >= 1500 && v <= 4500) {  // Valid voltage range
                    if (v < lowestCellMv) lowestCellMv = v;
                    if (v > highestCellMv) highestCellMv = v;
                    packVoltage += v / 1000.0f;  // Convert mV to V
                    validCellVoltages[cellCount] = v;
                    cellCount++;
                }
            }

            // Process temperatures
            for (int t = 0; t < TEMPS_PER_MODULE; t++) {
                float tempC = modules[m].temperatures[t] * 0.001f;
                // Ignore invalid temps (< -70°C or > 100°C)
                if (tempC > -70.0f && tempC < 100.0f) {
                    if (tempC < lowestTemp) lowestTemp = tempC;
                    if (tempC > highestTemp) highestTemp = tempC;
                    tempSum += tempC;
                    tempCount++;
                }
            }
        }

        // Calculate averages
        if (cellCount > 0) {
            avgCellVoltage = (packVoltage / cellCount);
            cellVoltageDeltaMv = highestCellMv - lowestCellMv;

            // The pack is small (maximum 160 cells), so an in-place insertion
            // sort avoids dynamic allocation while producing a deterministic median.
            for (int i = 1; i < cellCount; i++) {
                const long value = validCellVoltages[i];
                int j = i - 1;
                while (j >= 0 && validCellVoltages[j] > value) {
                    validCellVoltages[j + 1] = validCellVoltages[j];
                    j--;
                }
                validCellVoltages[j + 1] = value;
            }

            if ((cellCount & 1) == 0) {
                const long lowerMiddle = validCellVoltages[(cellCount / 2) - 1];
                const long upperMiddle = validCellVoltages[cellCount / 2];
                medianCellMv = (lowerMiddle + upperMiddle) / 2;
            } else {
                medianCellMv = validCellVoltages[cellCount / 2];
            }

            // Keep the lowest seven valid cells out of balancing. The eighth
            // lowest cell becomes the target, so only cells above it are asked
            // to discharge. With fewer than eight cells, leave balancing off.
            if (cellCount >= 8) {
                balanceTargetMv = validCellVoltages[7];
            }
        }
        if (tempCount > 0) {
            avgTemp = tempSum / tempCount;
        }
    }

    /**
     * Legacy method - kept for compatibility
     */
    void updateLowestCell() {
        updatePackStatistics();
    }

    /**
     * Check if any CMU has reported in
     */
    bool hasAnyData() const {
        for (int m = 0; m < BMS_MODULE_COUNT; m++) {
            if (modules[m].present && isModuleSelectedForPack(m)) return true;
        }
        return false;
    }

    /**
     * Get pack voltage (sum of all series cells divided by parallel strings)
     * SAFETY: Prevents division by zero
     */
    float getPackVoltage(int parallelStrings = 1) const {
        // SAFETY: Prevent division by zero
        if (parallelStrings <= 0) {
            Serial.println("[BMS] ERROR: Invalid parallelStrings in getPackVoltage");
            return packVoltage; // Return undivided voltage
        }
        return packVoltage / parallelStrings;
    }
};

// =============================================================================
// GLOBAL STATE DECLARATION
// =============================================================================
/**
 * EMBEDDED CONCEPT: extern keyword
 * --------------------------------
 * 'extern' says "this variable exists, but is defined elsewhere".
 * Similar to importing a variable in PHP/JS modules.
 *
 * We declare it here (extern) so any file including this header can use it.
 * The actual variable is created in bms_data.cpp.
 */
extern BmsState g_bmsState;

/**
 * Load settings from NVS
 */
void settingsLoad();

/**
 * Save settings to NVS
 */
void settingsSave();
