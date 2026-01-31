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
    g_bmsSettings.expectedCmusA = s_prefs.getUShort("cmusA", g_bmsSettings.expectedCmusA);
    g_bmsSettings.expectedCmusB = s_prefs.getUShort("cmusB", g_bmsSettings.expectedCmusB);

    s_prefs.end();

    Serial.println("[BMS] Settings loaded from NVS");
    Serial.printf("[BMS] Expected CMUs A: 0x%03X, B: 0x%03X\n",
                  g_bmsSettings.expectedCmusA, g_bmsSettings.expectedCmusB);
}

void settingsSave() {
    s_prefs.begin(NVS_NAMESPACE, false); // Read/write

    s_prefs.putUShort("cmusA", g_bmsSettings.expectedCmusA);
    s_prefs.putUShort("cmusB", g_bmsSettings.expectedCmusB);

    s_prefs.end();
    Serial.println("[BMS] Settings saved to NVS");
}
