/**
 * @file serial_menu.h
 * @brief Serial console interface for user interaction and data display
 *
 * Handles:
 * - Processing user input commands
 * - Displaying BMS pack information
 */
#pragma once

#include <Arduino.h>

/**
 * Process any pending serial input
 *
 * EMBEDDED CONCEPT: Serial Communication
 * --------------------------------------
 * Serial (UART) is the simplest way to communicate with a microcontroller.
 * It's like a bidirectional text pipe - we send printf-style output,
 * user can send single-character commands.
 *
 * The T-2Can appears as a USB serial port on your computer.
 * Use PlatformIO's Serial Monitor or any terminal (screen, minicom, etc).
 *
 * Call this from loop() to handle user input.
 */
void serialProcessInput();

/**
 * Print current BMS pack information to serial
 *
 * Displays all cell voltages and temperatures in a readable format.
 * Call this periodically (every ~500ms) from loop().
 */
void serialPrintPackInfo();
