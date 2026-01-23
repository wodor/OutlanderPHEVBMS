/**
 * @file soc_calc.h
 * @brief State of Charge (SOC) calculation system
 * 
 * Implements coulomb-counting (amp-hour integration) with voltage-based fallback.
 * Based on the V2 implementation.
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize SOC system
 * Attempts to load saved SOC from NVS, or calculates initial SOC from voltage
 */
void socInit();

/**
 * Update SOC calculation based on current flow
 * Should be called periodically (e.g., every 100ms)
 * 
 * Uses coulomb counting: integrates current over time
 * Formula: SOC = (ampSeconds * 0.27777777777778) / (capacity * parallelStrings * 1000) * 100
 * Where 0.27777777777778 = 1/3600 (converts seconds to hours)
 */
void socUpdate();

/**
 * Reset SOC to a specific value (called when fully charged)
 * @param socPercent New SOC value (0-100)
 */
void socReset(int socPercent);

/**
 * Calculate voltage-based SOC
 * Uses the voltage curve defined in settings
 * @return SOC percentage (0-100)
 */
int socCalculateFromVoltage();

/**
 * Save current SOC to non-volatile storage
 */
void socSave();

/**
 * Load SOC from non-volatile storage
 * @return true if successfully loaded, false otherwise
 */
bool socLoad();
