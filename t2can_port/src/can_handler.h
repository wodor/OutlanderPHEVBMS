/**
 * @file can_handler.h
 * @brief CAN bus communication layer for Outlander BMS
 *
 * This module handles all CAN bus I/O:
 * - Initializing the MCP2515 CAN controller
 * - Receiving and decoding BMS messages
 * - Sending balance commands
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize the CAN bus hardware
 *
 * EMBEDDED CONCEPT: Initialization Functions
 * ------------------------------------------
 * Hardware peripherals need configuration before use. Unlike web frameworks
 * that auto-configure, embedded code explicitly initializes each peripheral.
 *
 * This is called once from setup(), never from loop().
 *
 * @return true if initialization successful, false otherwise
 */
bool canInit();

/**
 * Process any pending CAN messages
 *
 * EMBEDDED CONCEPT: Polling vs Interrupts
 * ---------------------------------------
 * Two ways to handle incoming data:
 * 1. Polling: Check "is there data?" repeatedly (what we do here)
 * 2. Interrupts: Hardware triggers a function when data arrives
 *
 * Polling is simpler and fine for our use case. Interrupts are better
 * when you need immediate response or are doing other work.
 *
 * Call this frequently from loop() to process incoming messages.
 */
void canPoll();

/**
 * Send balance command to the BMS
 *
 * The Outlander BMS expects periodic messages on CAN ID 0x3C3 to control
 * cell balancing. We send the target voltage (lowest cell) so all cells
 * balance down to that level.
 *
 * Call this periodically (every ~400ms) from loop().
 */
void canSendBalanceCommand();
