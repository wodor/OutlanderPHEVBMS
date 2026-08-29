#include "current_taper.h"

#include <cmath>
#include <cstdio>

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
    bool selectedModuleFound = false;
    for (int module = 0; module < BMS_MODULE_COUNT; ++module) {
        if (!isModuleSelectedForPack(module)) continue;
        selectedModuleFound = true;
        const CmuData& cmu = g_bmsState.modules[module];
        if (!cmu.present || now - cmu.lastSeenTime >= CAN_DATA_TIMEOUT_MS) return {0, 0};
        for (int cell = 0; cell < CELLS_PER_MODULE; ++cell) {
            if (cmu.voltages[cell] < 1500 || cmu.voltages[cell] > 4500) return {0, 0};
        }
    }
    if (!selectedModuleFound) return {0, 0};
    return currentTaperCalculate(g_bmsState.highestCellMv, g_bmsState.lowestCellMv,
                                 g_bmsSettings);
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
