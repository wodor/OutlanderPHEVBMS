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
static constexpr int VOLTAGE_POLICY_REVISION = 2;
static constexpr uint8_t SOC_CURVE_STORAGE_VERSION = 1;
static const char* SOC_CURVE_NVS_KEY = "socPts";

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
struct PersistedSocCurve {
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    SocCurvePoint points[SOC_CURVE_MAX_POINTS];
    uint32_t crc32;
};

uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t curveBlobCrc(const PersistedSocCurve& blob) {
    return crc32(reinterpret_cast<const uint8_t*>(&blob),
                 offsetof(PersistedSocCurve, crc32));
}

bool loadPersistedSocCurve(SocCurvePoint* points, uint8_t& count) {
    if (s_prefs.getBytesLength(SOC_CURVE_NVS_KEY) != sizeof(PersistedSocCurve)) return false;
    PersistedSocCurve blob = {};
    if (s_prefs.getBytes(SOC_CURVE_NVS_KEY, &blob, sizeof(blob)) != sizeof(blob) ||
        blob.version != SOC_CURVE_STORAGE_VERSION || blob.crc32 != curveBlobCrc(blob) ||
        !validateSocCurvePoints(blob.points, blob.count)) {
        return false;
    }
    memcpy(points, blob.points, sizeof(SocCurvePoint) * blob.count);
    count = blob.count;
    return true;
}

void storePersistedSocCurve() {
    PersistedSocCurve blob = {};
    blob.version = SOC_CURVE_STORAGE_VERSION;
    blob.count = g_bmsSettings.socCurvePointCount;
    memcpy(blob.points, g_bmsSettings.socCurvePoints,
           sizeof(SocCurvePoint) * blob.count);
    blob.crc32 = curveBlobCrc(blob);
    s_prefs.putBytes(SOC_CURVE_NVS_KEY, &blob, sizeof(blob));
}

// Apply the agreed normal-use voltage policy to the two historical endpoints
// that this firmware has previously persisted.  4.20 V remains exclusively
// the emergency ceiling enforced in protection.cpp, not an SOC/design target.
bool migrateSocHighVoltageEndpoint() {
    int& highVoltageMv = g_bmsSettings.socVoltageCurve[2];
    if (highVoltageMv == 4000) {
        highVoltageMv = 4050;
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

bool settingsSetSocCurvePoints(const SocCurvePoint* points, uint8_t count) {
    if (!validateSocCurvePoints(points, count)) return false;
    memset(g_bmsSettings.socCurvePoints, 0, sizeof(g_bmsSettings.socCurvePoints));
    memcpy(g_bmsSettings.socCurvePoints, points, sizeof(SocCurvePoint) * count);
    g_bmsSettings.socCurvePointCount = count;
    g_bmsSettings.socVoltageCurve[0] = points[0].voltageMv;
    g_bmsSettings.socVoltageCurve[1] = points[0].socPercent;
    g_bmsSettings.socVoltageCurve[2] = points[count - 1].voltageMv;
    g_bmsSettings.socVoltageCurve[3] = points[count - 1].socPercent;
    return true;
}

int settingsSocCurveLowVoltageMv() {
    return g_bmsSettings.socCurvePoints[0].voltageMv;
}

int settingsSocCurveHighVoltageMv() {
    return g_bmsSettings.socCurvePoints[g_bmsSettings.socCurvePointCount - 1].voltageMv;
}

void settingsLoad() {
    s_prefs.begin(NVS_NAMESPACE, true); // Read-only

    // Load expected CMU masks
    // If key doesn't exist, it uses the current value (set by BmsSettings constructor)
    g_bmsSettings.expectedCmusA = (uint16_t)s_prefs.getUInt("cmusA", g_bmsSettings.expectedCmusA);
    g_bmsSettings.expectedCmusB = (uint16_t)s_prefs.getUInt("cmusB", g_bmsSettings.expectedCmusB);

    // Load SOC voltage curve (persisted as individual int keys)
    g_bmsSettings.socVoltageCurve[0] = s_prefs.getInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    g_bmsSettings.socVoltageCurve[1] = s_prefs.getInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    g_bmsSettings.socVoltageCurve[2] = s_prefs.getInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    g_bmsSettings.socVoltageCurve[3] = s_prefs.getInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    g_bmsSettings.useVoltageSoc = s_prefs.getBool("useVSoc", g_bmsSettings.useVoltageSoc);
    g_bmsSettings.chargeFullVoltageMv = s_prefs.getInt("chgFullV", g_bmsSettings.chargeFullVoltageMv);
    g_bmsSettings.chargeReducedVoltageMv = s_prefs.getInt("chgRedV", g_bmsSettings.chargeReducedVoltageMv);
    g_bmsSettings.chargeStopVoltageMv = s_prefs.getInt("chgStopV", g_bmsSettings.chargeStopVoltageMv);
    g_bmsSettings.chargeFullCurrentDa = s_prefs.getInt("chgFullI", g_bmsSettings.chargeFullCurrentDa);
    g_bmsSettings.chargeReducedCurrentDa = s_prefs.getInt("chgRedI", g_bmsSettings.chargeReducedCurrentDa);
    g_bmsSettings.dischargeFullVoltageMv = s_prefs.getInt("disFullV", g_bmsSettings.dischargeFullVoltageMv);
    g_bmsSettings.dischargeReducedVoltageMv = s_prefs.getInt("disRedV", g_bmsSettings.dischargeReducedVoltageMv);
    g_bmsSettings.dischargeStopVoltageMv = s_prefs.getInt("disStopV", g_bmsSettings.dischargeStopVoltageMv);
    g_bmsSettings.dischargeFullCurrentDa = s_prefs.getInt("disFullI", g_bmsSettings.dischargeFullCurrentDa);
    g_bmsSettings.dischargeReducedCurrentDa = s_prefs.getInt("disRedI", g_bmsSettings.dischargeReducedCurrentDa);
    const bool needsVoltagePolicyMigration =
        s_prefs.getInt("vPolRev", 0) < VOLTAGE_POLICY_REVISION;

    SocCurvePoint persistedPoints[SOC_CURVE_MAX_POINTS] = {};
    uint8_t persistedCount = 0;
    const bool hasValidPersistedCurve = loadPersistedSocCurve(persistedPoints, persistedCount);

    s_prefs.end();

    bool settingsChanged = false;
    if (needsVoltagePolicyMigration) {
        if (migrateSocHighVoltageEndpoint()) {
            Serial.printf("[BMS] Migrated SOC high-voltage endpoint to %d mV\n",
                          g_bmsSettings.socVoltageCurve[2]);
            settingsChanged = true;
        }
        if (hasValidPersistedCurve && persistedCount >= SOC_CURVE_MIN_POINTS &&
            persistedPoints[persistedCount - 1].voltageMv == 4000) {
            persistedPoints[persistedCount - 1].voltageMv = 4050;
            settingsChanged = true;
        }
        recordVoltagePolicyMigration();
    }

    if (hasValidPersistedCurve) {
        settingsSetSocCurvePoints(persistedPoints, persistedCount);
    } else if (g_bmsSettings.socVoltageCurve[0] == 3200 &&
               g_bmsSettings.socVoltageCurve[1] == 0 &&
               g_bmsSettings.socVoltageCurve[2] == 4050 &&
               g_bmsSettings.socVoltageCurve[3] == 100) {
        // The constructor already contains the measured 80-cell fitted curve.
        settingsChanged = true;
    } else {
        const SocCurvePoint legacy[] = {
            {g_bmsSettings.socVoltageCurve[0], g_bmsSettings.socVoltageCurve[1]},
            {g_bmsSettings.socVoltageCurve[2], g_bmsSettings.socVoltageCurve[3]},
        };
        settingsSetSocCurvePoints(legacy, 2);
        settingsChanged = true;
    }

    if (settingsChanged) settingsSave();

    Serial.println("[BMS] Settings loaded from NVS");
    Serial.printf("[BMS] Expected CMUs A: 0x%03X, B: 0x%03X\n",
                  g_bmsSettings.expectedCmusA, g_bmsSettings.expectedCmusB);
    Serial.println("[BMS] Both CAN buses are dedicated to CMU traffic; MQTT is the external transport");
    Serial.printf("[BMS] SOC curve: %u points, endpoints [%d,%d,%d,%d] useVoltageSoc=%s\n",
                  static_cast<unsigned>(g_bmsSettings.socCurvePointCount),
                  g_bmsSettings.socVoltageCurve[0], g_bmsSettings.socVoltageCurve[1],
                  g_bmsSettings.socVoltageCurve[2], g_bmsSettings.socVoltageCurve[3],
                  g_bmsSettings.useVoltageSoc ? "YES" : "NO");
    Serial.printf("[BMS] Charge taper: %d/%d/%d mV, %.1f/%.1f A\n",
                  g_bmsSettings.chargeFullVoltageMv, g_bmsSettings.chargeReducedVoltageMv,
                  g_bmsSettings.chargeStopVoltageMv, g_bmsSettings.chargeFullCurrentDa / 10.0f,
                  g_bmsSettings.chargeReducedCurrentDa / 10.0f);
    Serial.printf("[BMS] Discharge taper: %d/%d/%d mV, %.1f/%.1f A\n",
                  g_bmsSettings.dischargeFullVoltageMv, g_bmsSettings.dischargeReducedVoltageMv,
                  g_bmsSettings.dischargeStopVoltageMv, g_bmsSettings.dischargeFullCurrentDa / 10.0f,
                  g_bmsSettings.dischargeReducedCurrentDa / 10.0f);
}

void settingsSave() {
    const SocCurvePoint& first = g_bmsSettings.socCurvePoints[0];
    const SocCurvePoint& last =
        g_bmsSettings.socCurvePoints[g_bmsSettings.socCurvePointCount - 1];
    if (first.voltageMv != g_bmsSettings.socVoltageCurve[0] ||
        first.socPercent != g_bmsSettings.socVoltageCurve[1] ||
        last.voltageMv != g_bmsSettings.socVoltageCurve[2] ||
        last.socPercent != g_bmsSettings.socVoltageCurve[3]) {
        const SocCurvePoint legacy[] = {
            {g_bmsSettings.socVoltageCurve[0], g_bmsSettings.socVoltageCurve[1]},
            {g_bmsSettings.socVoltageCurve[2], g_bmsSettings.socVoltageCurve[3]},
        };
        settingsSetSocCurvePoints(legacy, 2);
    }

    s_prefs.begin(NVS_NAMESPACE, false); // Read/write

    s_prefs.putUInt("cmusA", g_bmsSettings.expectedCmusA);
    s_prefs.putUInt("cmusB", g_bmsSettings.expectedCmusB);
    s_prefs.putInt("socV0", g_bmsSettings.socVoltageCurve[0]);
    s_prefs.putInt("socV1", g_bmsSettings.socVoltageCurve[1]);
    s_prefs.putInt("socV2", g_bmsSettings.socVoltageCurve[2]);
    s_prefs.putInt("socV3", g_bmsSettings.socVoltageCurve[3]);
    s_prefs.putBool("useVSoc", g_bmsSettings.useVoltageSoc);
    storePersistedSocCurve();
    s_prefs.putInt("chgFullV", g_bmsSettings.chargeFullVoltageMv);
    s_prefs.putInt("chgRedV", g_bmsSettings.chargeReducedVoltageMv);
    s_prefs.putInt("chgStopV", g_bmsSettings.chargeStopVoltageMv);
    s_prefs.putInt("chgFullI", g_bmsSettings.chargeFullCurrentDa);
    s_prefs.putInt("chgRedI", g_bmsSettings.chargeReducedCurrentDa);
    s_prefs.putInt("disFullV", g_bmsSettings.dischargeFullVoltageMv);
    s_prefs.putInt("disRedV", g_bmsSettings.dischargeReducedVoltageMv);
    s_prefs.putInt("disStopV", g_bmsSettings.dischargeStopVoltageMv);
    s_prefs.putInt("disFullI", g_bmsSettings.dischargeFullCurrentDa);
    s_prefs.putInt("disRedI", g_bmsSettings.dischargeReducedCurrentDa);

    s_prefs.end();
    Serial.println("[BMS] Settings saved to NVS");
}
