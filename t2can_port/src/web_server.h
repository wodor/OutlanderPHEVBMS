/**
 * @file web_server.h
 * @brief Async web server for BMS monitoring dashboard
 *
 * Provides REST API endpoints and an embedded HTML dashboard.
 * Uses ESPAsyncWebServer which runs in a background FreeRTOS task.
 */
#pragma once

#include <Arduino.h>

/**
 * Initialize the async web server.
 * Sets up all API endpoints and starts listening on port 80.
 * Call after WiFi initialization.
 */
void webServerInit();
