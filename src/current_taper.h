#pragma once

#include "bms_data.h"

struct CurrentLimits {
    int chargeDa;
    int dischargeDa;
};

// Calculate normal-use limits from selected-cell extrema. Invalid/missing data
// returns zero in both directions; downstream freshness/protection gates remain
// independently mandatory.
CurrentLimits currentTaperCalculate();
CurrentLimits currentTaperCalculate(long highestCellMv, long lowestCellMv,
                                    const BmsSettings& settings);

bool currentTaperSettingsValid(const BmsSettings& settings);

// Atomic API format, in volts/amps:
// chargeFullV,chargeReducedV,chargeStopV,chargeFullA,chargeReducedA,
// dischargeFullV,dischargeReducedV,dischargeStopV,dischargeFullA,dischargeReducedA
bool parseCurrentTaperConfig(const char* text, BmsSettings& output);
