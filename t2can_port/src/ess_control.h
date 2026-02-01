/**
 * @file ess_control.h
 * @brief ESS (Energy Storage System) control - precharge, contactor, and charger management
 * 
 * This module provides safe sequencing for ESS mode operation:
 * - Precharge circuit management (time + current threshold)
 * - Main contactor control
 * - Charger enable/disable based on protection status
 * 
 * ESS mode is for stationary storage only (not vehicle drive mode).
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize ESS control system
 * Call once during setup() after protectionInit()
 */
void essInit();

/**
 * Check if precharge sequence is complete and ready for main contactor
 * 
 * Precharge is considered ready when BOTH conditions are met:
 * 1. Elapsed time >= prechargeTimeMs (from BmsSettings)
 * 2. Absolute current <= prechargeCurrent (from BmsSettings, in mA)
 * 
 * @param startMs Timestamp when precharge started (from millis())
 * @param nowMs Current timestamp (from millis())
 * @return true if precharge is ready, false otherwise
 */
bool essPrechargeReady(unsigned long startMs, unsigned long nowMs);

/**
 * Main ESS control tick function
 * Call periodically from loop() (e.g., every 100-500ms)
 * 
 * Handles state machine for:
 * - Precharge sequencing
 * - Main contactor engagement/disengagement
 * - Charger enable/disable
 * - Safety shutdown on protection faults
 */
void essTick();
