#include "current_taper.h"
#include "protection.h"
#include <Preferences.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
charge::Hysteresis s_hysteresis;
TaperSnapshot s_snapshot;
#ifndef UNIT_TEST
portMUX_TYPE s_taperMutex = portMUX_INITIALIZER_UNLOCKED;
struct Lock { Lock() { portENTER_CRITICAL(&s_taperMutex); } ~Lock() { portEXIT_CRITICAL(&s_taperMutex); } };
#else
struct Lock {};
#endif
bool saveHysteresis(const charge::Record& record, void*) {
    Preferences prefs;
    if (!prefs.begin("charge_hold", false)) return false;
    charge::Record check;
    const bool ok = prefs.putBytes("state", &record, sizeof(record)) == sizeof(record) &&
        prefs.getBytes("state", &check, sizeof(check)) == sizeof(check) &&
        memcmp(&record, &check, sizeof(record)) == 0 && charge::valid(check);
    prefs.end();
    return ok;
}
uint32_t fingerprint(const BmsSettings& s) {
    const int fields[] = {s.chargeFullVoltageMv, s.chargeReducedVoltageMv, s.chargeStopVoltageMv,
        s.chargeFullCurrentDa, s.chargeReducedCurrentDa, s.dischargeFullVoltageMv,
        s.dischargeReducedVoltageMv, s.dischargeStopVoltageMv, s.dischargeFullCurrentDa,
        s.dischargeReducedCurrentDa, s.expectedCmusA, s.expectedCmusB};
    uint32_t hash = 2166136261U;
    for (int value : fields) for (int shift = 0; shift < 32; shift += 8) {
        hash ^= (uint32_t(value) >> shift) & 255; hash *= 16777619U;
    }
    return hash;
}
int interpolate(int x, int x0, int x1, int y0, int y1) {
    const long numerator = static_cast<long>(x - x0) * (y1 - y0);
    const long denominator = x1 - x0;
    const long rounded = numerator >= 0 ? numerator + denominator / 2
                                        : numerator - denominator / 2;
    return y0 + static_cast<int>(rounded / denominator);
}

int ampsToDeciamps(float amps) {
    return static_cast<int>(std::lround(amps * 10.0f));
}
}  // namespace

bool currentTaperSettingsValid(const BmsSettings& s) {
    const bool currentsValid =
        s.chargeFullCurrentDa >= 1 && s.chargeFullCurrentDa <= 1000 &&
        s.chargeReducedCurrentDa >= 1 && s.chargeReducedCurrentDa <= s.chargeFullCurrentDa &&
        s.dischargeFullCurrentDa >= 1 && s.dischargeFullCurrentDa <= 1000 &&
        s.dischargeReducedCurrentDa >= 1 && s.dischargeReducedCurrentDa <= s.dischargeFullCurrentDa;
    const bool chargeValid =
        s.chargeFullVoltageMv >= 2500 &&
        s.chargeFullVoltageMv < s.chargeReducedVoltageMv &&
        s.chargeReducedVoltageMv < s.chargeStopVoltageMv &&
        s.chargeStopVoltageMv < 4200;
    const bool dischargeValid =
        s.dischargeStopVoltageMv > 2800 &&
        s.dischargeStopVoltageMv < s.dischargeReducedVoltageMv &&
        s.dischargeReducedVoltageMv < s.dischargeFullVoltageMv &&
        s.dischargeFullVoltageMv <= 4500;
    return currentsValid && chargeValid && dischargeValid;
}

CurrentLimits currentTaperCalculate(long highestCellMv, long lowestCellMv,
                                    const BmsSettings& s) {
    if (!currentTaperSettingsValid(s) || highestCellMv < 1500 || highestCellMv > 4500 ||
        lowestCellMv < 1500 || lowestCellMv > 4500) {
        return {0, 0};
    }

    int chargeDa = s.chargeFullCurrentDa;
    if (highestCellMv >= s.chargeStopVoltageMv) chargeDa = 0;
    else if (highestCellMv >= s.chargeReducedVoltageMv) chargeDa = s.chargeReducedCurrentDa;
    else if (highestCellMv > s.chargeFullVoltageMv) {
        chargeDa = interpolate(static_cast<int>(highestCellMv), s.chargeFullVoltageMv,
                               s.chargeReducedVoltageMv, s.chargeFullCurrentDa,
                               s.chargeReducedCurrentDa);
    }

    int dischargeDa = s.dischargeFullCurrentDa;
    if (lowestCellMv <= s.dischargeStopVoltageMv) dischargeDa = 0;
    else if (lowestCellMv <= s.dischargeReducedVoltageMv) dischargeDa = s.dischargeReducedCurrentDa;
    else if (lowestCellMv < s.dischargeFullVoltageMv) {
        dischargeDa = interpolate(static_cast<int>(lowestCellMv), s.dischargeReducedVoltageMv,
                                  s.dischargeFullVoltageMv, s.dischargeReducedCurrentDa,
                                  s.dischargeFullCurrentDa);
    }
    return {chargeDa, dischargeDa};
}

CurrentLimits currentTaperCalculate() {
    return currentTaperSnapshot().limits;
}

BmsSettings currentTaperSettingsSnapshot() { Lock lock; (void)lock; return g_bmsSettings; }
void currentTaperApplySettings(const BmsSettings& settings) {
    Lock lock; (void)lock; g_bmsSettings = settings;
    s_snapshot.limits = {0, 0}; s_snapshot.healthy = false; s_snapshot.reason = 4;
    s_snapshot.event = s_snapshot.peakMv = s_snapshot.ageMs = 0;
}

void currentTaperInit() {
    Preferences prefs;
    charge::Record record;
    const bool opened = prefs.begin("charge_hold", false);
    const bool present = opened && prefs.isKey("state");
    const bool valid = present && prefs.getBytesLength("state") == sizeof(record) &&
        prefs.getBytes("state", &record, sizeof(record)) == sizeof(record) && charge::valid(record);
    if (opened) prefs.end();
    s_hysteresis.restore(valid ? &record : nullptr, !opened || (present && !valid), millis());
    Lock lock; (void)lock; s_snapshot = TaperSnapshot();
}

void currentTaperTick() {
    const uint32_t now = millis();
    const BmsSettings settings = currentTaperSettingsSnapshot();
    const bool complete = g_bmsState.selectedCmusHaveCompleteFreshData(now);
    const bool valid = currentTaperSettingsValid(settings);
    const bool supervised = protectionSupervisedOverrideActive();
    const char* protection = protectionGetStatus();
    const bool protectionOk = strcmp(protection, "OK") == 0 &&
        !supervised && !g_bmsState.debugMode &&
        g_bmsState.highestTemp < settings.overTemp &&
        g_bmsState.highestCellMv < 4200 && g_bmsState.lowestCellMv > 2800;
    const bool healthy = complete && valid && protectionOk;
    TaperSnapshot next;
    next.minMv = g_bmsState.lowestCellMv; next.maxMv = g_bmsState.highestCellMv;
    next.fullMv = settings.chargeFullVoltageMv; next.reducedMv = settings.chargeReducedVoltageMv;
    next.stopMv = settings.chargeStopVoltageMv; next.updatedMs = now;
    s_hysteresis.update(now, next.maxMv, next.stopMv, healthy, fingerprint(settings),
                        saveHysteresis, nullptr, complete && valid);
    // Preserve the existing supervised voltage-recovery path. It can never
    // release hysteresis or qualify full, but otherwise permitted discharge
    // must not be suppressed by a charge-only hold (or recovery override).
    const bool limitsAllowed = complete && valid && g_bmsState.highestTemp < settings.overTemp &&
        (strcmp(protection, "OK") == 0 || (supervised && strcmp(protection, "SUPERVISED OVERRIDE") == 0));
    if (limitsAllowed) {
        next.limits = currentTaperCalculate(next.maxMv, next.minMv, settings);
        next.limits.chargeDa = s_hysteresis.limit(next.limits.chargeDa, settings.chargeReducedCurrentDa);
    }
    next.mode = s_hysteresis.mode(); next.storageError = s_hysteresis.storage_error();
    next.healthy = healthy && !next.storageError;
    next.reason = !valid ? 3 : !complete ? 4 : (!protectionOk || next.storageError) ? 5 :
        next.mode == charge::Mode::TOP_HOLD ? (s_hysteresis.event() ? 6 : 7) :
        next.maxMv > next.fullMv ? 1 : 0;
    next.offRemainingMs = s_hysteresis.off_remaining(); next.dwellMs = s_hysteresis.dwell_ms(now);
    if (next.reason == 6 || next.reason == 7) {
        next.event = s_hysteresis.event(); next.peakMv = s_hysteresis.peak(); next.ageMs = s_hysteresis.age();
    }
    Lock lock; (void)lock;
    if (fingerprint(g_bmsSettings) != fingerprint(settings)) {
        // Async configuration changed while this owner was evaluating/saving.
        next.limits = {0, 0}; next.healthy = false; next.reason = 4;
        next.event = next.peakMv = next.ageMs = 0;
    }
    s_snapshot = next;
}

TaperSnapshot currentTaperSnapshot() {
    Lock lock; (void)lock;
    TaperSnapshot result = s_snapshot;
    // A stalled owner must not advertise an indefinitely valid cached permit.
    if (uint32_t(millis() - result.updatedMs) > 1000) {
        result.limits = {0, 0}; result.healthy = false; result.reason = 4;
        result.event = result.peakMv = result.ageMs = 0;
    }
    return result;
}

bool currentLimitsShouldPublish(const CurrentLimits& current,
                                const CurrentLimits& lastPublished,
                                unsigned long elapsedSincePublishMs) {
    const bool chargeChanged = current.chargeDa != lastPublished.chargeDa;
    const bool dischargeChanged = current.dischargeDa != lastPublished.dischargeDa;
    if (!chargeChanged && !dischargeChanged) return false;

    // Never debounce entry into or recovery from the fail-closed zero state.
    if ((current.chargeDa == 0) != (lastPublished.chargeDa == 0) ||
        (current.dischargeDa == 0) != (lastPublished.dischargeDa == 0)) {
        return true;
    }

    const int chargeDelta = current.chargeDa > lastPublished.chargeDa
        ? current.chargeDa - lastPublished.chargeDa
        : lastPublished.chargeDa - current.chargeDa;
    const int dischargeDelta = current.dischargeDa > lastPublished.dischargeDa
        ? current.dischargeDa - lastPublished.dischargeDa
        : lastPublished.dischargeDa - current.dischargeDa;
    if (chargeDelta >= CURRENT_LIMIT_MQTT_DEADBAND_DA ||
        dischargeDelta >= CURRENT_LIMIT_MQTT_DEADBAND_DA) {
        return true;
    }

    // Eventually expose a persistent small change without allowing jitter to
    // write a new Home Assistant state on every fast-summary cycle.
    return elapsedSincePublishMs >= CURRENT_LIMIT_MQTT_MAX_HOLD_MS;
}

bool parseCurrentTaperConfig(const char* text, BmsSettings& output) {
    if (text == nullptr) return false;
    float values[10];
    char trailing = '\0';
    if (std::sscanf(text, "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f%c",
                    &values[0], &values[1], &values[2], &values[3], &values[4],
                    &values[5], &values[6], &values[7], &values[8], &values[9],
                    &trailing) != 10) return false;
    for (int i = 0; i < 10; ++i) {
        if (!std::isfinite(values[i])) return false;
    }

    BmsSettings candidate = output;
    candidate.chargeFullVoltageMv = static_cast<int>(std::lround(values[0] * 1000.0f));
    candidate.chargeReducedVoltageMv = static_cast<int>(std::lround(values[1] * 1000.0f));
    candidate.chargeStopVoltageMv = static_cast<int>(std::lround(values[2] * 1000.0f));
    candidate.chargeFullCurrentDa = ampsToDeciamps(values[3]);
    candidate.chargeReducedCurrentDa = ampsToDeciamps(values[4]);
    candidate.dischargeFullVoltageMv = static_cast<int>(std::lround(values[5] * 1000.0f));
    candidate.dischargeReducedVoltageMv = static_cast<int>(std::lround(values[6] * 1000.0f));
    candidate.dischargeStopVoltageMv = static_cast<int>(std::lround(values[7] * 1000.0f));
    candidate.dischargeFullCurrentDa = ampsToDeciamps(values[8]);
    candidate.dischargeReducedCurrentDa = ampsToDeciamps(values[9]);
    if (!currentTaperSettingsValid(candidate)) return false;
    output = candidate;
    return true;
}

bool parseCurrentTaperParameter(const char* name, const char* text,
                                BmsSettings& output) {
    if (name == nullptr || text == nullptr) return false;

    float value = 0.0f;
    char trailing = '\0';
    if (std::sscanf(text, "%f%c", &value, &trailing) != 1 || !std::isfinite(value)) {
        return false;
    }

    BmsSettings candidate = output;
    if (std::strcmp(name, "charge_full_voltage") == 0) {
        candidate.chargeFullVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "charge_reduced_voltage") == 0) {
        candidate.chargeReducedVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "charge_stop_voltage") == 0) {
        candidate.chargeStopVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "charge_full_current") == 0) {
        candidate.chargeFullCurrentDa = ampsToDeciamps(value);
    } else if (std::strcmp(name, "charge_reduced_current") == 0) {
        candidate.chargeReducedCurrentDa = ampsToDeciamps(value);
    } else if (std::strcmp(name, "discharge_full_voltage") == 0) {
        candidate.dischargeFullVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "discharge_reduced_voltage") == 0) {
        candidate.dischargeReducedVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "discharge_stop_voltage") == 0) {
        candidate.dischargeStopVoltageMv = static_cast<int>(std::lround(value * 1000.0f));
    } else if (std::strcmp(name, "discharge_full_current") == 0) {
        candidate.dischargeFullCurrentDa = ampsToDeciamps(value);
    } else if (std::strcmp(name, "discharge_reduced_current") == 0) {
        candidate.dischargeReducedCurrentDa = ampsToDeciamps(value);
    } else {
        return false;
    }

    if (!currentTaperSettingsValid(candidate)) return false;
    output = candidate;
    return true;
}
