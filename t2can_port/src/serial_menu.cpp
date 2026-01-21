/**
 * @file serial_menu.cpp
 * @brief Serial console interface implementation
 */

#include "serial_menu.h"
#include "config.h"
#include "bms_data.h"

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

        case 'h':  // Help
        case '?':
            Serial.println();
            Serial.println("=== Commands ===");
            Serial.println("  b - Toggle cell balancing");
            Serial.println("  d - Toggle debug mode (show raw CAN)");
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
    // Update lowest cell calculation before display
    g_bmsState.updateLowestCell();

    Serial.println();
    Serial.println("================== OUTLANDER BMS STATUS ==================");

    // Check if we have any data
    if (!g_bmsState.hasAnyData()) {
        Serial.println("  No CMU data received yet. Check CAN bus connection.");
        Serial.println("  - Verify wiring to CAN-A port");
        Serial.println("  - Ensure BMS is powered and transmitting");
        return;
    }

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

    // Pack summary
    Serial.println("-----------------------------------------------------------");
    Serial.printf("Lowest cell: %ld mV\n", g_bmsState.lowestCellMv);
    Serial.printf("Balancing:   %s\n", g_bmsState.balancingEnabled ? "ENABLED" : "disabled");
    Serial.println("===========================================================");
}
