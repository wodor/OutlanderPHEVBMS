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

SimpBmsStats simpBmsGetStats();
