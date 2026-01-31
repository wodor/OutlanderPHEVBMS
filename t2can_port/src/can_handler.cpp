/**
 * @file can_handler.cpp
 * @brief CAN bus communication implementation
 */

#include "can_handler.h"
#include "config.h"
#include "bms_data.h"
#include <SPI.h>
#include "mcp2515.h"
#include "driver/twai.h"

// =============================================================================
// PRIVATE MODULE STATE
// =============================================================================
/**
 * EMBEDDED CONCEPT: Static Variables for Module-Private State
 * -----------------------------------------------------------
 * 'static' at file scope means "private to this file" - similar to
 * private class members in PHP. Other files can't access these directly.
 */

// Bus A: External MCP2515 CAN controller via SPI
static MCP2515 s_canA(PIN_MCP2515_CS, 10000000, &SPI);

// Bus B: Internal ESP32-S3 TWAI controller
static bool s_twaiEnabled = false;

// Buffers for CAN frames
static struct can_frame s_rxFrame;  // Received frame (MCP2515)
static struct can_frame s_txFrame;  // Frame to transmit

// CAN statistics for diagnostics
static CanStats s_canStats = {};

// =============================================================================
// PRIVATE HELPER FUNCTIONS
// =============================================================================

/**
 * Decode a received CAN frame and update BMS state
 *
 * @param canId Message ID
 * @param dlc Data length
 * @param data Pointer to 8 bytes of data
 * @param busIndex 0 for Bus A, 1 for Bus B
 */
static void processFrame(uint32_t canId, uint8_t dlc, uint8_t* data, int busIndex) {
    // Track all received messages
    s_canStats.messagesReceived++;
    s_canStats.lastMessageTime = millis();

    // Check if this is a CMU message (0x601-0x684 range)
    if ((canId & 0xF00) != 0x600) {
        if (g_bmsState.debugMode) {
            Serial.printf("[CAN-%c] Other ID:0x%03X DLC:%d Data:",
                          (busIndex == 0 ? 'A' : 'B'), canId, dlc);
            for (int i = 0; i < dlc; i++) {
                Serial.printf(" %02X", data[i]);
            }
            Serial.println();
        }
        return;
    }

    // Extract CMU ID (1-10) and message type (1-4)
    uint8_t msgType  = canId & 0x00F;
    int     cmuId    = (canId & 0x0F0) >> 4;  // 1-10

    // Map to global module index
    // Bus A (0) -> index 0-9
    // Bus B (1) -> index 10-19
    int cmuIndex = (busIndex * 10) + (cmuId - 1);

    // Validate CMU index
    if (cmuIndex < 0 || cmuIndex >= BMS_MODULE_COUNT) {
        return;
    }

    // Validate message type (1-4 are valid)
    if (msgType < 1 || msgType > 4) {
        return;
    }

    // Check if this CMU is expected on this bus
    bool expected = false;
    if (busIndex == 0) {
        expected = (g_bmsSettings.expectedCmusA & (1 << (cmuId - 1)));
    } else {
        expected = (g_bmsSettings.expectedCmusB & (1 << (cmuId - 1)));
    }

    if (!expected) {
        // Received message from unexpected CMU - still process it but maybe log?
        if (g_bmsState.debugMode) {
            Serial.printf("[CAN-%c] Unexpected CMU ID:0x%03X\n",
                          (busIndex == 0 ? 'A' : 'B'), canId);
        }
        // return; // Uncomment to ignore unexpected CMUs
    }

    // This is a valid CMU message
    s_canStats.messagesDecoded++;

    // Mark this CMU as present
    g_bmsState.modules[cmuIndex].present = true;
    g_bmsState.modules[cmuIndex].lastSeenTime = millis();
    g_bmsState.lastCanMessageTime = millis();

    CmuData& cmu = g_bmsState.modules[cmuIndex];

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
        Serial.printf("[CAN-%c] ID:0x%03X CMU:%d Index:%d Type:%d Data:",
                      (busIndex == 0 ? 'A' : 'B'), canId, cmuId, cmuIndex, msgType);
        for (int i = 0; i < dlc; i++) {
            Serial.printf(" %02X", data[i]);
        }
        Serial.println();
    }
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

bool canInit() {
    // -------------------------------------------------------------------------
    // Initialize Bus A (MCP2515 via SPI)
    // -------------------------------------------------------------------------
    Serial.println("[CAN-A] Initializing MCP2515...");

    pinMode(PIN_MCP2515_RST, OUTPUT);
    digitalWrite(PIN_MCP2515_RST, HIGH);
    delay(100);
    digitalWrite(PIN_MCP2515_RST, LOW);   // Assert reset
    delay(100);
    digitalWrite(PIN_MCP2515_RST, HIGH);  // Release reset
    delay(100);

    SPI.begin(PIN_MCP2515_SCLK, PIN_MCP2515_MISO, PIN_MCP2515_MOSI, PIN_MCP2515_CS);

    s_canA.reset();

    // T-2Can usually has 16MHz crystal. AGENTS.md mentions 8MHz.
    // We use the configured CAN_CRYSTAL_MHZ.
    MCP2515::CAN_CLOCK clock = (CAN_CRYSTAL_MHZ == 8) ? MCP2515::MCP_8MHZ : MCP2515::MCP_16MHZ;

    if (s_canA.setBitrate(CAN_500KBPS, clock) != MCP2515::ERROR_OK) {
        Serial.println("[CAN-A] ERROR: Failed to set bitrate!");
        return false;
    }

    s_canA.setNormalMode();

    // -------------------------------------------------------------------------
    // Initialize Bus B (Internal TWAI)
    // -------------------------------------------------------------------------
    Serial.println("[CAN-B] Initializing Internal TWAI...");

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)PIN_CAN_TX,
        (gpio_num_t)PIN_CAN_RX,
        TWAI_MODE_NORMAL
    );
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
        if (twai_start() == ESP_OK) {
            Serial.println("[CAN-B] TWAI driver started");
            s_twaiEnabled = true;
        } else {
            Serial.println("[CAN-B] ERROR: Failed to start TWAI driver!");
        }
    } else {
        Serial.println("[CAN-B] ERROR: Failed to install TWAI driver!");
    }

    // Prepare the TX frame structure (reused for all balance commands)
    s_txFrame.can_id  = CAN_ID_BALANCE_CMD;
    s_txFrame.can_dlc = 8;  // Data Length Code: always 8 bytes for our messages
    memset(s_txFrame.data, 0, 8);
    s_txFrame.data[3] = 4;  // Fixed protocol bytes
    s_txFrame.data[4] = 3;

    // Verify SPI communication is working
    bool spiOk = canVerifySpiComm();
    if (!spiOk) {
        Serial.println("[CAN-A] WARNING: SPI communication may not be working!");
        Serial.println("[CAN-A] All reads returned 0xFF - check wiring");
    }

    // Print initial diagnostic info
    Serial.println("[CAN-A] MCP2515 initialized successfully");
    Serial.printf("[CAN-A] Config: 500kbps, %dMHz crystal\n", CAN_CRYSTAL_MHZ);
    Serial.printf("[CAN-A] Pins: CS=%d, SCLK=%d, MOSI=%d, MISO=%d, RST=%d\n",
                  PIN_MCP2515_CS, PIN_MCP2515_SCLK, PIN_MCP2515_MOSI,
                  PIN_MCP2515_MISO, PIN_MCP2515_RST);

    // Show initial register state
    uint8_t status = s_canA.getStatus();
    uint8_t errorFlags = s_canA.getErrorFlags();
    Serial.printf("[CAN-A] Initial STATUS=0x%02X EFLG=0x%02X\n", status, errorFlags);

    if (errorFlags != 0) {
        Serial.println("[CAN-A] WARNING: Error flags already set at init!");
    }

    return true;
}

void canPoll() {
    // -------------------------------------------------------------------------
    // Poll Bus A (MCP2515)
    // -------------------------------------------------------------------------
    s_canStats.lastErrorFlags = s_canA.getErrorFlags();
    s_canStats.lastInterrupts = s_canA.getInterrupts();
    s_canStats.lastStatus = s_canA.getStatus();

    while (s_canA.readMessage(&s_rxFrame) == MCP2515::ERROR_OK) {
        s_canStats.readAttempts++;
        processFrame(s_rxFrame.can_id, s_rxFrame.can_dlc, s_rxFrame.data, 0);
    }

    // -------------------------------------------------------------------------
    // Poll Bus B (Internal TWAI)
    // -------------------------------------------------------------------------
    if (s_twaiEnabled) {
        twai_message_t twaiMsg;
        // Receive messages without blocking (tick_to_wait = 0)
        while (twai_receive(&twaiMsg, 0) == ESP_OK) {
            s_canStats.readAttempts++;
            if (!(twaiMsg.flags & TWAI_MSG_FLAG_RTR)) {
                processFrame(twaiMsg.identifier, twaiMsg.data_length_code, twaiMsg.data, 1);
            }
        }
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

    // Send to Bus A (MCP2515)
    s_canStats.txAttempts++;
    if (s_canA.sendMessage(&s_txFrame) == MCP2515::ERROR_OK) {
        s_canStats.txSuccess++;
    }

    // Send to Bus B (TWAI)
    if (s_twaiEnabled) {
        twai_message_t twaiMsg;
        twaiMsg.identifier = s_txFrame.can_id;
        twaiMsg.data_length_code = s_txFrame.can_dlc;
        twaiMsg.flags = TWAI_MSG_FLAG_NONE;
        memcpy(twaiMsg.data, s_txFrame.data, 8);

        s_canStats.txAttempts++;
        if (twai_transmit(&twaiMsg, 0) == ESP_OK) {
            s_canStats.txSuccess++;
        }
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
    uint8_t status = s_canA.getStatus();
    uint8_t errorFlags = s_canA.getErrorFlags();

    // If SPI is not working, we typically get 0xFF (all ones) back
    // A working MCP2515 will return reasonable values
    bool spiOk = (status != 0xFF) || (errorFlags != 0xFF);

    return spiOk;
}

void canPrintDiagnostics() {
    Serial.println();
    Serial.println("=== CAN BUS DIAGNOSTICS ===");

    // -------------------------------------------------------------------------
    // Bus A (MCP2515)
    // -------------------------------------------------------------------------
    Serial.println("Bus A (MCP2515):");

    // Verify SPI communication
    bool spiOk = canVerifySpiComm();
    Serial.printf("  SPI Communication: %s\n", spiOk ? "OK" : "FAILED (check wiring)");

    // MCP2515 status registers
    uint8_t status = s_canA.getStatus();
    uint8_t errorFlags = s_canA.getErrorFlags();
    uint8_t interrupts = s_canA.getInterrupts();

    Serial.println("  MCP2515 Registers:");
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
    Serial.printf("  TEC:      %d (TX error count)\n", s_canA.errorCountTX());
    Serial.printf("  REC:      %d (RX error count)\n", s_canA.errorCountRX());

    // -------------------------------------------------------------------------
    // Bus B (TWAI)
    // -------------------------------------------------------------------------
    Serial.println();
    Serial.println("Bus B (Internal TWAI):");
    if (s_twaiEnabled) {
        twai_status_info_t twaiStatus;
        if (twai_get_status_info(&twaiStatus) == ESP_OK) {
            Serial.printf("  State:    %s\n",
                (twaiStatus.state == TWAI_STATE_RUNNING) ? "RUNNING" :
                (twaiStatus.state == TWAI_STATE_BUS_OFF) ? "BUS-OFF" : "STOPPED/RECOVERING");
            Serial.printf("  TX Err:   %d\n", twaiStatus.tx_error_counter);
            Serial.printf("  RX Err:   %d\n", twaiStatus.rx_error_counter);
            Serial.printf("  TX Failed: %d\n", twaiStatus.tx_failed_count);
            Serial.printf("  RX Miss:  %d\n", twaiStatus.rx_missed_count);
            Serial.printf("  ARB Lost: %d\n", twaiStatus.arb_lost_count);
            Serial.printf("  Bus Err:  %d\n", twaiStatus.bus_error_count);
        } else {
            Serial.println("  ERROR: Failed to get TWAI status");
        }
    } else {
        Serial.println("  TWAI not enabled");
    }

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
