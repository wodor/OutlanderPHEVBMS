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

    // Extract message type (lower nibble) and CMU index (upper nibble shifted)
    // Example: ID 0x052 -> type=2, cmuIndex=4 (CMU 5, zero-indexed)
    uint8_t msgType  = canId & 0x00F;
    int     cmuIndex = ((canId & 0x0F0) >> 4) - 1;

    // Validate CMU index
    if (cmuIndex < 0 || cmuIndex >= BMS_MODULE_COUNT) {
        return;  // Invalid CMU, ignore
    }

    // Mark this CMU as present (we received data from it)
    g_bmsState.modules[cmuIndex].present = true;

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
    // MCP_8MHZ refers to the crystal on the MCP2515 board (not the ESP32's clock)
    if (s_canController.setBitrate(CAN_500KBPS, MCP_8MHZ) != MCP2515::ERROR_OK) {
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

    Serial.println("[CAN] MCP2515 initialized successfully");
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
    while (s_canController.readMessage(&s_rxFrame) == MCP2515::ERROR_OK) {
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

    s_canController.sendMessage(&s_txFrame);
}
