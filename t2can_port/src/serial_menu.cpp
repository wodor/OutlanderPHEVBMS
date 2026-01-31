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
static void printFullReport();

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

        case 'r':  // Show full report
            printFullReport();
            break;

        case 'R':  // Reset SOC to 100%
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

        case 'A': { // Set Bus A expected CMUs
            Serial.println();
            Serial.printf("Current Bus A expected CMUs: 0x%03X\n", g_bmsSettings.expectedCmusA);
            Serial.println("Enter new hex mask (e.g., 3FF for CMUs 1-10):");
            while (Serial.available()) Serial.read();
            long start = millis();
            while (!Serial.available() && millis() - start < 5000) delay(10);
            if (Serial.available()) {
                String s = Serial.readStringUntil('\n');
                g_bmsSettings.expectedCmusA = strtoul(s.c_str(), NULL, 16) & 0x3FF;
                Serial.printf("Updated Bus A mask to: 0x%03X\n", g_bmsSettings.expectedCmusA);
            }
            break;
        }

        case 'B': { // Set Bus B expected CMUs
            Serial.println();
            Serial.printf("Current Bus B expected CMUs: 0x%03X\n", g_bmsSettings.expectedCmusB);
            Serial.println("Enter new hex mask (e.g., 3FF for CMUs 1-10):");
            while (Serial.available()) Serial.read();
            long start = millis();
            while (!Serial.available() && millis() - start < 5000) delay(10);
            if (Serial.available()) {
                String s = Serial.readStringUntil('\n');
                g_bmsSettings.expectedCmusB = strtoul(s.c_str(), NULL, 16) & 0x3FF;
                Serial.printf("Updated Bus B mask to: 0x%03X\n", g_bmsSettings.expectedCmusB);
            }
            break;
        }

        case 'h':  // Help
        case '?':
            Serial.println();
            Serial.println("=== Commands ===");
            Serial.println("  b - Toggle cell balancing");
            Serial.println("  c - Show CAN bus diagnostics");
            Serial.println("  A - Set Bus A expected CMUs mask (hex)");
            Serial.println("  B - Set Bus B expected CMUs mask (hex)");
            Serial.println("  d - Toggle debug mode (show raw CAN)");
            Serial.println("  r - Show full report");
            Serial.println("  R - Reset SOC to 100%");
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
    Serial.print(".");
}

static void printFullReport() {
    g_bmsState.updatePackStatistics();

    Serial.println();
    Serial.println();
    Serial.println("╔═══════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                        OUTLANDER BMS MONITOR                              ║");
    Serial.println("╠═══════════════════════════════════════════════════════════════════════════╣");

    if (!g_bmsState.hasAnyData()) {
        Serial.println("║  No CMU data received. Check CAN bus connection.                          ║");
        Serial.println("╚═══════════════════════════════════════════════════════════════════════════╝");
        return;
    }

    unsigned long msSinceCan = (g_bmsState.lastCanMessageTime > 0)
        ? (millis() - g_bmsState.lastCanMessageTime)
        : 999999;
    const char* canStatus = (msSinceCan < 2000) ? "OK" : (msSinceCan < 10000) ? "SLOW" : "NO DATA";

    int presentCount = 0;
    int balancingCount = 0;
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (g_bmsState.modules[m].present) {
            presentCount++;
            for (int c = 0; c < CELLS_PER_MODULE; c++) {
                if ((g_bmsState.modules[m].balanceStatus >> c) & 1) {
                    balancingCount++;
                }
            }
        }
    }

    Serial.println("║  SUMMARY                                                                  ║");
    Serial.println("╟───────────────────────────────────────────────────────────────────────────╢");
    Serial.printf("║  CAN: %-7s SOC: %3d%%  Pack: %6.2fV  Current: %+7.2fA (avg %+7.2fA)  ║\n",
                  canStatus, g_bmsState.soc, g_bmsState.packVoltage, 
                  g_bmsState.currentAmps, g_bmsState.avgCurrentAmps);
    Serial.printf("║  Cells: %4ld-%4ldmV (d%4ldmV)  Avg: %.3fV  Temp: %5.1f/%5.1f/%5.1fC    ║\n",
                  g_bmsState.lowestCellMv, g_bmsState.highestCellMv,
                  g_bmsState.highestCellMv - g_bmsState.lowestCellMv,
                  g_bmsState.avgCellVoltage,
                  g_bmsState.lowestTemp, g_bmsState.avgTemp, g_bmsState.highestTemp);
    Serial.printf("║  Modules: %2d/%-2d Balancing: %-3s (%2d cells) Protection: %-16s    ║\n",
                  presentCount, BMS_MODULE_COUNT,
                  g_bmsState.balancingEnabled ? "ON" : "OFF",
                  balancingCount,
                  protectionGetStatus());
    Serial.println("╠═══════════════════════════════════════════════════════════════════════════╣");
    Serial.println("║  MODULES                                                                  ║");

    for (int bus = 0; bus < 2; bus++) {
        bool busHeaderPrinted = false;
        for (int m = 0; m < 10; m++) {
            int idx = bus * 10 + m;
            const CmuData& cmu = g_bmsState.modules[idx];
            if (!cmu.present) continue;

            if (!busHeaderPrinted) {
                Serial.println("╟───────────────────────────────────────────────────────────────────────────╢");
                Serial.printf("║  BUS %c                                                                    ║\n", bus == 0 ? 'A' : 'B');
                busHeaderPrinted = true;
            }

        long modMin = 9999, modMax = 0;
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            if (cmu.voltages[c] > 0) {
                if (cmu.voltages[c] < modMin) modMin = cmu.voltages[c];
                if (cmu.voltages[c] > modMax) modMax = cmu.voltages[c];
            }
        }

            Serial.println("╟───────────────────────────────────────────────────────────────────────────╢");
            Serial.printf("║  CMU %2d  d%4ldmV  Temps: %5.1fC | %5.1fC                                 ║\n",
                          m + 1, modMax - modMin,
                          cmu.temperatures[0] / 1000.0f, cmu.temperatures[1] / 1000.0f);
        Serial.print("║  ");
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            bool isBalancing = (cmu.balanceStatus >> c) & 1;
            bool isLowest = (cmu.voltages[c] == g_bmsState.lowestCellMv);
            char marker = ' ';
            if (isLowest && isBalancing) marker = '!';
            else if (isLowest) marker = '*';
            else if (isBalancing) marker = '~';
            Serial.printf("%4ld%c ", cmu.voltages[c], marker);
        }
            Serial.println("mV                        ║");
        }
    }

    Serial.println("╚═══════════════════════════════════════════════════════════════════════════╝");
    Serial.println();
}

static void printDetailedStats() {
    Serial.println();
    Serial.println("================= DETAILED STATISTICS ====================");
    
    // Print each present module
    for (int bus = 0; bus < 2; bus++) {
        for (int m = 0; m < 10; m++) {
            int idx = bus * 10 + m;
            const CmuData& cmu = g_bmsState.modules[idx];

            if (!cmu.present) continue;

            // Module header with balance status
            Serial.printf("Bus %c | CMU %d | Bal: ", bus == 0 ? 'A' : 'B', m + 1);

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
    }
    
    Serial.println("===========================================================");
}
