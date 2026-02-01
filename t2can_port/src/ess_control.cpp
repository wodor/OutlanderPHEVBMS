/**
 * @file ess_control.cpp
 * @brief ESS control implementation
 */

#include "ess_control.h"
#include "bms_data.h"
#include "protection.h"
#include <Arduino.h>

// ESS state machine states
enum EssState {
    ESS_IDLE,           // Initial state, contactors off
    ESS_PRECHARGE,      // Precharge in progress
    ESS_CONTACTOR_ON,   // Main contactor engaged, normal operation
    ESS_FAULT           // Protection fault, all outputs disabled
};

// Internal state
static EssState s_essState = ESS_IDLE;
static unsigned long s_prechargeStartMs = 0;

void essInit() {
    s_essState = ESS_IDLE;
    s_prechargeStartMs = 0;
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
    
    // State machine
    switch (s_essState) {
        case ESS_IDLE:
            // Starting state - all outputs should be off
            // In a real implementation, ensure precharge relay is OFF
            // and main contactor is OFF
            
            // For now, we transition to PRECHARGE to begin the sequence
            // In production, this would be triggered by user command or auto-start
            // For this implementation, we just log the state
            break;
            
        case ESS_PRECHARGE:
            // Check if protection fault occurred during precharge
            if (!protectionOk) {
                Serial.println("[ESS] Protection fault during precharge - aborting");
                s_essState = ESS_FAULT;
                // In real implementation: turn off precharge relay
                break;
            }
            
            // Check if precharge is complete
            if (essPrechargeReady(s_prechargeStartMs, millis())) {
                Serial.println("[ESS] Precharge complete - engaging main contactor");
                s_essState = ESS_CONTACTOR_ON;
                // In real implementation: 
                // - Turn off precharge relay
                // - Turn on main contactor
                // - Set PWM for contactor hold (contactorHoldDuty)
            }
            break;
            
        case ESS_CONTACTOR_ON:
            // Normal operation - main contactor engaged
            
            // Check for protection faults
            if (!protectionOk) {
                Serial.println("[ESS] Protection fault - disengaging contactor");
                s_essState = ESS_FAULT;
                // In real implementation: turn off main contactor
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
            
            // Note: In a real ESS, we'd also check canDischarge for load control
            // For now, we just maintain the contactor state
            break;
            
        case ESS_FAULT:
            // Protection fault - all outputs disabled
            // Remain in fault state until protection clears and system is reset
            
            // Ensure charger is disabled
            g_bmsState.chargerEnabled = false;
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

void essStartPrecharge() {
    if (s_essState == ESS_IDLE) {
        Serial.println("[ESS] Starting precharge sequence");
        s_essState = ESS_PRECHARGE;
        s_prechargeStartMs = millis();
        // In real implementation: turn on precharge relay
    } else {
        Serial.println("[ESS] Cannot start precharge - not in IDLE state");
    }
}

void essReset() {
    Serial.println("[ESS] Resetting to IDLE state");
    s_essState = ESS_IDLE;
    s_prechargeStartMs = 0;
    g_bmsState.chargerEnabled = false;
    // In real implementation: ensure all outputs are off
}
