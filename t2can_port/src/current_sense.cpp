/**
 * @file current_sense.cpp
 * @brief Current sensing implementation
 */

#include "current_sense.h"
#include "bms_data.h"
#include "config.h"

// Low-pass filter state
static float s_filteredCurrent = 0.0f;
static const float FILTER_ALPHA = 0.1f;  // Simple exponential moving average

void currentSenseInit() {
    // Configure ADC for analog sensors if needed
    if (g_bmsSettings.currentSensorType == 1 || g_bmsSettings.currentSensorType == 3) {
        pinMode(PIN_CURRENT_SENSE_LOW, INPUT);
        pinMode(PIN_CURRENT_SENSE_HIGH, INPUT);
        // Set ADC resolution (ESP32 supports 9-12 bits, default is 12)
        analogReadResolution(12);
        // Set ADC attenuation (allows reading up to ~3.3V)
        analogSetPinAttenuation(PIN_CURRENT_SENSE_LOW, ADC_11db);
        analogSetPinAttenuation(PIN_CURRENT_SENSE_HIGH, ADC_11db);
        Serial.println("[CURRENT] Analog current sensing initialized");
    }
    
    g_bmsState.lastCurrentUpdate = millis();
}

void currentSenseUpdate() {
    float rawCurrent = 0.0f;
    unsigned long currentTime = millis();
    
    switch (g_bmsSettings.currentSensorType) {
        case 0:  // No current sensor
            g_bmsState.currentAmps = 0.0f;
            g_bmsState.currentSensorRange = 0;
            g_bmsState.currentSenseLowAmps = 0.0f;
            g_bmsState.currentSenseHighAmps = 0.0f;
            break;
            
        case 1: {  // Analog dual-range
            // Read both ADC channels
            int adc1 = analogRead(PIN_CURRENT_SENSE_LOW);
            int adc2 = analogRead(PIN_CURRENT_SENSE_HIGH);
            
            // Convert ADC readings to mV (assuming 3.3V reference, 12-bit ADC)
            float mv1 = (adc1 / 4095.0f) * 3300.0f;
            float mv2 = (adc2 / 4095.0f) * 3300.0f;
            
            // Apply offsets
            mv1 -= g_bmsSettings.offset1;
            mv2 -= g_bmsSettings.offset2;
            
            // Check deadband
            if (abs(mv1) < g_bmsSettings.currentDeadband) mv1 = 0.0f;
            if (abs(mv2) < g_bmsSettings.currentDeadband) mv2 = 0.0f;
            
            // SAFETY: Prevent division by zero in current conversion
            float current1 = 0.0f;
            float current2 = 0.0f;
            
            if (g_bmsSettings.conversionLow > 0.01f) {
                current1 = mv1 / g_bmsSettings.conversionLow;   // Low range
            } else {
                Serial.println("[CURRENT] ERROR: Invalid conversionLow, defaulting to 0A");
            }
            
            if (g_bmsSettings.conversionHigh > 0.01f) {
                current2 = mv2 / g_bmsSettings.conversionHigh;  // High range
            } else {
                Serial.println("[CURRENT] ERROR: Invalid conversionHigh, defaulting to 0A");
            }
            
            // Select range based on current magnitude
            // Use low range for small currents (higher resolution)
            // Use high range for large currents (wider range)
            if (abs(current1 * 1000.0f) < g_bmsSettings.rangeChangeCurrent) {
                rawCurrent = current1;
                g_bmsState.currentSensorRange = 1;  // Low range
            } else {
                rawCurrent = current2;
                g_bmsState.currentSensorRange = 2;  // High range
            }
            g_bmsState.currentSenseLowAmps = current1;
            g_bmsState.currentSenseHighAmps = current2;
            break;
        }
        
        case 2:  // CAN bus sensor
            // Current from CAN would be updated by CAN message handler
            // For now, just use existing value
            rawCurrent = g_bmsState.currentAmps;
            g_bmsState.currentSensorRange = 0;
            g_bmsState.currentSenseLowAmps = 0.0f;
            g_bmsState.currentSenseHighAmps = 0.0f;
            break;
            
        case 3: {  // Analog single-range
            // Read single ADC channel
            int adc1 = analogRead(PIN_CURRENT_SENSE_LOW);
            float mv1 = (adc1 / 4095.0f) * 3300.0f;
            mv1 -= g_bmsSettings.offset1;
            
            if (abs(mv1) < g_bmsSettings.currentDeadband) mv1 = 0.0f;
            
            // SAFETY: Prevent division by zero
            if (g_bmsSettings.conversionHigh > 0.01f) {
                rawCurrent = mv1 / g_bmsSettings.conversionHigh;
            } else {
                Serial.println("[CURRENT] ERROR: Invalid conversionHigh, defaulting to 0A");
                rawCurrent = 0.0f;
            }
            g_bmsState.currentSensorRange = 1;
            g_bmsState.currentSenseLowAmps = rawCurrent;
            g_bmsState.currentSenseHighAmps = 0.0f;
            break;
        }
        
        default:
            g_bmsState.currentAmps = 0.0f;
            g_bmsState.currentSensorRange = 0;
            g_bmsState.currentSenseLowAmps = 0.0f;
            g_bmsState.currentSenseHighAmps = 0.0f;
            return;
    }
    
    // Apply exponential moving average filter
    s_filteredCurrent = s_filteredCurrent * (1.0f - FILTER_ALPHA) + rawCurrent * FILTER_ALPHA;
    
    // SAFETY: Check for NaN or infinity in current measurements
    if (isnan(rawCurrent) || isinf(rawCurrent)) {
        Serial.println("[CURRENT] ERROR: Invalid current reading, resetting to 0A");
        rawCurrent = 0.0f;
    }
    
    if (isnan(s_filteredCurrent) || isinf(s_filteredCurrent)) {
        Serial.println("[CURRENT] ERROR: Invalid filtered current, resetting to 0A");
        s_filteredCurrent = 0.0f;
    }
    
    // SAFETY: Clamp current to reasonable values
    if (rawCurrent > 1000.0f) {
        Serial.println("[CURRENT] WARNING: Excessive current reading, clamping to 1000A");
        rawCurrent = 1000.0f;
    } else if (rawCurrent < -1000.0f) {
        Serial.println("[CURRENT] WARNING: Excessive discharge reading, clamping to -1000A");
        rawCurrent = -1000.0f;
    }
    
    g_bmsState.currentAmps = rawCurrent;
    g_bmsState.avgCurrentAmps = s_filteredCurrent;
    g_bmsState.lastCurrentUpdate = currentTime;
}

float currentSenseGetAmps() {
    return g_bmsState.avgCurrentAmps;
}
