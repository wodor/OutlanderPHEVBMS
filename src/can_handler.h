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
 * cell balancing. We send the eighth-lowest valid cell voltage, leaving the
 * seven lowest cells untouched while higher cells balance down to that level.
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
    uint32_t balanceTxAttempts;   // Enabled balance frames submitted to CAN
    uint32_t balanceTxQueued;     // Enabled balance frames accepted by CAN driver
    uint32_t lastBalanceCommandTime; // millis() when an enabled balance frame was sent
    long     lastBalanceTargetMv; // Target in the most recent enabled balance frame
    uint8_t  lastBalanceBusMask;  // Bit 0 = Bus A, bit 1 = Bus B
    uint8_t  lastErrorFlags;      // Last MCP2515 EFLG register value
    uint8_t  lastInterrupts;      // Last CANINTF register value
    uint8_t  lastStatus;          // Last STATUS register value
    uint32_t lastMessageTime;     // millis() of last received message
    bool     twaiStatusValid;     // Whether the cached Bus B TWAI status was read successfully
    uint8_t  twaiState;           // Cached twai_state_t value for Bus B
    uint32_t twaiTxErrorCounter;
    uint32_t twaiRxErrorCounter;
    uint32_t twaiTxFailedCount;
    uint32_t twaiRxMissedCount;
    uint32_t twaiArbLostCount;
    uint32_t twaiBusErrorCount;
    uint32_t twaiLastStatusTime;  // millis() of the cached Bus B status
    bool     balanceRecoveryActive;
    uint32_t balanceRecoveryRemainingMs;
    uint32_t balanceRecoveryCount;
};

/**
 * Get current CAN statistics
 */
CanStats canGetStats();

/** Snapshot of the physical CAN-controller diagnostics used by the web UI. */
struct CanHardwareDiagnostics {
    bool spiOk;
    uint8_t status;
    uint8_t errorFlags;
    uint8_t interrupts;
    uint8_t txErrorCount;
    uint8_t rxErrorCount;
};

CanHardwareDiagnostics canGetHardwareDiagnostics();

/**
 * Start one manual balance-command recovery cycle: send the normal disabled
 * balance frame for two seconds, then restore balancing. This does not reboot
 * the controller or claim to reset any CMU.
 *
 * @return true when the cycle was started; false when balancing is off or a
 *         recovery cycle is already in progress.
 */
bool canRequestBalanceRecovery();

/**
 * Service non-blocking CAN state transitions. Call once per main-loop pass.
 */
void canTick();

/**
 * Convert a cached TWAI state value to a stable diagnostic string.
 */
const char* canGetTwaiStateName(uint8_t state);

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
