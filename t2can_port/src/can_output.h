/**
 * @file can_output.h
 * @brief CAN output messages for Battery-Emulator integration
 *
 * This module sends BMS data to the Battery-Emulator translation layer
 * which converts it to Pylontech protocol for FOX ESS inverter compatibility.
 *
 * Message Protocol:
 * -----------------
 * 0x100: BMS Status (voltage, current, SOC, temps)
 * 0x101: Cell voltage details (min/max/avg)
 * 0x102: Limits and protection (charge/discharge current, warnings, faults)
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize CAN output module
 * Sets up message buffers and timing
 */
void canOutputInit();

/**
 * Send all BMS status messages to Battery-Emulator
 *
 * This sends three messages:
 * - 0x100: Basic status (voltage, current, SOC, temps)
 * - 0x101: Cell details (min/max cell voltages)
 * - 0x102: Limits (charge/discharge current limits)
 *
 * Call this periodically (e.g., every 1000ms) from main loop
 */
void canOutputSendBmsData();

/**
 * Configuration: Set which CAN bus to use for output
 *
 * @param useBusA If true, send on Bus A (MCP2515), otherwise Bus B (TWAI)
 *
 * Default is Bus B (TWAI) since Bus A is typically connected to CMUs
 */
void canOutputSetBus(bool useBusA);

/**
 * Get output statistics
 */
struct CanOutputStats {
    uint32_t messagesSent;      // Total messages transmitted
    uint32_t lastSendTime;      // millis() of last send
    uint8_t  lastBusUsed;       // 0=Bus A, 1=Bus B
    bool     enabled;           // Output enabled?
};

CanOutputStats canOutputGetStats();

/**
 * Enable or disable CAN output
 * @param enable True to enable output, false to disable
 */
void canOutputSetEnabled(bool enable);
