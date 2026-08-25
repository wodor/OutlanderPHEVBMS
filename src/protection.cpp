/**
 * @file protection.cpp
 * @brief Critical-condition battery-safe-to-use output supervision.
 */

#include "protection.h"
#include "bms_data.h"
#include "config.h"

namespace {
bool s_overTempFault = false;
bool s_overVoltageFault = false;
bool s_underVoltageFault = false;
bool s_commFault = false;
bool s_supervisedOverrideActive = false;
unsigned long s_supervisedOverrideStartedAt = 0;
constexpr unsigned long SUPERVISED_OVERRIDE_DURATION_MS = 10UL * 60UL * 1000UL;

bool configuredCmusAreFresh(unsigned long now) {
    for (int cmu = 0; cmu < 10; ++cmu) {
        if (g_bmsSettings.expectedCmusA & (1U << cmu)) {
            const CmuData& data = g_bmsState.modules[cmu];
            if (!data.present || now - data.lastSeenTime >= CAN_DATA_TIMEOUT_MS) return false;
        }
        if (g_bmsSettings.expectedCmusB & (1U << cmu)) {
            const CmuData& data = g_bmsState.modules[cmu + 10];
            if (!data.present || now - data.lastSeenTime >= CAN_DATA_TIMEOUT_MS) return false;
        }
    }
    return true;
}

bool supervisedOverrideIsActive(unsigned long now) {
    if (!s_supervisedOverrideActive) return false;
    if (now - s_supervisedOverrideStartedAt >= SUPERVISED_OVERRIDE_DURATION_MS) {
        s_supervisedOverrideActive = false;
        Serial.println("[SAFETY] Supervised recovery override expired");
    }
    return s_supervisedOverrideActive;
}
}  // namespace

void protectionInit() {
    s_supervisedOverrideActive = false;
    s_supervisedOverrideStartedAt = 0;
    protectionClearFaults();
    pinMode(PIN_BATTERY_SAFE_TO_USE, OUTPUT);
    // Permissive by default. The 10-second CAN timeout below is also the
    // startup grace period while selected CMUs start reporting.
    digitalWrite(PIN_BATTERY_SAFE_TO_USE, HIGH);
    Serial.println("[SAFETY] Battery-safe-to-use output initialized HIGH");
}

bool protectionCheck() {
    const unsigned long now = millis();
    const bool globalCanStale = g_bmsState.lastCanMessageTime == 0 ||
        now - g_bmsState.lastCanMessageTime >= CAN_DATA_TIMEOUT_MS;
    const bool selectedCmuMissing = !configuredCmusAreFresh(now);
    s_commFault = now >= CAN_DATA_TIMEOUT_MS && (globalCanStale || selectedCmuMissing);

    s_overTempFault = false;
    s_overVoltageFault = false;
    s_underVoltageFault = false;
    if (g_bmsState.hasAnyData()) {
        g_bmsState.updatePackStatistics();
        s_overTempFault = g_bmsState.highestTemp >= g_bmsSettings.overTemp;
        s_overVoltageFault = g_bmsState.highestCellMv >= 4200;
        s_underVoltageFault = g_bmsState.lowestCellMv <= 2800;
    }

    const bool supervisedOverride = supervisedOverrideIsActive(now);
    // Recovery override is intentionally narrow: it is for supervised
    // over/undervoltage recovery only. Temperature and data-loss trips can
    // never be bypassed by software.
    const bool safe = !(s_commFault || s_overTempFault ||
                        ((!supervisedOverride) &&
                         (s_overVoltageFault || s_underVoltageFault)));
    digitalWrite(PIN_BATTERY_SAFE_TO_USE, safe ? HIGH : LOW);
    return safe;
}

const char* protectionGetStatus() {
    if (s_commFault) return "COMMUNICATION FAULT";
    if (s_overTempFault) return "OVERTEMP";
    if (protectionSupervisedOverrideActive() &&
        (s_overVoltageFault || s_underVoltageFault)) {
        return "SUPERVISED OVERRIDE";
    }
    if (s_overVoltageFault) return "OVERVOLTAGE";
    if (s_underVoltageFault) return "UNDERVOLTAGE";
    return "OK";
}

bool protectionEnableSupervisedOverride() {
    const unsigned long now = millis();
    const bool globalCanFresh = g_bmsState.lastCanMessageTime != 0 &&
        now - g_bmsState.lastCanMessageTime < CAN_DATA_TIMEOUT_MS;
    if (!globalCanFresh || !configuredCmusAreFresh(now)) {
        Serial.println("[SAFETY] Supervised override refused: CMU CAN data is not fresh");
        return false;
    }
    if (g_bmsState.hasAnyData()) {
        g_bmsState.updatePackStatistics();
        if (g_bmsState.highestTemp >= g_bmsSettings.overTemp) {
            Serial.println("[SAFETY] Supervised override refused: high-temperature trip is non-overridable");
            return false;
        }
    }
    s_supervisedOverrideActive = true;
    s_supervisedOverrideStartedAt = now;
    Serial.println("[SAFETY] Supervised recovery override enabled for 10 minutes");
    return true;
}

void protectionCancelSupervisedOverride() {
    s_supervisedOverrideActive = false;
    Serial.println("[SAFETY] Supervised recovery override cancelled");
}

bool protectionSupervisedOverrideActive() {
    return supervisedOverrideIsActive(millis());
}

unsigned long protectionSupervisedOverrideRemainingMs() {
    if (!supervisedOverrideIsActive(millis())) return 0;
    return SUPERVISED_OVERRIDE_DURATION_MS - (millis() - s_supervisedOverrideStartedAt);
}

void protectionClearFaults() {
    s_overTempFault = false;
    s_overVoltageFault = false;
    s_underVoltageFault = false;
    s_commFault = false;
}
