/**
 * @file soc_calc.cpp
 * @brief Voltage-derived state-of-charge calculation.
 */

#include "soc_calc.h"
#include "bms_data.h"

namespace {
constexpr unsigned long SOC_SAMPLE_INTERVAL_MS = 1000;
constexpr unsigned long SOC_UPWARD_STEP_INTERVAL_MS = 30000;
constexpr uint8_t SOC_MEDIAN_SAMPLES = 15;

long s_voltageSamples[SOC_MEDIAN_SAMPLES] = {};
uint8_t s_sampleCount = 0;
uint8_t s_sampleIndex = 0;
unsigned long s_lastSampleMs = 0;
unsigned long s_lastUpwardChangeMs = 0;
long s_filteredCellMv = 0;
int s_unfilteredPercent = 0;
bool s_initialized = false;

long sampleMedian() {
    long sorted[SOC_MEDIAN_SAMPLES];
    for (uint8_t i = 0; i < s_sampleCount; ++i) sorted[i] = s_voltageSamples[i];
    for (uint8_t i = 1; i < s_sampleCount; ++i) {
        const long value = sorted[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && sorted[j] > value) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = value;
    }
    if ((s_sampleCount & 1U) != 0U) return sorted[s_sampleCount / 2U];
    return (sorted[(s_sampleCount / 2U) - 1U] + sorted[s_sampleCount / 2U]) / 2L;
}

int mapVoltageToSoc(long voltageMv) {
    if (voltageMv >= SOC_FULL_CELL_MV) return 100;
    const int mapped = interpolateSocCurve(voltageMv, g_bmsSettings.socCurvePoints,
                                          g_bmsSettings.socCurvePointCount);
    return mapped < 100 ? mapped : 99;
}
}  // namespace

void socInit() {
    socResetFilter();
    socUpdate();
    Serial.printf("[SOC] Using voltage curve: %d%%\n", g_bmsState.soc);
}

void socUpdate() {
    if (!g_bmsState.hasAnyData()) {
        g_bmsState.soc = 0;
        return;
    }

    const long highestCellMv = g_bmsState.highestCellMv;
    if (highestCellMv <= 0 || highestCellMv > 5000) return;
    s_unfilteredPercent = mapVoltageToSoc(highestCellMv);
    const unsigned long now = millis();

    if (!s_initialized) {
        for (uint8_t i = 0; i < SOC_MEDIAN_SAMPLES; ++i) {
            s_voltageSamples[i] = highestCellMv;
        }
        s_sampleCount = SOC_MEDIAN_SAMPLES;
        s_sampleIndex = 0;
        s_filteredCellMv = highestCellMv;
        g_bmsState.soc = s_unfilteredPercent;
        s_lastSampleMs = now;
        s_lastUpwardChangeMs = now;
        s_initialized = true;
        return;
    }

    // The endpoint follows the live maximum, never rounding or a stale median.
    if (highestCellMv >= SOC_FULL_CELL_MV) {
        g_bmsState.soc = 100;
    } else if (g_bmsState.soc >= 100) {
        g_bmsState.soc = 99;
    }
    if (static_cast<unsigned long>(now - s_lastSampleMs) < SOC_SAMPLE_INTERVAL_MS) return;
    s_lastSampleMs = now;
    s_voltageSamples[s_sampleIndex] = highestCellMv;
    s_sampleIndex = static_cast<uint8_t>((s_sampleIndex + 1U) % SOC_MEDIAN_SAMPLES);
    if (s_sampleCount < SOC_MEDIAN_SAMPLES) ++s_sampleCount;
    s_filteredCellMv = sampleMedian();

    if (highestCellMv >= SOC_FULL_CELL_MV) return;
    const int filtered = mapVoltageToSoc(s_filteredCellMv);
    const int candidate = filtered < 100 ? filtered : 99;
    if (candidate <= g_bmsState.soc) {
        g_bmsState.soc = candidate;
        s_lastUpwardChangeMs = now;
        return;
    }

    const unsigned long elapsed = static_cast<unsigned long>(now - s_lastUpwardChangeMs);
    const unsigned long allowedSteps = elapsed / SOC_UPWARD_STEP_INTERVAL_MS;
    if (allowedSteps == 0) return;
    const int increase = static_cast<int>(allowedSteps);
    g_bmsState.soc = constrain(g_bmsState.soc + increase, 0, candidate);
    s_lastUpwardChangeMs += allowedSteps * SOC_UPWARD_STEP_INTERVAL_MS;
}

int socCalculateFromVoltage() {
    if (!g_bmsState.hasAnyData()) return 0;

    const long highCellMv = g_bmsState.highestCellMv;
    if (highCellMv <= 0 || highCellMv > 5000) return g_bmsState.soc;

    return mapVoltageToSoc(highCellMv);
}

void socResetFilter() {
    memset(s_voltageSamples, 0, sizeof(s_voltageSamples));
    s_sampleCount = 0;
    s_sampleIndex = 0;
    s_lastSampleMs = 0;
    s_lastUpwardChangeMs = 0;
    s_filteredCellMv = 0;
    s_unfilteredPercent = 0;
    s_initialized = false;
}

int socUnfilteredPercent() {
    return s_unfilteredPercent;
}

long socFilteredCellMv() {
    return s_filteredCellMv;
}
