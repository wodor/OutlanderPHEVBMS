/**
 * @file soc_calc.h
 * @brief State of charge derived only from the configured voltage curve.
 */
#pragma once

#include <Arduino.h>

void socInit();
void socUpdate();
int socCalculateFromVoltage();
void socResetFilter();
int socUnfilteredPercent();
long socFilteredCellMv();
