/**
 * @file config.h
 * @brief Hardware configuration and constants for Outlander BMS on T-2Can
 *
 * EMBEDDED C++ CONCEPT: Header Guards
 * ------------------------------------
 * #pragma once (or #ifndef/#define/#endif) prevents this file from being
 * included multiple times. Unlike PHP's require_once which is runtime,
 * this is handled by the preprocessor at compile time.
 */
#pragma once

#include <Arduino.h>

// =============================================================================
// HARDWARE PIN DEFINITIONS
// =============================================================================
/**
 * EMBEDDED CONCEPT: GPIO (General Purpose Input/Output)
 * -----------------------------------------------------
 * Unlike web servers, embedded systems directly control hardware pins.
 * Each pin can be configured as input (read sensors) or output (control LEDs, etc).
 *
 * The T-2Can board uses ESP32-S3 with specific pins for different functions.
 * These values come from the board's schematic - they're fixed in hardware.
 */

// MCP2515 CAN Controller (external chip connected via SPI bus)
// SPI = Serial Peripheral Interface - a 4-wire protocol for chip-to-chip communication
constexpr uint8_t PIN_MCP2515_CS   = 10;  // Chip Select - tells MCP2515 "I'm talking to you"
constexpr uint8_t PIN_MCP2515_SCLK = 12;  // Serial Clock - timing signal
constexpr uint8_t PIN_MCP2515_MOSI = 11;  // Master Out Slave In - data TO the MCP2515
constexpr uint8_t PIN_MCP2515_MISO = 13;  // Master In Slave Out - data FROM the MCP2515
constexpr uint8_t PIN_MCP2515_RST  = 9;   // Reset pin - low pulse reboots the chip

// Built-in ESP32 TWAI (Bus B)
constexpr uint8_t PIN_CAN_TX = 17;
constexpr uint8_t PIN_CAN_RX = 18;

// =============================================================================
// BMS CONFIGURATION
// =============================================================================
/**
 * EMBEDDED CONCEPT: constexpr vs #define
 * --------------------------------------
 * In modern C++, prefer constexpr over #define for constants.
 * - constexpr: Type-safe, scoped, debuggable
 * - #define: Text substitution, no type checking, can cause weird bugs
 *
 * Think of constexpr as 'const' in PHP but evaluated at compile time.
 */

constexpr int BMS_MODULE_COUNT = 20;      // 10 per bus (Bus A + Bus B)
constexpr int CELLS_PER_MODULE = 8;       // Each CMU monitors 8 cells
constexpr int TEMPS_PER_MODULE = 3;       // Each CMU has 3 temperature sensors

// =============================================================================
// CAN BUS CONFIGURATION
// =============================================================================
/**
 * WHAT IS CAN BUS?
 * ----------------
 * CAN (Controller Area Network) is like a simple network for embedded devices.
 * Think of it as a broadcast network where:
 * - Every device sees every message
 * - Messages have an ID (like a topic) and up to 8 bytes of data
 * - No addressing - devices filter by message ID they care about
 *
 * Automotive CAN typically runs at 500 kbit/s.
 * The Outlander BMS broadcasts cell voltages and temps on specific CAN IDs.
 */

constexpr uint32_t CAN_BAUD_RATE = 500000;  // 500 kbit/s - standard for automotive
constexpr uint8_t  CAN_CRYSTAL_MHZ = 16;    // T-2Can's MCP2515 has 16MHz crystal

// CAN message IDs used by Outlander BMS
// Format: 0x0[CMU_number][message_type] where CMU 1-8 = 0x10-0x80
constexpr uint16_t CAN_ID_BALANCE_CMD = 0x3C3;  // We send this to control balancing

// Message type identifiers (lower nibble of CAN ID)
constexpr uint8_t MSG_TYPE_STATUS   = 0x1;  // Balance status + temperatures
constexpr uint8_t MSG_TYPE_VOLTS_1  = 0x2;  // Cells 1-4 voltages
constexpr uint8_t MSG_TYPE_VOLTS_2  = 0x3;  // Cells 5-8 voltages

// =============================================================================
// TIMING CONSTANTS
// =============================================================================
/**
 * EMBEDDED CONCEPT: Non-blocking timing
 * -------------------------------------
 * Unlike PHP where you might sleep() or wait for I/O, embedded systems
 * must stay responsive. We use millis() (milliseconds since boot) to
 * track time and check "has enough time passed?" instead of blocking.
 *
 * This is similar to event loops in Node.js or ReactPHP.
 */

constexpr unsigned long INTERVAL_CAN_SEND_MS  = 200;   // Send balance cmd every X ms
constexpr unsigned long INTERVAL_DISPLAY_MS   = 500;   // Update display every X ms

// =============================================================================
// DEFAULT VALUES
// =============================================================================

constexpr long DEFAULT_LOW_CELL_MV = 5000;  // Initial "lowest cell" value (impossibly high)
                                             // Will be replaced by actual readings

// =============================================================================
// WIFI CONFIGURATION
// =============================================================================
/**
 * WiFi credentials are stored in .config.h file which is excluded from git.
 * Use .config.h.template as a reference to create your own .config.h file.
 * 
 * If the file doesn't exist, empty defaults are used, which can still be
 * overridden via build flags in platformio.ini:
 *   build_flags = -D WIFI_SSID=\"MyNetwork\" -D WIFI_PASSWORD=\"MyPassword\"
 */
// Include private configuration if available
#if __has_include("../.config.h")
    #include "../.config.h"
#else
    #ifndef WIFI_SSID
    #define WIFI_SSID ""
    #endif

    #ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD ""
    #endif
#endif

constexpr unsigned long INTERVAL_WIFI_POLL_MS = 1000;  // Check WiFi state every 1s
