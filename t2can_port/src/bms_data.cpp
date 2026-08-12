/**
 * @file bms_data.cpp
 * @brief Implementation file for BMS data structures
 *
 * EMBEDDED CONCEPT: Header/Source Split
 * --------------------------------------
 * C++ splits code into .h (declarations) and .cpp (definitions).
 * - .h files are like PHP interfaces - they declare what exists
 * - .cpp files provide the actual implementation
 *
 * This split enables:
 * 1. Faster compilation (change .cpp, only recompile that file)
 * 2. Hiding implementation details
 * 3. Avoiding "multiple definition" linker errors
 */

#include "bms_data.h"
#include <Preferences.h>

// Preferences object for NVS storage
static Preferences s_prefs;
static const char* NVS_NAMESPACE = "bms";

// =============================================================================
// GLOBAL STATE DEFINITION
// =============================================================================
/**
 * This is where the actual memory for g_bmsState is allocated.
 * The header only declared it (extern), here we define it.
 *
 * MEMORY NOTE:
 * This struct uses approximately:
 * - 8 modules × (8 longs × 4 bytes + 3 longs × 4 bytes + int + bool) ≈ 360 bytes
 *
 * That's tiny compared to ESP32's 320KB RAM.
 */
BmsState g_bmsState;

/**
 * Global BMS settings with default values
 * Settings can be persisted to NVS (ESP32 non-volatile storage)
 */
BmsSettings g_bmsSettings;

void settingsLoad() {
    s_prefs.begin(NVS_NAMESPACE, true); // Read-only

    // Load expected CMU masks
    // If key doesn't exist, it uses the current value (set by BmsSettings constructor)
    g_bmsSettings.expectedCmusA = (uint16_t)s_prefs.getUInt("cmusA", g_bmsSettings.expectedCmusA);
    g_bmsSettings.expectedCmusB = (uint16_t)s_prefs.getUInt("cmusB", g_bmsSettings.expectedCmusB);
    g_bmsSettings.useBusAForCmu = s_prefs.getBool("cmuAen", g_bmsSettings.useBusAForCmu);
    g_bmsSettings.simpBmsEnabled = s_prefs.getBool("simpben", g_bmsSettings.simpBmsEnabled);

    // Load SOC voltage curve (persisted as individual int keys)
    g_bmsSettings.socVoltageCurve[0] = s_prefs.getInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    g_bmsSettings.socVoltageCurve[1] = s_prefs.getInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    g_bmsSettings.socVoltageCurve[2] = s_prefs.getInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    g_bmsSettings.socVoltageCurve[3] = s_prefs.getInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    g_bmsSettings.useVoltageSoc = s_prefs.getBool("useVSoc", g_bmsSettings.useVoltageSoc);

    s_prefs.end();

    Serial.println("[BMS] Settings loaded from NVS");
    Serial.printf("[BMS] Expected CMUs A: 0x%03X, B: 0x%03X\n",
                  g_bmsSettings.expectedCmusA, g_bmsSettings.expectedCmusB);
    Serial.printf("[BMS] Bus A for CMU: %s, SIMPBMS: %s\n",
                  g_bmsSettings.useBusAForCmu ? "YES" : "NO",
                  g_bmsSettings.simpBmsEnabled ? "ENABLED" : "DISABLED");
    Serial.printf("[BMS] SOC curve: [%d,%d,%d,%d] useVoltageSoc=%s\n",
                  g_bmsSettings.socVoltageCurve[0], g_bmsSettings.socVoltageCurve[1],
                  g_bmsSettings.socVoltageCurve[2], g_bmsSettings.socVoltageCurve[3],
                  g_bmsSettings.useVoltageSoc ? "YES" : "NO");
}

void settingsSave() {
    s_prefs.begin(NVS_NAMESPACE, false); // Read/write

    s_prefs.putUInt("cmusA", g_bmsSettings.expectedCmusA);
    s_prefs.putUInt("cmusB", g_bmsSettings.expectedCmusB);
    s_prefs.putBool("cmuAen", g_bmsSettings.useBusAForCmu);
    s_prefs.putBool("simpben", g_bmsSettings.simpBmsEnabled);
    s_prefs.putInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    s_prefs.putInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    s_prefs.putInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    s_prefs.putInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    s_prefs.putBool("useVSoc", g_bmsSettings.useVoltageSoc);

    s_prefs.end();
    Serial.println("[BMS] Settings saved to NVS");
}
