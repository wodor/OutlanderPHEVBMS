/**
 * @file soc_calc.cpp
 * @brief Voltage-derived state-of-charge calculation.
 */

#include "soc_calc.h"
#include "bms_data.h"

void socInit() {
    g_bmsState.soc = socCalculateFromVoltage();
    Serial.printf("[SOC] Using voltage curve: %d%%\n", g_bmsState.soc);
}

void socUpdate() {
    g_bmsState.soc = socCalculateFromVoltage();
}

int socCalculateFromVoltage() {
    if (!g_bmsState.hasAnyData()) return 0;

    const long lowCellMv = g_bmsState.lowestCellMv;
    if (lowCellMv <= 0 || lowCellMv > 5000) return g_bmsState.soc;

    const int lowVolt = g_bmsSettings.socVoltageCurve[0];
    const int lowSoc = g_bmsSettings.socVoltageCurve[1];
    const int highVolt = g_bmsSettings.socVoltageCurve[2];
    const int highSoc = g_bmsSettings.socVoltageCurve[3];
    if (highVolt <= lowVolt) return g_bmsState.soc;

    return constrain(map(static_cast<int>(lowCellMv), lowVolt, highVolt, lowSoc, highSoc), 0, 100);
}
