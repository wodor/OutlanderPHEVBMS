/**
 * @file can_output.cpp
 * @brief CAN output implementation for Battery-Emulator integration
 */

#include "can_output.h"
#include "config.h"
#include "bms_data.h"
#include "protection.h"
#include <SPI.h>
#include "mcp2515.h"
#include "driver/twai.h"

// =============================================================================
// PRIVATE MODULE STATE
// =============================================================================

static CanOutputStats s_outputStats = {};
static bool s_useBusA = false;  // Default: use Bus B (TWAI) for output
static bool s_enabled = true;   // Default: enabled

// Message buffers
static struct can_frame s_msg100;  // Status message
static struct can_frame s_msg101;  // Cell details
static struct can_frame s_msg102;  // Limits

// =============================================================================
// PRIVATE HELPER FUNCTIONS
// =============================================================================

/**
 * Send a CAN frame on the selected bus
 */
static bool sendFrame(struct can_frame* frame) {
    if (!s_enabled) {
        return false;
    }

    bool success = false;

    if (s_useBusA) {
        // Send on Bus A (MCP2515)
        // Note: This requires s_canA to be accessible
        // In production, you may need to refactor can_handler.cpp
        // to expose a sendMessage() function instead
        s_outputStats.lastBusUsed = 0;
        // success = (s_canA.sendMessage(frame) == MCP2515::ERROR_OK);
        // PLACEHOLDER: External access to s_canA needed
        Serial.println("[CAN-OUT] WARNING: Bus A output not yet implemented");
        Serial.println("[CAN-OUT] Refactor needed: expose s_canA in can_handler.h");
    } else {
        // Send on Bus B (Internal TWAI)
        s_outputStats.lastBusUsed = 1;

        twai_message_t twaiMsg;
        twaiMsg.identifier = frame->can_id;
        twaiMsg.data_length_code = frame->can_dlc;
        twaiMsg.flags = TWAI_MSG_FLAG_NONE;
        memcpy(twaiMsg.data, frame->data, 8);

        if (twai_transmit(&twaiMsg, pdMS_TO_TICKS(10)) == ESP_OK) {
            success = true;
        }
    }

    if (success) {
        s_outputStats.messagesSent++;
        s_outputStats.lastSendTime = millis();
    }

    return success;
}

/**
 * Build message 0x100: BMS Status
 *
 * Byte layout:
 * [0-1]: Pack voltage (uint16_t, 0.1V resolution) - little endian
 * [2-3]: Current (int16_t, 0.1A resolution) - little endian
 * [4]:   SOC (uint8_t, 1% resolution)
 * [5]:   Highest temperature (uint8_t, 1°C offset by +40)
 * [6]:   Lowest temperature (uint8_t, 1°C offset by +40)
 * [7]:   Status flags (bit 0: charger enabled, bit 1: discharge enabled)
 */
static void buildMessage0x100() {
    s_msg100.can_id  = 0x100;
    s_msg100.can_dlc = 8;

    // Pack voltage in 0.1V units (e.g., 320.5V → 3205)
    uint16_t voltage_dV = (uint16_t)(g_bmsState.packVoltage * 10.0f);
    s_msg100.data[0] = voltage_dV & 0xFF;         // Low byte
    s_msg100.data[1] = (voltage_dV >> 8) & 0xFF;  // High byte

    // Current in 0.1A units (e.g., 15.2A → 152)
    // Positive = charging, negative = discharging
    int16_t current_dA = (int16_t)(g_bmsState.currentAmps * 10.0f);
    s_msg100.data[2] = current_dA & 0xFF;         // Low byte
    s_msg100.data[3] = (current_dA >> 8) & 0xFF;  // High byte

    // SOC (0-100%)
    s_msg100.data[4] = (uint8_t)constrain(g_bmsState.soc, 0, 100);

    // Temperatures (offset by +40 to handle negatives, e.g., -10°C → 30, 50°C → 90)
    int16_t highTemp = (int16_t)(g_bmsState.highestTemp + 40.0f);
    int16_t lowTemp  = (int16_t)(g_bmsState.lowestTemp + 40.0f);
    s_msg100.data[5] = (uint8_t)constrain(highTemp, 0, 255);
    s_msg100.data[6] = (uint8_t)constrain(lowTemp, 0, 255);

    // Status flags
    uint8_t flags = 0;
    if (g_bmsState.chargerEnabled) flags |= 0x01;  // Bit 0: charger enabled
    // Bit 1 could be discharge enabled (future)
    // Bit 2-7 reserved for future use
    s_msg100.data[7] = flags;
}

/**
 * Build message 0x101: Cell Details
 *
 * Byte layout:
 * [0-1]: Highest cell voltage (uint16_t, mV) - little endian
 * [2-3]: Lowest cell voltage (uint16_t, mV) - little endian
 * [4-5]: Average cell voltage (uint16_t, mV) - little endian
 * [6]:   Number of cells detected (uint8_t)
 * [7]:   Balance status (0=off, 1=active)
 */
static void buildMessage0x101() {
    s_msg101.can_id  = 0x101;
    s_msg101.can_dlc = 8;

    // Cell voltages in mV
    uint16_t highCell = (uint16_t)g_bmsState.highestCellMv;
    uint16_t lowCell  = (uint16_t)g_bmsState.lowestCellMv;
    uint16_t avgCell  = (uint16_t)(g_bmsState.avgCellVoltage * 1000.0f);

    s_msg101.data[0] = highCell & 0xFF;
    s_msg101.data[1] = (highCell >> 8) & 0xFF;
    s_msg101.data[2] = lowCell & 0xFF;
    s_msg101.data[3] = (lowCell >> 8) & 0xFF;
    s_msg101.data[4] = avgCell & 0xFF;
    s_msg101.data[5] = (avgCell >> 8) & 0xFF;

    // Count detected cells (cells with valid voltages 1500-4500mV)
    uint8_t cellCount = 0;
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (!g_bmsState.modules[m].present) continue;
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            long v = g_bmsState.modules[m].voltages[c];
            if (v >= 1500 && v <= 4500) {
                cellCount++;
            }
        }
    }
    s_msg101.data[6] = cellCount;

    // Balance status
    s_msg101.data[7] = g_bmsState.balancingEnabled ? 1 : 0;
}

/**
 * Build message 0x102: Limits and Protection
 *
 * Byte layout:
 * [0-1]: Max charge current (uint16_t, 0.1A resolution) - little endian
 * [2-3]: Max discharge current (uint16_t, 0.1A resolution) - little endian
 * [4]:   Warning flags (bitfield)
 * [5]:   Fault flags (bitfield)
 * [6-7]: Reserved for future use
 */
static void buildMessage0x102() {
    s_msg102.can_id  = 0x102;
    s_msg102.can_dlc = 8;

    // Get current limits from protection system
    uint16_t maxCharge = (uint16_t)abs(g_bmsState.targetChargeCurrent);
    uint16_t maxDischarge = (uint16_t)abs(g_bmsState.targetDischargeCurrent);

    s_msg102.data[0] = maxCharge & 0xFF;
    s_msg102.data[1] = (maxCharge >> 8) & 0xFF;
    s_msg102.data[2] = maxDischarge & 0xFF;
    s_msg102.data[3] = (maxDischarge >> 8) & 0xFF;

    const char* protectionStatus = protectionGetStatus();

    // Warning flags (bit 0-7)
    uint8_t warnings = 0;
    if (strcmp(protectionStatus, "IMBALANCE WARNING") == 0) warnings |= 0x01;
    s_msg102.data[4] = warnings;

    // Fault flags (bit 0-7)
    uint8_t faults = 0;
    if (strcmp(protectionStatus, "OVERVOLTAGE") == 0) faults |= 0x01;         // Bit 0
    if (strcmp(protectionStatus, "UNDERVOLTAGE") == 0) faults |= 0x02;        // Bit 1
    if (strcmp(protectionStatus, "OVERTEMP") == 0) faults |= 0x04;            // Bit 2
    if (strcmp(protectionStatus, "UNDERTEMP") == 0) faults |= 0x08;           // Bit 3
    if (strcmp(protectionStatus, "IMBALANCE WARNING") == 0) faults |= 0x20;   // Bit 5
    if (strcmp(protectionStatus, "COMMUNICATION FAULT") == 0) faults |= 0x40; // Bit 6
    s_msg102.data[5] = faults;

    // Reserved bytes
    s_msg102.data[6] = 0;
    s_msg102.data[7] = 0;
}

// =============================================================================
// PUBLIC API IMPLEMENTATION
// =============================================================================

void canOutputInit() {
    Serial.println("[CAN-OUT] Initializing BMS output messages...");

    s_outputStats.enabled = s_enabled;
    s_outputStats.messagesSent = 0;
    s_outputStats.lastSendTime = 0;
    s_outputStats.lastBusUsed = s_useBusA ? 0 : 1;

    // Initialize message structures
    memset(&s_msg100, 0, sizeof(s_msg100));
    memset(&s_msg101, 0, sizeof(s_msg101));
    memset(&s_msg102, 0, sizeof(s_msg102));

    Serial.printf("[CAN-OUT] Output bus: %s\n", s_useBusA ? "Bus A (MCP2515)" : "Bus B (TWAI)");
    Serial.printf("[CAN-OUT] Enabled: %s\n", s_enabled ? "YES" : "NO");
    Serial.println("[CAN-OUT] Messages: 0x100 (status), 0x101 (cells), 0x102 (limits)");
}

void canOutputSendBmsData() {
    if (!s_enabled) {
        return;
    }

    // Build all three messages with current BMS state
    buildMessage0x100();
    buildMessage0x101();
    buildMessage0x102();

    // Send messages sequentially
    // Small delay between messages to avoid bus congestion
    sendFrame(&s_msg100);
    delay(5);  // 5ms inter-message spacing

    sendFrame(&s_msg101);
    delay(5);

    sendFrame(&s_msg102);
}

void canOutputSetBus(bool useBusA) {
    s_useBusA = useBusA;
    Serial.printf("[CAN-OUT] Output bus changed to: %s\n",
                  useBusA ? "Bus A (MCP2515)" : "Bus B (TWAI)");
}

CanOutputStats canOutputGetStats() {
    s_outputStats.enabled = s_enabled;
    return s_outputStats;
}

void canOutputSetEnabled(bool enable) {
    s_enabled = enable;
    Serial.printf("[CAN-OUT] Output %s\n", enable ? "ENABLED" : "DISABLED");
}
