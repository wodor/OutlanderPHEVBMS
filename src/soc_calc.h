/**
 * @file soc_calc.h
 * @brief State of charge derived only from the configured voltage curve.
 */
#pragma once

#include <Arduino.h>

// Display reference, not a charge target or an energy/capacity estimate.
constexpr long SOC_FULL_CELL_MV = 4200;

void socInit();
void socUpdate();
int socCalculateFromVoltage();
void socResetFilter();
int socUnfilteredPercent();
long socFilteredCellMv();
