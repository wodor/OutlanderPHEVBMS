/**
 * @file current_sense.h
 * @brief Current sensing and measurement
 * 
 * Supports multiple current sensor types:
 * - Analog dual-range (high precision for low currents, wider range for high currents)
 * - Analog single-range
 * - CAN bus sensors (LEM CAB300/500, IsaScale, Victron Lynx)
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize current sensing hardware
 */
void currentSenseInit();

/**
 * Read current sensor and update BMS state
 * Applies filtering and range selection
 */
void currentSenseUpdate();

/**
 * Get filtered current reading in amps
 * Positive = charging, negative = discharging
 */
float currentSenseGetAmps();
