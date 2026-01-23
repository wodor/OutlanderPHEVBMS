/**
 * @file serial_menu.cpp
 * @brief Serial console interface implementation
 */

#include "serial_menu.h"
#include "config.h"
#include "bms_data.h"
#include "soc_calc.h"
#include "protection.h"
#include "can_handler.h"

// Forward declarations
static void printDetailedStats();

// =============================================================================
// COMMAND HANDLERS
// =============================================================================

/**
 * Handle single-character commands from serial input
 */
static void handleCommand(char cmd) {
    switch (cmd) {
        case 'b':  // Toggle balancing
            g_bmsState.balancingEnabled = !g_bmsState.balancingEnabled;
            Serial.println();
            Serial.print("[CMD] Balancing: ");
            Serial.println(g_bmsState.balancingEnabled ? "ON" : "OFF");
            break;

        case 'd':  // Toggle debug mode
            g_bmsState.debugMode = !g_bmsState.debugMode;
            Serial.println();
            Serial.print("[CMD] Debug mode: ");
            Serial.println(g_bmsState.debugMode ? "ON (showing raw CAN frames)" : "OFF");
            break;

        case 'r':  // Reset SOC to 100%
            Serial.println();
            Serial.println("[CMD] Resetting SOC to 100%");
            socReset(100);
            break;

        case 's':  // Show detailed statistics
            Serial.println();
            printDetailedStats();
            break;

        case 'c':  // CAN diagnostics
            canPrintDiagnostics();
            break;

        case 'h':  // Help
        case '?':
            Serial.println();
            Serial.println("=== Commands ===");
            Serial.println("  b - Toggle cell balancing");
            Serial.println("  c - Show CAN bus diagnostics");
            Serial.println("  d - Toggle debug mode (show raw CAN)");
            Serial.println("  r - Reset SOC to 100%");
            Serial.println("  s - Show detailed statistics");
            Serial.println("  h - Show this help");
            break;

        case '\n':  // Ignore newlines
        case '\r':
            break;

        default:
            Serial.println();
            Serial.println("[CMD] Unknown command. Press 'h' for help.");
            break;
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

void serialProcessInput() {
    /**
     * EMBEDDED CONCEPT: Serial.available()
     * ------------------------------------
     * Serial input is buffered. available() returns how many bytes
     * are waiting to be read. We process one character at a time.
     *
     * This is non-blocking - if no input, we just continue.
     */
    while (Serial.available() > 0) {
        char c = Serial.read();
        handleCommand(c);
    }
}

void serialPrintPackInfo() {
    // Update pack statistics before display
    g_bmsState.updatePackStatistics();

    Serial.println();
    Serial.println("================== OUTLANDER BMS STATUS ==================");

    // Check if we have any data
    if (!g_bmsState.hasAnyData()) {
        Serial.println("  No CMU data received yet. Check CAN bus connection.");
        Serial.println("  - Verify wiring to CAN-A port");
        Serial.println("  - Ensure BMS is powered and transmitting");
        return;
    }

    // Pack summary with V2 features
    Serial.println("-----------------------------------------------------------");
    Serial.printf("Pack Voltage:    %.2fV\n", g_bmsState.packVoltage);
    Serial.printf("Lowest cell:     %ld mV (%.3fV)\n", g_bmsState.lowestCellMv, g_bmsState.lowestCellMv / 1000.0f);
    Serial.printf("Highest cell:    %ld mV (%.3fV)\n", g_bmsState.highestCellMv, g_bmsState.highestCellMv / 1000.0f);
    Serial.printf("Cell delta:      %ld mV (%.3fV)\n", 
                  g_bmsState.highestCellMv - g_bmsState.lowestCellMv,
                  (g_bmsState.highestCellMv - g_bmsState.lowestCellMv) / 1000.0f);
    Serial.printf("Avg cell:        %.3fV\n", g_bmsState.avgCellVoltage);
    Serial.println("-----------------------------------------------------------");
    Serial.printf("Temperature:     %.1fC (low) / %.1fC (avg) / %.1fC (high)\n",
                  g_bmsState.lowestTemp, g_bmsState.avgTemp, g_bmsState.highestTemp);
    Serial.println("-----------------------------------------------------------");
    Serial.printf("SOC:             %d%%\n", g_bmsState.soc);
    Serial.printf("Current:         %.2fA (avg: %.2fA)\n", g_bmsState.currentAmps, g_bmsState.avgCurrentAmps);
    Serial.printf("Amp-hours:       %.2fAh\n", g_bmsState.ampSeconds * 0.27777777777778f / 1000.0f);
    Serial.println("-----------------------------------------------------------");
    Serial.printf("Balancing:       %s\n", g_bmsState.balancingEnabled ? "ENABLED" : "disabled");
    Serial.printf("Protection:      %s\n", protectionGetStatus());
    Serial.println("===========================================================");
}

static void printDetailedStats() {
    Serial.println();
    Serial.println("================= DETAILED STATISTICS ====================");
    
    // Print each present module
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        const CmuData& cmu = g_bmsState.modules[m];

        if (!cmu.present) continue;

        // Module header with balance status
        Serial.printf("CMU %d | Bal: ", m + 1);

        /**
         * EMBEDDED CONCEPT: Bitmask Display
         * ----------------------------------
         * balanceStatus is a bitmask where each bit = one cell
         * Bit 0 = cell 1, bit 7 = cell 8
         *
         * We print which cells are currently balancing.
         */
        if (cmu.balanceStatus == 0) {
            Serial.print("none");
        } else {
            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                if (cmu.balanceStatus & (1 << c)) {
                    Serial.printf("%d ", c + 1);
                }
            }
        }
        Serial.println();

        // Cell voltages
        Serial.print("       Cells: ");
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            Serial.printf("%4ld ", cmu.voltages[c]);
        }
        Serial.println("mV");

        // Temperatures
        // Raw value * 0.001 = degrees Celsius
        Serial.print("       Temps: ");
        for (int t = 0; t < TEMPS_PER_MODULE; t++) {
            float tempC = cmu.temperatures[t] * 0.001f;
            Serial.printf("%.1fC ", tempC);
        }
        Serial.println();
    }
    
    Serial.println("===========================================================");
}
