/**
 * @file ess_control.cpp
 * @brief ESS control implementation
 */

#include "ess_control.h"
#include "bms_data.h"
#include "protection.h"
#include "config.h"
#include <Arduino.h>

// Internal state
static EssState s_essState = ESS_STATE_IDLE;
static unsigned long s_prechargeStartMs = 0;

static void essSetOutputsIdle() {
    digitalWrite(PIN_OUT_CONTACTOR_MAIN, LOW);
    digitalWrite(PIN_OUT_PRECHARGE, LOW);
    digitalWrite(PIN_OUT_CONTACTOR_NEG, LOW);
    digitalWrite(PIN_OUT_CHARGER_EN, LOW);
    digitalWrite(PIN_OUT_DISCHARGE_EN, LOW);
}

static void essSetOutputsPrecharge() {
    digitalWrite(PIN_OUT_CONTACTOR_NEG, HIGH);
    digitalWrite(PIN_OUT_PRECHARGE, HIGH);
    digitalWrite(PIN_OUT_CONTACTOR_MAIN, LOW);
    digitalWrite(PIN_OUT_CHARGER_EN, LOW);
    digitalWrite(PIN_OUT_DISCHARGE_EN, LOW);
}

static void essSetOutputsContactor(bool chargerEnabled) {
    digitalWrite(PIN_OUT_CONTACTOR_NEG, HIGH);
    digitalWrite(PIN_OUT_CONTACTOR_MAIN, HIGH);
    digitalWrite(PIN_OUT_PRECHARGE, LOW);
    digitalWrite(PIN_OUT_CHARGER_EN, chargerEnabled ? HIGH : LOW);
    digitalWrite(PIN_OUT_DISCHARGE_EN, LOW);
}

void essInit() {
    s_essState = ESS_STATE_IDLE;
    s_prechargeStartMs = 0;
    g_bmsState.chargerEnabled = false;
    pinMode(PIN_INPUT_AC_PRESENT, INPUT_PULLDOWN);
    pinMode(PIN_INPUT_KEY_ON, INPUT_PULLDOWN);
    pinMode(PIN_INPUT_AUX, INPUT_PULLDOWN);
    pinMode(PIN_OUT_CONTACTOR_MAIN, OUTPUT);
    pinMode(PIN_OUT_PRECHARGE, OUTPUT);
    pinMode(PIN_OUT_CONTACTOR_NEG, OUTPUT);
    pinMode(PIN_OUT_CHARGER_EN, OUTPUT);
    pinMode(PIN_OUT_DISCHARGE_EN, OUTPUT);
    essSetOutputsIdle();
    Serial.println("[ESS] Control system initialized (ESS mode)");
}

bool essPrechargeReady(unsigned long startMs, unsigned long nowMs) {
    // Check time condition
    // Use unsigned arithmetic to handle millis() rollover correctly
    unsigned long elapsed = nowMs - startMs;
    if (elapsed < (unsigned long)g_bmsSettings.prechargeTimeMs) {
        return false; // Time not elapsed
    }
    
    // Check current condition
    // prechargeCurrent is in mA, currentAmps is in A
    // Convert currentAmps to mA and use absolute value
    float currentMa = abs(g_bmsState.currentAmps) * 1000.0f;
    if (currentMa > (float)g_bmsSettings.prechargeCurrent) {
        return false; // Current too high
    }
    
    return true; // Both conditions met
}

void essTick() {
    // Always check protection status first
    bool protectionOk = protectionCheck();
    
    // Get charge/discharge permissions
    bool canCharge = protectionCanCharge();
    bool canDischarge = protectionCanDischarge();

    bool acPresent = digitalRead(PIN_INPUT_AC_PRESENT) == HIGH;
    bool keyOn = digitalRead(PIN_INPUT_KEY_ON) == HIGH;
    bool startRequest = acPresent || keyOn;
    
    // State machine
    switch (s_essState) {
        case ESS_STATE_IDLE:
            // Starting state - all outputs should be off
            // In a real implementation, ensure precharge relay is OFF
            // and main contactor is OFF
            g_bmsState.chargerEnabled = false;
            essSetOutputsIdle();
            
            // State machine stays in IDLE until explicitly commanded to start precharge
            // (e.g., via CLI command calling essStartPrecharge() or auto-start logic)
            if (startRequest && protectionOk) {
                s_essState = ESS_STATE_PRECHARGE;
                s_prechargeStartMs = millis();
            }
            break;
            
        case ESS_STATE_PRECHARGE:
            if (!startRequest) {
                s_essState = ESS_STATE_IDLE;
                essSetOutputsIdle();
                break;
            }
            // Check if protection fault occurred during precharge
            if (!protectionOk) {
                Serial.println("[ESS] Protection fault during precharge - aborting");
                s_essState = ESS_STATE_FAULT;
                // In real implementation: turn off precharge relay
                essSetOutputsIdle();
                break;
            }
            g_bmsState.chargerEnabled = false;
            essSetOutputsPrecharge();
            
            // Check if precharge is complete
            if (essPrechargeReady(s_prechargeStartMs, millis())) {
                Serial.println("[ESS] Precharge complete - engaging main contactor");
                s_essState = ESS_STATE_CONTACTOR_ON;
                // In real implementation: 
                // - Turn off precharge relay
                // - Turn on main contactor
                // - Set PWM for contactor hold (contactorHoldDuty)
            }
            break;
            
        case ESS_STATE_CONTACTOR_ON:
            // Normal operation - main contactor engaged
            
            // Check for protection faults
            if (!protectionOk) {
                Serial.println("[ESS] Protection fault - disengaging contactor");
                s_essState = ESS_STATE_FAULT;
                // In real implementation: turn off main contactor
                essSetOutputsIdle();
                break;
            }

            if (!startRequest) {
                s_essState = ESS_STATE_IDLE;
                g_bmsState.chargerEnabled = false;
                essSetOutputsIdle();
                break;
            }
            
            // Control charger based on protection status
            if (canCharge) {
                // Charger can be enabled
                // In real implementation: enable charger output
                g_bmsState.chargerEnabled = true;
            } else {
                // Charger should be disabled
                // In real implementation: disable charger output
                g_bmsState.chargerEnabled = false;
            }
            
            essSetOutputsContactor(g_bmsState.chargerEnabled);
            // Note: In a real ESS, we'd also check canDischarge for load control
            // For now, we just maintain the contactor state
            break;
            
        case ESS_STATE_FAULT:
            // Protection fault - all outputs disabled
            // Remain in fault state until protection clears and system is reset
            
            // Ensure charger is disabled
            g_bmsState.chargerEnabled = false;
            essSetOutputsIdle();
            // In real implementation: 
            // - Main contactor OFF
            // - Precharge relay OFF
            // - Charger disabled
            
            // Check if protection has cleared
            if (protectionOk) {
                // Protection cleared, but stay in fault state
                // Require manual intervention to restart
                // User would call protectionClearFaults() and reset ESS state
            }
            break;
    }
}

// Helper functions for manual ESS state control (not exposed in header yet)
// These would be called from CLI commands in a full implementation

static void essStartPrecharge() {
    if (s_essState == ESS_STATE_IDLE) {
        Serial.println("[ESS] Starting precharge sequence");
        s_essState = ESS_STATE_PRECHARGE;
        s_prechargeStartMs = millis();
        // In real implementation: turn on precharge relay
    } else {
        Serial.println("[ESS] Cannot start precharge - not in IDLE state");
    }
}

static void essReset() {
    Serial.println("[ESS] Resetting to IDLE state");
    s_essState = ESS_STATE_IDLE;
    s_prechargeStartMs = 0;
    g_bmsState.chargerEnabled = false;
    // In real implementation: ensure all outputs are off
}

EssState essGetState() {
    return s_essState;
}

const char* essGetStateName() {
    switch (s_essState) {
        case ESS_STATE_IDLE:
            return "IDLE";
        case ESS_STATE_PRECHARGE:
            return "PRECHARGE";
        case ESS_STATE_CONTACTOR_ON:
            return "CONTACTOR_ON";
        case ESS_STATE_FAULT:
            return "FAULT";
        default:
            return "UNKNOWN";
    }
}

bool essIsContactorClosed() {
    return s_essState == ESS_STATE_CONTACTOR_ON;
}
