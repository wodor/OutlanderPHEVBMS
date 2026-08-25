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
static constexpr int VOLTAGE_POLICY_REVISION = 1;

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

namespace {
// Apply the agreed normal-use voltage policy to the two historical endpoints
// that this firmware has previously persisted.  4.20 V remains exclusively
// the emergency ceiling enforced in protection.cpp, not an SOC/design target.
bool migrateSocHighVoltageEndpoint() {
    int& highVoltageMv = g_bmsSettings.socVoltageCurve[2];
    if (highVoltageMv == 4000) {
        highVoltageMv = 4050;
        return true;
    }
    if (highVoltageMv == 4050) {
        highVoltageMv = 4100;
        return true;
    }
    return false;
}

void recordVoltagePolicyMigration() {
    s_prefs.begin(NVS_NAMESPACE, false);
    s_prefs.putInt("vPolRev", VOLTAGE_POLICY_REVISION);
    s_prefs.end();
}
}  // namespace

void settingsLoad() {
    s_prefs.begin(NVS_NAMESPACE, true); // Read-only

    // Load expected CMU masks
    // If key doesn't exist, it uses the current value (set by BmsSettings constructor)
    g_bmsSettings.expectedCmusA = (uint16_t)s_prefs.getUInt("cmusA", g_bmsSettings.expectedCmusA);
    g_bmsSettings.expectedCmusB = (uint16_t)s_prefs.getUInt("cmusB", g_bmsSettings.expectedCmusB);
    g_bmsSettings.useBusAForCmu = s_prefs.getBool("cmuAen", g_bmsSettings.useBusAForCmu);

    // Load SOC voltage curve (persisted as individual int keys)
    g_bmsSettings.socVoltageCurve[0] = s_prefs.getInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    g_bmsSettings.socVoltageCurve[1] = s_prefs.getInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    g_bmsSettings.socVoltageCurve[2] = s_prefs.getInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    g_bmsSettings.socVoltageCurve[3] = s_prefs.getInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    g_bmsSettings.useVoltageSoc = s_prefs.getBool("useVSoc", g_bmsSettings.useVoltageSoc);
    const bool needsVoltagePolicyMigration =
        s_prefs.getInt("vPolRev", 0) < VOLTAGE_POLICY_REVISION;

    s_prefs.end();

    if (needsVoltagePolicyMigration && migrateSocHighVoltageEndpoint()) {
        Serial.printf("[BMS] Migrated SOC high-voltage endpoint to %d mV\n",
                      g_bmsSettings.socVoltageCurve[2]);
        settingsSave();
        recordVoltagePolicyMigration();
    }

    Serial.println("[BMS] Settings loaded from NVS");
    Serial.printf("[BMS] Expected CMUs A: 0x%03X, B: 0x%03X\n",
                  g_bmsSettings.expectedCmusA, g_bmsSettings.expectedCmusB);
    Serial.printf("[BMS] Bus A for CMU: %s\n",
                  g_bmsSettings.useBusAForCmu ? "YES" : "NO");
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
    s_prefs.putInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    s_prefs.putInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    s_prefs.putInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    s_prefs.putInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    s_prefs.putBool("useVSoc", g_bmsSettings.useVoltageSoc);

    s_prefs.end();
    Serial.println("[BMS] Settings saved to NVS");
}
