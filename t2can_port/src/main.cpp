/**
 * @file main.cpp
 * @brief Entry point for Outlander PHEV BMS Reader on T-2Can
 *
 * EMBEDDED C++ CONCEPT: Program Structure
 * ----------------------------------------
 * Unlike PHP scripts that run top-to-bottom, Arduino programs have two
 * mandatory functions:
 *
 * 1. setup() - Runs ONCE when the board powers on or resets
 *              Use for: initializing hardware, setting pin modes, serial begin
 *
 * 2. loop()  - Runs FOREVER in an infinite loop after setup()
 *              Use for: main program logic, reading sensors, responding to events
 *
 * Think of it like:
 *   setup();          // Constructor / bootstrap
 *   while (true) {    // The framework calls loop() endlessly
 *       loop();
 *   }
 *
 * IMPORTANT: loop() should NOT block (no long delays, no infinite waits).
 * Use millis() timing pattern to do things periodically without blocking.
 */

#include <Arduino.h>
#include "config.h"
#include "bms_data.h"
#include "can_handler.h"
#include "serial_menu.h"
#include "wifi_handler.h"
#include "web_server.h"
#include "soc_calc.h"
#include "current_sense.h"
#include "protection.h"
#include "ess_control.h"

// =============================================================================
// TIMING STATE
// =============================================================================
/**
 * EMBEDDED CONCEPT: millis() for Non-Blocking Timing
 * --------------------------------------------------
 * millis() returns milliseconds since boot (wraps after ~50 days).
 * We store "last time we did X" and check if enough time has passed.
 *
 * This is like setInterval() in JavaScript, but manual.
 *
 * Pattern:
 *   static unsigned long lastTime = 0;
 *   if (millis() - lastTime >= INTERVAL) {
 *       lastTime = millis();
 *       doThing();
 *   }
 *
 * Why "millis() - lastTime" instead of "millis() > lastTime + INTERVAL"?
 * Because it handles the 50-day overflow correctly (unsigned arithmetic).
 */
static unsigned long s_lastCanSendTime = 0;
static unsigned long s_lastDisplayTime = 0;
static unsigned long s_lastWifiPollTime = 0;
static unsigned long s_lastSocUpdateTime = 0;
static unsigned long s_lastCurrentUpdateTime = 0;
static unsigned long s_lastProtectionCheckTime = 0;
static unsigned long s_lastSocSaveTime = 0;
static unsigned long s_lastEssTickTime = 0;

// Interval constants
constexpr unsigned long INTERVAL_SOC_UPDATE_MS = 100;        // Update SOC every 100ms
constexpr unsigned long INTERVAL_CURRENT_UPDATE_MS = 50;     // Read current every 50ms
constexpr unsigned long INTERVAL_PROTECTION_CHECK_MS = 500;  // Check protection every 500ms
constexpr unsigned long INTERVAL_SOC_SAVE_MS = 60000;        // Save SOC every 60 seconds
constexpr unsigned long INTERVAL_ESS_TICK_MS = 500;          // ESS control tick every 500ms

// =============================================================================
// SETUP
// =============================================================================
void setup() {
    /**
     * EMBEDDED CONCEPT: Serial Initialization
     * ---------------------------------------
     * Serial.begin(baudRate) initializes the UART at specified speed.
     * Both sides (ESP32 and your terminal) must use the same baud rate.
     * 115200 is a common, fast rate that all terminals support.
     */
    Serial.begin(115200);

    /**
     * EMBEDDED CONCEPT: Startup Delay
     * -------------------------------
     * USB serial on ESP32-S3 takes a moment to enumerate.
     * Without this delay, early Serial.print() calls might be lost.
     * This is only needed for USB CDC (serial over USB), not hardware UART.
     */
    delay(1000);

    // Banner
    Serial.println();
    Serial.println("========================================");
    Serial.println(" Outlander PHEV BMS Reader");
    Serial.println(" Hardware: LilyGO T-2Can (ESP32-S3)");
    Serial.println("========================================");
    Serial.println();

    // Load settings from NVS
    settingsLoad();

    // Initialize CAN bus
    if (!canInit()) {
        Serial.println("FATAL: CAN initialization failed!");
        Serial.println("Check hardware connections and restart.");

        /**
         * EMBEDDED CONCEPT: Halt on Fatal Error
         * -------------------------------------
         * If critical hardware fails, we can't do anything useful.
         * Infinite loop prevents running with broken state.
         * The watchdog timer will eventually reset the board (if enabled).
         */
        while (true) {
            delay(1000);
        }
    }

    // Initialize WiFi
    wifiInit();

    // Initialize web server (runs in background FreeRTOS task)
    webServerInit();

    // Initialize V2 features
    Serial.println();
    Serial.println("Initializing V2 features...");
    
    // Initialize current sensing
    currentSenseInit();
    
    // Initialize SOC calculation
    socInit();
    
    // Initialize protection system
    protectionInit();
    
    // Initialize ESS control (precharge, contactor, charger)
    essInit();

    Serial.println();
    Serial.println("Commands: 'r' = report, 'b' = balancing, 'h' = help");
    Serial.println("Waiting for BMS data (dots = heartbeat)...");
    Serial.println();
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
    /**
     * THE MAIN LOOP PATTERN
     * ---------------------
     * Each iteration:
     * 1. Process inputs (serial commands, CAN messages)
     * 2. Check if it's time to do periodic tasks
     * 3. Do those tasks if needed
     * 4. Return immediately (no blocking!)
     *
     * This runs thousands of times per second.
     */

    // 1. Process serial input (user commands)
    serialProcessInput();

    // 2. Process incoming CAN messages
    //    This reads all available messages and updates g_bmsState
    canPoll();

    // 3. Periodic task: Send balance command every 400ms
    if (millis() - s_lastCanSendTime >= INTERVAL_CAN_SEND_MS) {
        s_lastCanSendTime = millis();
        canSendBalanceCommand();
    }

    // 4. Periodic task: Display pack info every 500ms
    if (millis() - s_lastDisplayTime >= INTERVAL_DISPLAY_MS) {
        s_lastDisplayTime = millis();
        serialPrintPackInfo();
    }

    // 5. Periodic task: Poll WiFi state every 1000ms
    if (millis() - s_lastWifiPollTime >= INTERVAL_WIFI_POLL_MS) {
        s_lastWifiPollTime = millis();
        wifiPoll();
    }

    // 6. Periodic task: Update current sensing every 50ms
    if (millis() - s_lastCurrentUpdateTime >= INTERVAL_CURRENT_UPDATE_MS) {
        s_lastCurrentUpdateTime = millis();
        currentSenseUpdate();
    }

    // 7. Periodic task: Update SOC calculation every 100ms
    if (millis() - s_lastSocUpdateTime >= INTERVAL_SOC_UPDATE_MS) {
        s_lastSocUpdateTime = millis();
        socUpdate();
    }

    // 8. Periodic task: Check protection limits every 500ms
    if (millis() - s_lastProtectionCheckTime >= INTERVAL_PROTECTION_CHECK_MS) {
        s_lastProtectionCheckTime = millis();
        protectionCheck();
    }

    // 9. Periodic task: ESS control tick every 500ms
    if (millis() - s_lastEssTickTime >= INTERVAL_ESS_TICK_MS) {
        s_lastEssTickTime = millis();
        essTick();
    }

    // 10. Periodic task: Save SOC to NVS every 60 seconds
    if (millis() - s_lastSocSaveTime >= INTERVAL_SOC_SAVE_MS) {
        s_lastSocSaveTime = millis();
        if (g_bmsState.socInitialized) {
            socSave();
        }
    }

    /**
     * NOTE: No delay() here!
     * ----------------------
     * delay() blocks everything - no CAN messages processed, no serial input.
     * The loop runs as fast as possible, and periodic tasks self-throttle
     * using the millis() pattern above.
     *
     * If you need to slow down for debugging, use delay(10) at most.
     */
}
