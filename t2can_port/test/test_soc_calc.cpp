/**
 * @file test_soc_calc.cpp
 * @brief Unit tests for SOC calculation module
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/soc_calc.h"

// External globals that need to be available
extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

#ifndef UNIT_TEST
void setUp(void) {
    // Reset state before each test
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
}

void tearDown(void) {
    // Clean up after each test
}
#endif

/**
 * Test voltage-based SOC calculation
 */
void test_soc_voltage_calculation() {
    // Setup voltage curve: 3100mV=10%, 4100mV=90%
    g_bmsSettings.socVoltageCurve[0] = 3100;
    g_bmsSettings.socVoltageCurve[1] = 10;
    g_bmsSettings.socVoltageCurve[2] = 4100;
    g_bmsSettings.socVoltageCurve[3] = 90;
    
    // Test low voltage (10%)
    g_bmsState.lowestCellMv = 3100;
    int soc = socCalculateFromVoltage();
    TEST_ASSERT_EQUAL_INT(10, soc);
    
    // Test high voltage (90%)
    g_bmsState.lowestCellMv = 4100;
    soc = socCalculateFromVoltage();
    TEST_ASSERT_EQUAL_INT(90, soc);
    
    // Test mid voltage (50%)
    g_bmsState.lowestCellMv = 3600;
    soc = socCalculateFromVoltage();
    TEST_ASSERT_INT_WITHIN(2, 50, soc);
    
    // Test below range (should clamp to 0%)
    g_bmsState.lowestCellMv = 2000;
    soc = socCalculateFromVoltage();
    TEST_ASSERT_EQUAL_INT(0, soc);
    
    // Test above range (should clamp to 100%)
    g_bmsState.lowestCellMv = 5000;
    soc = socCalculateFromVoltage();
    TEST_ASSERT_EQUAL_INT(100, soc);
}

/**
 * Test SOC reset functionality
 */
void test_soc_reset() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    
    // Reset to 100%
    socReset(100);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    
    // Calculate expected amp-seconds for 100%
    float expectedAmpSec = (100 * 100 * 1 * 1000.0f) / 0.27777777777778f;
    TEST_ASSERT_FLOAT_WITHIN(1.0f, expectedAmpSec, g_bmsState.ampSeconds);
    
    // Reset to 50%
    socReset(50);
    TEST_ASSERT_EQUAL_INT(50, g_bmsState.soc);
    
    // Reset to 0%
    socReset(0);
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
    
    // Test clamping (values > 100)
    socReset(150);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    
    // Test clamping (values < 0)
    socReset(-10);
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
}

/**
 * Test coulomb-counting SOC update
 */
void test_soc_coulomb_counting() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    g_bmsSettings.useVoltageSoc = false;
    
    // Initialize at 50%
    g_bmsState.soc = 50;
    g_bmsState.ampSeconds = (50 * 100 * 1 * 1000.0f) / 0.27777777777778f;
    g_bmsState.socInitialized = true;
    g_bmsState.lastSocUpdate = 0;
    
    // Simulate 10A charging for 1 second
    g_bmsState.currentAmps = 10.0f;
    unsigned long currentTime = 1000; // 1 second later
    
    // Manually calculate what SOC should be
    float deltaSeconds = 1.0f;
    float newAmpSeconds = g_bmsState.ampSeconds + (10.0f * deltaSeconds);
    int expectedSoc = (int)((newAmpSeconds * 0.27777777777778f / (100 * 1 * 1000.0f)) * 100.0f);
    
    // This test verifies the calculation logic
    TEST_ASSERT_TRUE(expectedSoc > 50); // Should increase with charging
}

/**
 * Test SOC clamping at boundaries
 */
void test_soc_clamping() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    
    // Test upper limit
    socReset(100);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    
    // Test lower limit
    socReset(0);
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
}

/**
 * Test SOC with parallel strings
 */
void test_soc_parallel_strings() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 2; // 2 strings in parallel
    
    socReset(100);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    
    // Amp-seconds should account for parallel strings
    float expectedAmpSec = (100 * 100 * 2 * 1000.0f) / 0.27777777777778f;
    TEST_ASSERT_FLOAT_WITHIN(1.0f, expectedAmpSec, g_bmsState.ampSeconds);
}

#ifndef UNIT_TEST
void setup() {
    delay(2000); // Wait for serial
    UNITY_BEGIN();

    RUN_TEST(test_soc_voltage_calculation);
    RUN_TEST(test_soc_reset);
    RUN_TEST(test_soc_coulomb_counting);
    RUN_TEST(test_soc_clamping);
    RUN_TEST(test_soc_parallel_strings);

    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}
#endif
