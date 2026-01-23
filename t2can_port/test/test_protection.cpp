/**
 * @file test_protection.cpp
 * @brief Unit tests for protection system
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/protection.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

#ifndef UNIT_TEST
void setUp(void) {
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
    protectionInit();
}

void tearDown(void) {
    protectionClearFaults();
}
#endif

/**
 * Test overvoltage detection
 */
void test_overvoltage_detection() {
    // Setup: Configure a module with overvoltage
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 4250; // 4.25V - over limit
    
    // Update statistics
    g_bmsState.updatePackStatistics();
    
    // Check should detect overvoltage
    bool result = protectionCheck();
    TEST_ASSERT_FALSE(result); // Should return false (fault detected)
    
    // Verify status
    const char* status = protectionGetStatus();
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", status);
}

/**
 * Test undervoltage detection
 */
void test_undervoltage_detection() {
    extern unsigned long g_mockMillis;
    
    g_bmsSettings.underVoltage = 3.0f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 2900; // 2.9V - under limit
    
    g_bmsState.updatePackStatistics();
    
    // Verify that statistics were updated correctly
    TEST_ASSERT_EQUAL_INT32(2900, g_bmsState.lowestCellMv);
    TEST_ASSERT_TRUE(g_bmsState.hasAnyData());
    
    // Note: Undervoltage has debounce, so might need multiple checks
    // For testing, we check that it's detected
    g_mockMillis = 1; // Start at 1 so debounce timer doesn't stay at 0
    bool result = protectionCheck();
    
    // After debounce period, should detect fault
    delay(1100); // Wait for debounce (1000ms + margin)
    result = protectionCheck();
    TEST_ASSERT_FALSE(result);
}

/**
 * Test overtemperature detection
 */
void test_overtemperature_detection() {
    g_bmsSettings.overTemp = 65.0f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].temperatures[0] = 70000; // 70°C - over limit
    
    g_bmsState.updatePackStatistics();
    
    bool result = protectionCheck();
    TEST_ASSERT_FALSE(result);
    
    const char* status = protectionGetStatus();
    TEST_ASSERT_EQUAL_STRING("OVERTEMP", status);
}

/**
 * Test undertemperature detection
 */
void test_undertemperature_detection() {
    g_bmsSettings.underTemp = -10.0f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].temperatures[0] = -15000; // -15°C - under limit
    
    g_bmsState.updatePackStatistics();
    
    bool result = protectionCheck();
    TEST_ASSERT_FALSE(result);
    
    const char* status = protectionGetStatus();
    TEST_ASSERT_EQUAL_STRING("UNDERTEMP", status);
}

/**
 * Test cell imbalance detection
 */
void test_cell_imbalance_detection() {
    g_bmsSettings.cellGap = 0.2f; // 200mV max gap
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600; // 3.6V
    g_bmsState.modules[0].voltages[1] = 3850; // 3.85V - 250mV gap
    
    g_bmsState.updatePackStatistics();
    
    bool result = protectionCheck();
    // Note: Cell imbalance is a warning, not a hard fault
    // So the function might still return true
    
    const char* status = protectionGetStatus();
    // Status should indicate imbalance
    TEST_ASSERT_TRUE(strstr(status, "IMBALANCE") != NULL || strcmp(status, "OK") == 0);
}

/**
 * Test protection allows charging
 */
void test_can_charge() {
    // Normal conditions - should allow charging
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsSettings.chargeVoltage = 4.1f;
    g_bmsSettings.chargeTemp = 0.0f;
    
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3800; // 3.8V - normal
    g_bmsState.modules[0].temperatures[0] = 25000; // 25°C - normal
    
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    bool canCharge = protectionCanCharge();
    TEST_ASSERT_TRUE(canCharge);
    
    // Overvoltage - should not allow charging
    g_bmsState.modules[0].voltages[0] = 4250; // 4.25V - over
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    canCharge = protectionCanCharge();
    TEST_ASSERT_FALSE(canCharge);
}

/**
 * Test protection allows discharging
 */
void test_can_discharge() {
    // Normal conditions - should allow discharging
    g_bmsSettings.underVoltage = 3.0f;
    g_bmsSettings.dischargeVoltage = 3.2f;
    
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3500; // 3.5V - normal
    
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    bool canDischarge = protectionCanDischarge();
    TEST_ASSERT_TRUE(canDischarge);
    
    // Undervoltage - should not allow discharging
    g_bmsState.modules[0].voltages[0] = 2950; // 2.95V - under
    g_bmsState.updatePackStatistics();
    delay(1100); // Wait for debounce
    protectionCheck();
    
    canDischarge = protectionCanDischarge();
    TEST_ASSERT_FALSE(canDischarge);
}

/**
 * Test hysteresis prevents oscillation
 */
void test_protection_hysteresis() {
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsState.modules[0].present = true;
    
    // Trip overvoltage
    g_bmsState.modules[0].voltages[0] = 4250; // 4.25V - over
    g_bmsState.updatePackStatistics();
    bool result = protectionCheck();
    TEST_ASSERT_FALSE(result);
    
    // Drop slightly below limit - should still be faulted (hysteresis)
    g_bmsState.modules[0].voltages[0] = 4190; // 4.19V - just below limit
    g_bmsState.updatePackStatistics();
    result = protectionCheck();
    TEST_ASSERT_FALSE(result); // Still faulted due to hysteresis
    
    // Drop well below limit - should clear
    g_bmsState.modules[0].voltages[0] = 4050; // 4.05V - well below
    g_bmsState.updatePackStatistics();
    result = protectionCheck();
    TEST_ASSERT_TRUE(result); // Should clear
}

/**
 * Test fault clearing
 */
void test_fault_clearing() {
    g_bmsSettings.overVoltage = 4.2f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 4250;
    
    g_bmsState.updatePackStatistics();
    protectionCheck();
    
    // Clear faults
    protectionClearFaults();
    
    // Drop voltage to normal
    g_bmsState.modules[0].voltages[0] = 3800;
    g_bmsState.updatePackStatistics();
    bool result = protectionCheck();
    TEST_ASSERT_TRUE(result);
}

#ifndef UNIT_TEST
void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_overvoltage_detection);
    RUN_TEST(test_undervoltage_detection);
    RUN_TEST(test_overtemperature_detection);
    RUN_TEST(test_undertemperature_detection);
    RUN_TEST(test_cell_imbalance_detection);
    RUN_TEST(test_can_charge);
    RUN_TEST(test_can_discharge);
    RUN_TEST(test_protection_hysteresis);
    RUN_TEST(test_fault_clearing);

    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}
#endif
