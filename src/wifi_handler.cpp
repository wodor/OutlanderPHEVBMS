/**
 * @file wifi_handler.cpp
 * @brief WiFi connectivity implementation
 *
 * Uses a state machine pattern for non-blocking connection management
 * with exponential backoff for reconnection attempts.
 */

#include "wifi_handler.h"
#include "config.h"
#include "bms_data.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

// =============================================================================
// WIFI STATE MACHINE
// =============================================================================

enum class WifiState {
    DISCONNECTED,   // Not connected, waiting to attempt
    CONNECTING,     // Connection in progress
    CONNECTED       // Connected with valid IP
};

static WifiState s_wifiState = WifiState::DISCONNECTED;
static unsigned long s_lastAttemptTime = 0;
static unsigned long s_reconnectDelay = 1000;  // Start with 1 second
static const unsigned long MAX_RECONNECT_DELAY = 30000;  // Cap at 30 seconds
static bool s_otaStarted = false;

static void startOtaIfNeeded() {
    if (s_otaStarted || WiFi.status() != WL_CONNECTED) {
        return;
    }

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    if (OTA_PASSWORD[0] != '\0') {
        ArduinoOTA.setPassword(OTA_PASSWORD);
    }

    ArduinoOTA.onStart([]() {
        // Stop requesting cell discharge while flash contents are changing.
        g_bmsState.balancingEnabled = false;
        Serial.println("[OTA] Update started; balancing disabled");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update complete; rebooting");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        const unsigned int percent = total > 0 ? (progress * 100U) / total : 0;
        static unsigned int lastPercent = 101;
        if (percent != lastPercent && percent % 10U == 0U) {
            Serial.printf("[OTA] Progress: %u%%\n", percent);
            lastPercent = percent;
        }
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error %u\n", static_cast<unsigned int>(error));
    });
    ArduinoOTA.begin();
    s_otaStarted = true;

    Serial.print("[OTA] Ready as ");
    Serial.print(OTA_HOSTNAME);
    Serial.println(".local");
}

// =============================================================================
// PUBLIC FUNCTIONS
// =============================================================================

void wifiInit() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(OTA_HOSTNAME);
    WiFi.setAutoReconnect(false);  // We handle reconnection ourselves

    Serial.print("[WiFi] Connecting to ");
    Serial.print(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    s_lastAttemptTime = millis();
    s_reconnectDelay = 1000;

    // Wait for connection at boot (blocking, with timeout)
    unsigned long startTime = millis();
    const unsigned long BOOT_WIFI_TIMEOUT = 10000;  // 10 second timeout

    while (WiFi.status() != WL_CONNECTED && (millis() - startTime < BOOT_WIFI_TIMEOUT)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        s_wifiState = WifiState::CONNECTED;
        Serial.println("[WiFi] Connected!");
        Serial.print("[WiFi] IP Address: ");
        Serial.println(WiFi.localIP());
        startOtaIfNeeded();
    } else {
        s_wifiState = WifiState::DISCONNECTED;
        Serial.println("[WiFi] Connection failed - will retry in background");
    }
}

void wifiPoll() {
    switch (s_wifiState) {
        case WifiState::DISCONNECTED: {
            // Check if it's time to retry
            if (millis() - s_lastAttemptTime >= s_reconnectDelay) {
                Serial.println("[WiFi] Attempting reconnection...");
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
                s_wifiState = WifiState::CONNECTING;
                s_lastAttemptTime = millis();

                // Exponential backoff
                s_reconnectDelay = min(s_reconnectDelay * 2, MAX_RECONNECT_DELAY);
            }
            break;
        }

        case WifiState::CONNECTING: {
            wl_status_t status = WiFi.status();

            if (status == WL_CONNECTED) {
                s_wifiState = WifiState::CONNECTED;
                s_reconnectDelay = 1000;  // Reset backoff on success
                Serial.print("[WiFi] Connected! IP: ");
                Serial.println(WiFi.localIP());
                startOtaIfNeeded();
            } else if (status == WL_CONNECT_FAILED ||
                       status == WL_NO_SSID_AVAIL ||
                       (millis() - s_lastAttemptTime > 20000)) {  // 20s timeout
                s_wifiState = WifiState::DISCONNECTED;
                s_lastAttemptTime = millis();
                Serial.println("[WiFi] Connection failed, will retry...");
            }
            break;
        }

        case WifiState::CONNECTED: {
            if (WiFi.status() != WL_CONNECTED) {
                if (s_otaStarted) {
                    ArduinoOTA.end();
                    s_otaStarted = false;
                }
                s_wifiState = WifiState::DISCONNECTED;
                s_lastAttemptTime = millis();
                Serial.println("[WiFi] Connection lost!");
            }
            break;
        }
    }
}

void wifiHandleOta() {
    if (s_otaStarted && WiFi.status() == WL_CONNECTED) {
        ArduinoOTA.handle();
    }
}

bool wifiIsConnected() {
    return s_wifiState == WifiState::CONNECTED && WiFi.status() == WL_CONNECTED;
}

String wifiGetIP() {
    if (wifiIsConnected()) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}
