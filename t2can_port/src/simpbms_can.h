/**
 * @file simpbms_can.h
 * @brief SIMPBMS/Victron-style CAN output
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize SIMPBMS CAN output module
 */
void simpBmsInit();

/**
 * Periodic tick to send SIMPBMS CAN frames
 */
void simpBmsTick();

/**
 * Basic output statistics
 */
struct SimpBmsStats {
    uint32_t framesSent;
    uint32_t lastSendTime;
    uint8_t  lastBusUsed; // 0=Bus A, 1=Bus B
};

/**
 * The design-voltage limits sent in SIMPBMS frame 0x351.
 * Battery Emulator maps these to its maximum and minimum design voltages.
 */
struct SimpBmsDesignVoltageLimits {
    uint16_t moduleCount;
    uint16_t seriesCells;
    float maxVoltageV;
    float minVoltageV;
};

SimpBmsStats simpBmsGetStats();

/**
 * Return the exact maximum/minimum design voltages currently sent in 0x351.
 */
SimpBmsDesignVoltageLimits simpBmsGetDesignVoltageLimits();
