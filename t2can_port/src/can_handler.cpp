/**
 * @file can_handler.cpp
 * @brief CAN bus communication implementation
 */

#include "can_handler.h"
#include "config.h"
#include "bms_data.h"
#include <SPI.h>
#include "mcp2515.h"

// =============================================================================
// PRIVATE MODULE STATE
// =============================================================================
/**
 * EMBEDDED CONCEPT: Static Variables for Module-Private State
 * -----------------------------------------------------------
 * 'static' at file scope means "private to this file" - similar to
 * private class members in PHP. Other files can't access these directly.
 */

// CAN controller instance - talks to MCP2515 chip via SPI
// Parameters: CS pin, SPI speed (10MHz), SPI bus pointer
static MCP2515 s_canController(PIN_MCP2515_CS, 10000000, &SPI);

// Buffers for CAN frames
static struct can_frame s_rxFrame;  // Received frame
static struct can_frame s_txFrame;  // Frame to transmit

// CAN statistics for diagnostics
static CanStats s_canStats = {};

// =============================================================================
// PRIVATE HELPER FUNCTIONS
// =============================================================================

/**
 * Decode a received CAN frame and update BMS state
 *
 * OUTLANDER BMS CAN PROTOCOL:
 * ---------------------------
 * Each CMU sends 3 message types on IDs 0x0[CMU]1, 0x0[CMU]2, 0x0[CMU]3
 * where CMU = 1-8 (encoded as 0x10, 0x20, ... 0x80)
 *
 * Example: CMU 1 sends on 0x011, 0x012, 0x013
 *          CMU 5 sends on 0x051, 0x052, 0x053
 *
 * Message format (8 bytes each):
 * - Type 1: [balance_status, ?, temp1_hi, temp1_lo, temp2_hi, temp2_lo, temp3_hi, temp3_lo]
 * - Type 2: [v1_hi, v1_lo, v2_hi, v2_lo, v3_hi, v3_lo, v4_hi, v4_lo]
 * - Type 3: [v5_hi, v5_lo, v6_hi, v6_lo, v7_hi, v7_lo, v8_hi, v8_lo]
 */
static void decodeCanFrame() {
    uint32_t canId = s_rxFrame.can_id;

    // Track all received messages
    s_canStats.messagesReceived++;
    s_canStats.lastMessageTime = millis();

    // Outlander CMU CAN ID format: 0x6XY where X=CMU number (1-8), Y=message type (1-4)
    // Example: 0x671 -> CMU 7, type 1 (status/temps)
    //          0x672 -> CMU 7, type 2 (voltages 1-4)
    //          0x673 -> CMU 7, type 3 (voltages 5-8)

    // Check if this is a CMU message (0x601-0x684 range)
    if ((canId & 0xF00) != 0x600) {
        // Not a CMU message - log in debug mode
        if (g_bmsState.debugMode) {
            Serial.printf("[CAN] Other ID:0x%03X DLC:%d Data:", canId, s_rxFrame.can_dlc);
            for (int i = 0; i < s_rxFrame.can_dlc; i++) {
                Serial.printf(" %02X", s_rxFrame.data[i]);
            }
            Serial.println();
        }
        return;
    }

    // Extract CMU index and message type from 0x6XY format
    uint8_t msgType  = canId & 0x00F;
    int     cmuIndex = ((canId & 0x0F0) >> 4) - 1;  // CMU 1-8 -> index 0-7

    // Validate CMU index (1-8 valid, so index 0-7)
    if (cmuIndex < 0 || cmuIndex >= BMS_MODULE_COUNT) {
        if (g_bmsState.debugMode) {
            Serial.printf("[CAN] Invalid CMU in ID:0x%03X\n", canId);
        }
        return;
    }

    // Validate message type (1-4 are valid)
    if (msgType < 1 || msgType > 4) {
        if (g_bmsState.debugMode) {
            Serial.printf("[CAN] Invalid msg type in ID:0x%03X\n", canId);
        }
        return;
    }

    // This is a valid CMU message
    s_canStats.messagesDecoded++;

    // Mark this CMU as present (we received data from it)
    g_bmsState.modules[cmuIndex].present = true;
    g_bmsState.lastCanMessageTime = millis();

    // Get reference to this CMU's data (avoids repeated array access)
    CmuData& cmu = g_bmsState.modules[cmuIndex];
    uint8_t* data = s_rxFrame.data;

    /**
     * EMBEDDED CONCEPT: Byte Manipulation
     * ------------------------------------
     * CAN sends raw bytes. Multi-byte values are split across bytes.
     * To reconstruct a 16-bit value from two bytes:
     *   value = (high_byte << 8) | low_byte
     * or equivalently:
     *   value = high_byte * 256 + low_byte
     *
     * This is like unpacking binary data in PHP with unpack('n', $data).
     */

    switch (msgType) {
        case MSG_TYPE_STATUS:  // Balance status + temperatures
            cmu.balanceStatus = data[0];
            cmu.temperatures[0] = (data[2] << 8) | data[3];
            cmu.temperatures[1] = (data[4] << 8) | data[5];
            cmu.temperatures[2] = (data[6] << 8) | data[7];
            break;

        case MSG_TYPE_VOLTS_1:  // Cells 1-4
            cmu.voltages[0] = (data[0] << 8) | data[1];
            cmu.voltages[1] = (data[2] << 8) | data[3];
            cmu.voltages[2] = (data[4] << 8) | data[5];
            cmu.voltages[3] = (data[6] << 8) | data[7];
            break;

        case MSG_TYPE_VOLTS_2:  // Cells 5-8
            cmu.voltages[4] = (data[0] << 8) | data[1];
            cmu.voltages[5] = (data[2] << 8) | data[3];
            cmu.voltages[6] = (data[4] << 8) | data[5];
            cmu.voltages[7] = (data[6] << 8) | data[7];
            break;
    }

    // Debug output if enabled
    if (g_bmsState.debugMode) {
        Serial.printf("[CAN] ID:0x%03X CMU:%d Type:%d Data:",
                      canId, cmuIndex + 1, msgType);
        for (int i = 0; i < s_rxFrame.can_dlc; i++) {
            Serial.printf(" %02X", data[i]);
        }
        Serial.println();
    }
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool canInit() {
    Serial.println("[CAN] Initializing MCP2515...");

    /**
     * EMBEDDED CONCEPT: Hardware Reset Sequence
     * -----------------------------------------
     * Many chips need a specific reset sequence to start cleanly.
     * The MCP2515 requires: HIGH -> LOW -> HIGH on its reset pin.
     * Delays ensure the chip has time to respond.
     */
    pinMode(PIN_MCP2515_RST, OUTPUT);
    digitalWrite(PIN_MCP2515_RST, HIGH);
    delay(100);
    digitalWrite(PIN_MCP2515_RST, LOW);   // Assert reset
    delay(100);
    digitalWrite(PIN_MCP2515_RST, HIGH);  // Release reset
    delay(100);

    /**
     * EMBEDDED CONCEPT: SPI Bus Initialization
     * ----------------------------------------
     * SPI.begin() configures the SPI peripheral with our pin assignments.
     * Multiple devices can share the same SPI bus (SCLK, MOSI, MISO)
     * but each needs its own CS (Chip Select) pin.
     */
    SPI.begin(PIN_MCP2515_SCLK, PIN_MCP2515_MISO, PIN_MCP2515_MOSI, PIN_MCP2515_CS);

    // Reset the MCP2515's internal state
    s_canController.reset();

    // Configure baud rate
    // T-2Can has 16MHz crystal on MCP2515 (library default)
    if (s_canController.setBitrate(CAN_500KBPS) != MCP2515::ERROR_OK) {
        Serial.println("[CAN] ERROR: Failed to set bitrate!");
        return false;
    }

    // Enter normal operation mode (as opposed to loopback/listen-only modes)
    s_canController.setNormalMode();

    // Prepare the TX frame structure (reused for all balance commands)
    s_txFrame.can_id  = CAN_ID_BALANCE_CMD;
    s_txFrame.can_dlc = 8;  // Data Length Code: always 8 bytes for our messages
    memset(s_txFrame.data, 0, 8);
    s_txFrame.data[3] = 4;  // Fixed protocol bytes
    s_txFrame.data[4] = 3;

    // Verify SPI communication is working
    bool spiOk = canVerifySpiComm();
    if (!spiOk) {
        Serial.println("[CAN] WARNING: SPI communication may not be working!");
        Serial.println("[CAN] All reads returned 0xFF - check wiring");
    }

    // Print initial diagnostic info
    Serial.println("[CAN] MCP2515 initialized successfully");
    Serial.printf("[CAN] Config: 500kbps, 16MHz crystal\n");
    Serial.printf("[CAN] Pins: CS=%d, SCLK=%d, MOSI=%d, MISO=%d, RST=%d\n",
                  PIN_MCP2515_CS, PIN_MCP2515_SCLK, PIN_MCP2515_MOSI,
                  PIN_MCP2515_MISO, PIN_MCP2515_RST);

    // Show initial register state
    uint8_t status = s_canController.getStatus();
    uint8_t errorFlags = s_canController.getErrorFlags();
    Serial.printf("[CAN] Initial STATUS=0x%02X EFLG=0x%02X\n", status, errorFlags);

    if (errorFlags != 0) {
        Serial.println("[CAN] WARNING: Error flags already set at init!");
    }

    return true;
}

void canPoll() {
    /**
     * EMBEDDED CONCEPT: Non-blocking I/O
     * ----------------------------------
     * readMessage() returns immediately with ERROR_OK if a message
     * was available, or an error code if not. It never blocks.
     *
     * This is like checking a queue: "anything there? no? ok, move on"
     */

    // Update diagnostic stats
    s_canStats.lastErrorFlags = s_canController.getErrorFlags();
    s_canStats.lastInterrupts = s_canController.getInterrupts();
    s_canStats.lastStatus = s_canController.getStatus();

    while (s_canController.readMessage(&s_rxFrame) == MCP2515::ERROR_OK) {
        s_canStats.readAttempts++;
        decodeCanFrame();
    }
}

void canSendBalanceCommand() {
    if (g_bmsState.balancingEnabled) {
        // Send lowest cell voltage so other cells balance down to match
        // highByte/lowByte split a 16-bit value into two bytes
        s_txFrame.data[0] = highByte(g_bmsState.lowestCellMv);
        s_txFrame.data[1] = lowByte(g_bmsState.lowestCellMv);
        s_txFrame.data[2] = 1;  // Balancing enabled flag
    } else {
        s_txFrame.data[0] = 0;
        s_txFrame.data[1] = 0;
        s_txFrame.data[2] = 0;  // Balancing disabled
    }

    s_canStats.txAttempts++;
    if (s_canController.sendMessage(&s_txFrame) == MCP2515::ERROR_OK) {
        s_canStats.txSuccess++;
    }
}

// =============================================================================
// DIAGNOSTIC FUNCTIONS
// =============================================================================

CanStats canGetStats() {
    return s_canStats;
}

bool canVerifySpiComm() {
    // Try to read the CANSTAT register - should return a valid mode value
    // After reset, CANSTAT should be 0x80 (config mode) or 0x00 (normal mode)
    uint8_t status = s_canController.getStatus();
    uint8_t errorFlags = s_canController.getErrorFlags();

    // If SPI is not working, we typically get 0xFF (all ones) back
    // A working MCP2515 will return reasonable values
    bool spiOk = (status != 0xFF) || (errorFlags != 0xFF);

    return spiOk;
}

void canPrintDiagnostics() {
    Serial.println();
    Serial.println("=== CAN BUS DIAGNOSTICS ===");

    // Verify SPI communication
    bool spiOk = canVerifySpiComm();
    Serial.printf("SPI Communication: %s\n", spiOk ? "OK" : "FAILED (check wiring)");

    // MCP2515 status registers
    uint8_t status = s_canController.getStatus();
    uint8_t errorFlags = s_canController.getErrorFlags();
    uint8_t interrupts = s_canController.getInterrupts();

    Serial.println();
    Serial.println("MCP2515 Registers:");
    Serial.printf("  STATUS:   0x%02X\n", status);
    Serial.printf("  EFLG:     0x%02X", errorFlags);

    // Decode error flags
    if (errorFlags == 0) {
        Serial.println(" (no errors)");
    } else {
        Serial.println();
        if (errorFlags & 0x80) Serial.println("    - RX1 Overflow");
        if (errorFlags & 0x40) Serial.println("    - RX0 Overflow");
        if (errorFlags & 0x20) Serial.println("    - TX Bus-Off");
        if (errorFlags & 0x10) Serial.println("    - TX Error-Passive");
        if (errorFlags & 0x08) Serial.println("    - RX Error-Passive");
        if (errorFlags & 0x04) Serial.println("    - TX Warning");
        if (errorFlags & 0x02) Serial.println("    - RX Warning");
        if (errorFlags & 0x01) Serial.println("    - Error Warning");
    }

    Serial.printf("  CANINTF:  0x%02X", interrupts);
    if (interrupts & 0x01) Serial.print(" RX0");
    if (interrupts & 0x02) Serial.print(" RX1");
    if (interrupts & 0x04) Serial.print(" TX0");
    if (interrupts & 0x08) Serial.print(" TX1");
    if (interrupts & 0x10) Serial.print(" TX2");
    if (interrupts & 0x20) Serial.print(" ERR");
    if (interrupts & 0x40) Serial.print(" WAK");
    if (interrupts & 0x80) Serial.print(" MERR");
    Serial.println();

    // TX/RX error counters
    Serial.printf("  TEC:      %d (TX error count)\n", s_canController.errorCountTX());
    Serial.printf("  REC:      %d (RX error count)\n", s_canController.errorCountRX());

    // Message statistics
    Serial.println();
    Serial.println("Message Statistics:");
    Serial.printf("  Read attempts:      %u\n", s_canStats.readAttempts);
    Serial.printf("  Messages received:  %u\n", s_canStats.messagesReceived);
    Serial.printf("  CMU msgs decoded:   %u\n", s_canStats.messagesDecoded);
    Serial.printf("  TX attempts:        %u\n", s_canStats.txAttempts);
    Serial.printf("  TX success:         %u\n", s_canStats.txSuccess);

    if (s_canStats.lastMessageTime > 0) {
        Serial.printf("  Last msg:           %u ms ago\n", (uint32_t)(millis() - s_canStats.lastMessageTime));
    } else {
        Serial.println("  Last msg:           (none received)");
    }

    Serial.println("===========================");
    Serial.println();
}
