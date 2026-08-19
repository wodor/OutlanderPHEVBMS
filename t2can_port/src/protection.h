/**
 * @file protection.h
 * @brief Critical-condition safety output supervision
 * 
 * Drives the active-HIGH battery-safe-to-use output. This controller owns
 * only the hard stop conditions: high temperature, no CMU CAN traffic for ten
 * seconds, or a configured CMU that is absent/stale. It does not set inverter
 * charge/discharge limits or communicate with Battery Emulator.
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize protection system
 */
void protectionInit();

/**
 * Check critical conditions and update the battery-safe-to-use output.
 *
 * @return true when the battery-safe-to-use output is HIGH
 */
bool protectionCheck();

/**
 * Get the current critical-condition status string.
 */
const char* protectionGetStatus();

/**
 * Temporarily suppress cell-voltage trips for supervised recovery work.
 * Temperature and CMU/CAN-loss remain active and non-overridable. This local
 * override expires automatically after ten minutes.
 *
 * @return false unless current CMU data is fresh enough to supervise recovery.
 */
bool protectionEnableSupervisedOverride();

/** Cancel a supervised recovery override before its automatic expiry. */
void protectionCancelSupervisedOverride();

/** Whether the supervised recovery override is currently active. */
bool protectionSupervisedOverrideActive();

/** Milliseconds remaining in the supervised recovery override. */
unsigned long protectionSupervisedOverrideRemainingMs();

/**
 * Clear diagnostic flags and re-evaluate on the next check.
 */
void protectionClearFaults();
