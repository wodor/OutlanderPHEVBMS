#include "current_taper.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
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
    const unsigned long now = millis();
    if (!g_bmsState.selectedCmusHaveCompleteFreshData(now)) return {0, 0};
    return currentTaperCalculate(g_bmsState.highestCellMv, g_bmsState.lowestCellMv,
                                 g_bmsSettings);
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
