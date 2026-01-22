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

// For dual-range analog sensors
static const int ADC_CHANNEL_1 = 0;  // GPIO pin for analog input 1
static const int ADC_CHANNEL_2 = 1;  // GPIO pin for analog input 2

void currentSenseInit() {
    // Configure ADC for analog sensors if needed
    if (g_bmsSettings.currentSensorType == 1 || g_bmsSettings.currentSensorType == 3) {
        // Set ADC resolution (ESP32 supports 9-12 bits, default is 12)
        analogReadResolution(12);
        // Set ADC attenuation (allows reading up to 3.3V)
        // Note: On ESP32-S3, ADC pins are different. We'll use placeholder pins here.
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
            break;
            
        case 1: {  // Analog dual-range
            // Read both ADC channels
            // Note: In a real implementation, you'd use actual GPIO pins
            // For now, this is placeholder code
            int adc1 = 2048;  // Placeholder - would be analogRead(ADC_PIN_1)
            int adc2 = 2048;  // Placeholder - would be analogRead(ADC_PIN_2)
            
            // Convert ADC readings to mV (assuming 3.3V reference, 12-bit ADC)
            float mv1 = (adc1 / 4095.0f) * 3300.0f;
            float mv2 = (adc2 / 4095.0f) * 3300.0f;
            
            // Apply offsets
            mv1 -= g_bmsSettings.offset1;
            mv2 -= g_bmsSettings.offset2;
            
            // Check deadband
            if (abs(mv1) < g_bmsSettings.currentDeadband) mv1 = 0.0f;
            if (abs(mv2) < g_bmsSettings.currentDeadband) mv2 = 0.0f;
            
            // Select range based on current magnitude
            // Use low range for small currents (higher resolution)
            // Use high range for large currents (wider range)
            float current1 = mv1 / g_bmsSettings.conversionLow;   // Low range
            float current2 = mv2 / g_bmsSettings.conversionHigh;  // High range
            
            if (abs(current1 * 1000.0f) < g_bmsSettings.rangeChangeCurrent) {
                rawCurrent = current1;
                g_bmsState.currentSensorRange = 1;  // Low range
            } else {
                rawCurrent = current2;
                g_bmsState.currentSensorRange = 2;  // High range
            }
            break;
        }
        
        case 2:  // CAN bus sensor
            // Current from CAN would be updated by CAN message handler
            // For now, just use existing value
            rawCurrent = g_bmsState.currentAmps;
            g_bmsState.currentSensorRange = 0;
            break;
            
        case 3: {  // Analog single-range
            // Read single ADC channel
            int adc1 = 2048;  // Placeholder
            float mv1 = (adc1 / 4095.0f) * 3300.0f;
            mv1 -= g_bmsSettings.offset1;
            
            if (abs(mv1) < g_bmsSettings.currentDeadband) mv1 = 0.0f;
            
            rawCurrent = mv1 / g_bmsSettings.conversionHigh;
            g_bmsState.currentSensorRange = 1;
            break;
        }
        
        default:
            g_bmsState.currentAmps = 0.0f;
            g_bmsState.currentSensorRange = 0;
            return;
    }
    
    // Apply exponential moving average filter
    s_filteredCurrent = s_filteredCurrent * (1.0f - FILTER_ALPHA) + rawCurrent * FILTER_ALPHA;
    
    g_bmsState.currentAmps = rawCurrent;
    g_bmsState.avgCurrentAmps = s_filteredCurrent;
    g_bmsState.lastCurrentUpdate = currentTime;
}

float currentSenseGetAmps() {
    return g_bmsState.avgCurrentAmps;
}
