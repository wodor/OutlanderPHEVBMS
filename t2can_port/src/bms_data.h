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
    long    lowestCellMv;               // Lowest cell voltage across entire pack
    bool    balancingEnabled;           // Are we sending balance commands?
    bool    debugMode;                  // Print raw CAN frames?

    BmsState() : lowestCellMv(DEFAULT_LOW_CELL_MV), balancingEnabled(false), debugMode(false) {}

    /**
     * Find the lowest cell voltage in the pack
     * Called after processing new CAN data
     */
    void updateLowestCell() {
        lowestCellMv = DEFAULT_LOW_CELL_MV;

        for (int m = 0; m < BMS_MODULE_COUNT; m++) {
            if (!modules[m].present) continue;

            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                long v = modules[m].voltages[c];
                // Sanity check: voltage should be > 0 (avoid uninitialized data)
                if (v > 0 && v < lowestCellMv) {
                    lowestCellMv = v;
                }
            }
        }
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
