#pragma once

#include "bms_data.h"
#include "charge_hysteresis.h"

struct CurrentLimits {
    int chargeDa;
    int dischargeDa;
};

struct TaperSnapshot {
    CurrentLimits limits{0, 0};
    long minMv = 0, maxMv = 0;
    int fullMv = 0, reducedMv = 0, stopMv = 0;
    bool healthy = false, storageError = false;
    unsigned reason = 4;
    charge::Mode mode = charge::Mode::NORMAL;
    uint32_t event = 0, peakMv = 0, ageMs = 0, offRemainingMs = 0, dwellMs = 0, updatedMs = 0;
};
void currentTaperInit();
void currentTaperTick();  // main-loop owner only
TaperSnapshot currentTaperSnapshot();  // coherent read-only copy
BmsSettings currentTaperSettingsSnapshot();
void currentTaperApplySettings(const BmsSettings& settings);

// MQTT keeps one-decimal precision, but small cell-voltage jitter must not
// create a new retained state every three seconds. Changes accumulate against
// the last published pair, so a genuine fast movement still crosses the
// deadband and is published on the next fast-summary cycle.
constexpr int CURRENT_LIMIT_MQTT_DEADBAND_DA = 5;  // 0.5 A
constexpr unsigned long CURRENT_LIMIT_MQTT_MAX_HOLD_MS = 60000;

// Calculate normal-use limits from selected-cell extrema. Invalid/missing data
// returns zero in both directions; downstream freshness/protection gates remain
// independently mandatory.
CurrentLimits currentTaperCalculate();
CurrentLimits currentTaperCalculate(long highestCellMv, long lowestCellMv,
                                    const BmsSettings& settings);

bool currentLimitsShouldPublish(const CurrentLimits& current,
                                const CurrentLimits& lastPublished,
                                unsigned long elapsedSincePublishMs);

bool currentTaperSettingsValid(const BmsSettings& settings);

// Atomic API format, in volts/amps:
// chargeFullV,chargeReducedV,chargeStopV,chargeFullA,chargeReducedA,
// dischargeFullV,dischargeReducedV,dischargeStopV,dischargeFullA,dischargeReducedA
bool parseCurrentTaperConfig(const char* text, BmsSettings& output);

// Apply one Home Assistant number-entity value without exposing partially
// valid settings. The candidate is validated as a complete taper before the
// caller's settings are changed.
bool parseCurrentTaperParameter(const char* name, const char* text,
                                BmsSettings& output);
