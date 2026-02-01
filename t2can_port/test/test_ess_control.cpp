/**
 * @file test_ess_control.cpp
 * @brief Unit tests for ESS (Energy Storage System) control
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/protection.h"
#include "../src/ess_control.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;
extern unsigned long g_mockMillis;

/**
 * Test that default settings for precharge are correct
 */
void test_settings_precharge_defaults() {
    BmsSettings settings;
    
    TEST_ASSERT_EQUAL_INT(5000, settings.prechargeTimeMs);
    TEST_ASSERT_EQUAL_INT(1000, settings.prechargeCurrent);
    TEST_ASSERT_EQUAL_INT(50, settings.contactorHoldDuty);
}

/**
 * Test that precharge completes when both time and current conditions are met
 */
void test_precharge_completes_time_and_current() {
    g_bmsSettings.prechargeTimeMs = 5000;
    g_bmsSettings.prechargeCurrent = 1000; // 1000 mA = 1.0 A
    
    unsigned long startMs = 1000;
    unsigned long nowMs;
    
    // Test 1: Time not elapsed, current low
    g_bmsState.currentAmps = 0.5f; // 500 mA
    nowMs = 4000; // 3 seconds elapsed
    bool ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_FALSE(ready); // Not ready - time not elapsed
    
    // Test 2: Time elapsed, current high
    g_bmsState.currentAmps = 1.5f; // 1500 mA
    nowMs = 7000; // 6 seconds elapsed
    ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_FALSE(ready); // Not ready - current too high
    
    // Test 3: Time elapsed, current low - should be ready
    g_bmsState.currentAmps = 0.8f; // 800 mA
    nowMs = 6500; // 5.5 seconds elapsed
    ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_TRUE(ready); // Ready - both conditions met
    
    // Test 4: Negative current (discharge) within threshold
    g_bmsState.currentAmps = -0.9f; // -900 mA
    nowMs = 7000; // 6 seconds elapsed
    ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_TRUE(ready); // Ready - abs(current) within threshold
}

/**
 * Test that precharge does not complete if current is too high
 */
void test_precharge_not_complete_if_current_high() {
    g_bmsSettings.prechargeTimeMs = 3000;
    g_bmsSettings.prechargeCurrent = 500; // 500 mA = 0.5 A
    
    unsigned long startMs = 1000;
    unsigned long nowMs = 5000; // 4 seconds elapsed (time condition met)
    
    // Current just above threshold
    g_bmsState.currentAmps = 0.51f; // 510 mA
    bool ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_FALSE(ready); // Not ready - current too high
    
    // Current well above threshold
    g_bmsState.currentAmps = 2.0f; // 2000 mA
    ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_FALSE(ready); // Not ready - current too high
}

/**
 * Test that precharge aborts on protection fault
 */
void test_precharge_aborts_on_fault() {
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsSettings.prechargeTimeMs = 5000;
    g_bmsSettings.prechargeCurrent = 1000;
    
    // Setup normal conditions first
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3800; // 3.8V - normal
    g_bmsState.modules[0].temperatures[0] = 25000; // 25°C - normal
    g_bmsState.currentAmps = 0.5f; // 500 mA - low
    g_bmsState.updatePackStatistics();
    
    // Verify protection is OK
    bool protectionOk = protectionCheck();
    TEST_ASSERT_TRUE(protectionOk);
    
    // Verify precharge would be ready
    unsigned long startMs = 1000;
    unsigned long nowMs = 7000; // 6 seconds elapsed
    bool ready = essPrechargeReady(startMs, nowMs);
    TEST_ASSERT_TRUE(ready);
    
    // Now trigger overvoltage fault
    g_bmsState.modules[0].voltages[0] = 4250; // 4.25V - overvoltage
    g_bmsState.updatePackStatistics();
    protectionOk = protectionCheck();
    TEST_ASSERT_FALSE(protectionOk);
    
    // Even though precharge conditions are met, should not proceed with fault
    // This is tested in essTick() state machine, but here we verify protection works
    const char* status = protectionGetStatus();
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", status);
}

/**
 * Test charger permission integration with protection
 */
void test_charger_permission_integration() {
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsSettings.chargeVoltage = 4.1f;
    g_bmsSettings.chargeTemp = 0.0f;
    
    // Setup normal conditions
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3800; // 3.8V - normal
    g_bmsState.modules[0].temperatures[0] = 25000; // 25°C - normal
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    // Should allow charging in normal conditions
    bool canCharge = protectionCanCharge();
    TEST_ASSERT_TRUE(canCharge);
    
    // Trigger overvoltage - should block charging
    g_bmsState.modules[0].voltages[0] = 4250; // 4.25V - overvoltage
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    canCharge = protectionCanCharge();
    TEST_ASSERT_FALSE(canCharge);
}

/**
 * Test discharge permission integration with protection
 */
void test_discharge_permission_integration() {
    g_bmsSettings.underVoltage = 3.0f;
    g_bmsSettings.dischargeVoltage = 3.2f;
    
    // Setup normal conditions
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3500; // 3.5V - normal
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    // Should allow discharging in normal conditions
    bool canDischarge = protectionCanDischarge();
    TEST_ASSERT_TRUE(canDischarge);
    
    // Trigger undervoltage - need to wait for debounce
    g_mockMillis = 1; // Start time
    g_bmsState.modules[0].voltages[0] = 2950; // 2.95V - undervoltage
    g_bmsState.updatePackStatistics();
    protectionCheck(); // First check - starts debounce
    
    // Advance time past debounce period
    g_mockMillis = 1200; // 1.2 seconds later
    protectionCheck(); // Second check - should detect fault after debounce
    
    canDischarge = protectionCanDischarge();
    TEST_ASSERT_FALSE(canDischarge);
}
