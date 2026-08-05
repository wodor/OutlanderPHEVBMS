/**
 * @file protection.h
 * @brief Voltage and temperature protection system
 * 
 * Monitors cell voltages and temperatures against configured limits.
 * Enforces over/under voltage and over/under temperature protection.
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize protection system
 */
void protectionInit();

/**
 * Check all protection limits
 * Should be called periodically after updating pack statistics
 * 
 * Checks:
 * - Cell overvoltage
 * - Cell undervoltage
 * - Pack overtemperature
 * - Pack undertemperature
 * - Cell voltage delta (imbalance)
 * 
 * @return true if all checks pass, false if any fault detected
 */
bool protectionCheck();

/**
 * Get human-readable protection status string
 */
const char* protectionGetStatus();

/**
 * Check if charging should be allowed
 * Considers voltage and temperature limits
 */
bool protectionCanCharge();

/**
 * Check if discharging should be allowed
 * Considers voltage and temperature limits
 */
bool protectionCanDischarge();

/**
 * Enable or disable undervoltage protection at runtime.
 * Disabling also clears any currently latched undervoltage fault.
 */
void protectionSetUndervoltageEnabled(bool enabled);

/**
 * Check whether undervoltage protection is currently enabled.
 */
bool protectionIsUndervoltageEnabled();

/**
 * Clear any latched faults
 */
void protectionClearFaults();
