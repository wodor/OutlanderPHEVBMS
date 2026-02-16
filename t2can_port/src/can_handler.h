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

struct can_frame;

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

/**
 * Send a CAN frame on the specified bus
 *
 * @param frame CAN frame to send
 * @param bus 0 = Bus A (MCP2515), 1 = Bus B (TWAI)
 * @return true if transmit succeeded, false otherwise
 */
bool canSendFrame(const struct can_frame& frame, uint8_t bus);

/**
 * Check if internal TWAI (Bus B) is enabled
 */
bool canIsBusBEnabled();

// =============================================================================
// DIAGNOSTIC FUNCTIONS
// =============================================================================

/**
 * CAN bus statistics for debugging
 */
struct CanStats {
    uint32_t messagesReceived;    // Total valid CAN frames received
    uint32_t messagesDecoded;     // Messages matching CMU format
    uint32_t readAttempts;        // Total readMessage() calls that returned OK
    uint32_t txAttempts;          // Total sendMessage() calls
    uint32_t txSuccess;           // Successful transmissions
    uint8_t  lastErrorFlags;      // Last MCP2515 EFLG register value
    uint8_t  lastInterrupts;      // Last CANINTF register value
    uint8_t  lastStatus;          // Last STATUS register value
    uint32_t lastMessageTime;     // millis() of last received message
};

/**
 * Get current CAN statistics
 */
CanStats canGetStats();

/**
 * Print CAN diagnostic information to serial
 * Shows MCP2515 status, error flags, and message counts
 */
void canPrintDiagnostics();

/**
 * Verify MCP2515 SPI communication is working
 * @return true if MCP2515 responds correctly
 */
bool canVerifySpiComm();
