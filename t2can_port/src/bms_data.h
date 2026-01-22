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

    // Constructor - initializes all values to safe defaults
    CmuData() : balanceStatus(0), present(false) {
        // Zero-initialize arrays
        // memset is a C function that fills memory with a value (here: 0)
        memset(voltages, 0, sizeof(voltages));
        memset(temperatures, 0, sizeof(temperatures));
    }
};

/**
 * BMS Configuration Settings
 * These values control thresholds, limits, and behavior.
 * In V2, these were stored in EEPROM. On ESP32-S3, we'll use NVS (flash storage).
 */
struct BmsSettings {
    // Voltage limits (per cell, in volts)
    float overVoltage;          // Overvoltage fault threshold (default: 4.2V)
    float underVoltage;         // Undervoltage discharge cutoff (default: 3.0V)
    float chargeVoltage;        // Maximum charge voltage (default: 4.1V)
    float dischargeVoltage;     // Minimum discharge voltage (default: 3.2V)
    float storageVoltage;       // Storage mode target (default: 3.8V)
    float chargeHysteresis;     // Voltage drop to resume charging (default: 0.2V)
    float dischargeHysteresis;  // Voltage rise to resume discharge (default: 0.2V)
    float balanceVoltage;       // Start balancing above this (default: 3.9V)
    float balanceHysteresis;    // Balance hysteresis (default: 0.04V)
    float cellGap;              // Max allowed cell voltage difference (default: 0.2V)
    
    // Temperature limits (in °C)
    float overTemp;             // Overheat fault threshold (default: 65°C)
    float underTemp;            // Cold limit (default: -10°C)
    float chargeTemp;           // Charge derate starts here (default: 0°C)
    float dischargeTemp;        // Discharge derate starts here (default: 40°C)
    float tempWarningOffset;    // Temp offset for warnings (default: 5°C)
    
    // Current limits (in 0.1A units, so 300 = 30.0A)
    int16_t maxChargeCurrent;   // Maximum charge current (default: 300 = 30A)
    int16_t endChargeCurrent;   // End-of-charge current (default: 50 = 5A)
    int16_t maxDischargeCurrent;// Maximum discharge current (default: 300 = 30A)
    int16_t coldChargeCurrent;  // Max charge current when cold (default: 10 = 1A)
    
    // Battery pack configuration
    int seriesCells;            // Number of cells in series (default: 12 for Outlander)
    int parallelStrings;        // Number of parallel strings (default: 1)
    int capacityAh;             // Battery capacity in Ah (default: 100Ah)
    
    // SOC voltage curve (for voltage-based SOC)
    // Maps voltage to SOC: [lowVolt_mV, lowSOC_%, highVolt_mV, highSOC_%]
    int socVoltageCurve[4];     // Default: [3100, 10, 4100, 90]
    bool useVoltageSoc;         // If true, use voltage-based SOC instead of coulomb-counting
    
    // Charger configuration
    int chargerType;            // 0=none, 1=Brusa, 2=ChevyVolt, 3=Eltek, 4=Elcon, etc.
    int chargerSpeedMs;         // Message interval in ms (default: 100ms)
    bool chargerDirect;         // True if charger always connected to HV (default: true)
    
    // Current sensor configuration
    int currentSensorType;      // 0=none, 1=analog dual, 2=CAN, 3=analog single
    int currentSensorCan;       // CAN sensor type: 1=LemCAB300, 2=LemCAB500, 3=IsaScale, 4=VictronLynx
    float conversionHigh;       // mV/A for high range (default: 580)
    float conversionLow;        // mV/A for low range (default: 6430)
    uint16_t offset1;           // mV offset for sensor 1 (default: 1750)
    uint16_t offset2;           // mV offset for sensor 2 (default: 1750)
    int32_t rangeChangeCurrent; // mA threshold for range switching (default: 20000)
    uint16_t currentDeadband;   // mV deadband to reject noise (default: 5)
    
    // Precharge & contactors
    int prechargeTimeMs;        // Precharge duration in ms (default: 5000)
    int prechargeCurrent;       // Max current before closing main (default: 1000mA)
    int contactorHoldDuty;      // PWM duty cycle to hold contactor (default: 50)
    
    // Constructor with defaults
    BmsSettings() :
        overVoltage(4.2f),
        underVoltage(3.0f),
        chargeVoltage(4.1f),
        dischargeVoltage(3.2f),
        storageVoltage(3.8f),
        chargeHysteresis(0.2f),
        dischargeHysteresis(0.2f),
        balanceVoltage(3.9f),
        balanceHysteresis(0.04f),
        cellGap(0.2f),
        overTemp(65.0f),
        underTemp(-10.0f),
        chargeTemp(0.0f),
        dischargeTemp(40.0f),
        tempWarningOffset(5.0f),
        maxChargeCurrent(300),
        endChargeCurrent(50),
        maxDischargeCurrent(300),
        coldChargeCurrent(10),
        seriesCells(12),
        parallelStrings(1),
        capacityAh(100),
        socVoltageCurve{3100, 10, 4100, 90},
        useVoltageSoc(false),
        chargerType(0),
        chargerSpeedMs(100),
        chargerDirect(true),
        currentSensorType(0),
        currentSensorCan(0),
        conversionHigh(580.0f),
        conversionLow(6430.0f),
        offset1(1750),
        offset2(1750),
        rangeChangeCurrent(20000),
        currentDeadband(5),
        prechargeTimeMs(5000),
        prechargeCurrent(1000),
        contactorHoldDuty(50)
    {}
};

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
    float   packVoltage;                // Total pack voltage in volts
    float   avgCellVoltage;             // Average cell voltage in volts
    
    // Temperature tracking
    float   lowestTemp;                 // Lowest temperature across pack (°C)
    float   highestTemp;                // Highest temperature across pack (°C)
    float   avgTemp;                    // Average temperature (°C)
    
    // SOC (State of Charge) tracking
    int     soc;                        // State of charge 0-100%
    float   ampSeconds;                 // Accumulated amp-seconds for coulomb counting
    bool    socInitialized;             // Has SOC been initialized?
    
    // Current sensing
    float   currentAmps;                // Current in amps (+ = charging, - = discharging)
    float   avgCurrentAmps;             // Averaged current
    int     currentSensorRange;         // 0=none, 1=low range, 2=high range
    
    // Charger state
    int16_t targetChargeCurrent;        // Target charge current (0.1A units)
    int16_t targetDischargeCurrent;     // Target discharge current (0.1A units)
    bool    chargerEnabled;             // Is charger enabled?
    
    // Control flags
    bool    balancingEnabled;           // Are we sending balance commands?
    bool    debugMode;                  // Print raw CAN frames?
    
    // Timing
    unsigned long lastCanMessageTime;   // millis() when last CAN message received
    unsigned long lastCurrentUpdate;    // millis() of last current reading
    unsigned long lastSocUpdate;        // millis() of last SOC calculation

    BmsState() : 
        lowestCellMv(DEFAULT_LOW_CELL_MV), 
        highestCellMv(0),
        packVoltage(0.0f),
        avgCellVoltage(0.0f),
        lowestTemp(999.0f),
        highestTemp(-999.0f),
        avgTemp(0.0f),
        soc(100),
        ampSeconds(0.0f),
        socInitialized(false),
        currentAmps(0.0f),
        avgCurrentAmps(0.0f),
        currentSensorRange(0),
        targetChargeCurrent(0),
        targetDischargeCurrent(0),
        chargerEnabled(false),
        balancingEnabled(false), 
        debugMode(false), 
        lastCanMessageTime(0),
        lastCurrentUpdate(0),
        lastSocUpdate(0)
    {}

    /**
     * Update pack statistics (voltages, temps, etc.)
     * Called periodically after processing CAN data
     */
    void updatePackStatistics() {
        lowestCellMv = DEFAULT_LOW_CELL_MV;
        highestCellMv = 0;
        packVoltage = 0.0f;
        lowestTemp = 999.0f;
        highestTemp = -999.0f;
        
        int cellCount = 0;
        int tempCount = 0;
        float tempSum = 0.0f;

        for (int m = 0; m < BMS_MODULE_COUNT; m++) {
            if (!modules[m].present) continue;

            // Process cell voltages
            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                long v = modules[m].voltages[c];
                if (v > 0) {  // Valid voltage
                    if (v < lowestCellMv) lowestCellMv = v;
                    if (v > highestCellMv) highestCellMv = v;
                    packVoltage += v / 1000.0f;  // Convert mV to V
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
            if (modules[m].present) return true;
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
extern BmsSettings g_bmsSettings;
