/**
 * @file wifi_handler.h
 * @brief WiFi connectivity module for BMS remote monitoring
 *
 * Provides non-blocking WiFi connection management with automatic reconnection.
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize WiFi in station mode.
 * Begins connection attempt but does not block.
 */
void wifiInit();

/**
 * Poll WiFi state machine.
 * Call periodically (e.g., every 1000ms) to handle reconnection logic.
 */
void wifiPoll();

/**
 * Service Arduino OTA requests.
 * Call on every main-loop iteration so uploads remain responsive.
 */
void wifiHandleOta();

/**
 * Check if WiFi is connected.
 * @return true if connected with valid IP
 */
bool wifiIsConnected();

/**
 * Get the current IP address as a string.
 * @return IP address string, or "0.0.0.0" if not connected
 */
String wifiGetIP();
