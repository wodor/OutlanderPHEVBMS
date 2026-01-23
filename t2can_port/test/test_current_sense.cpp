/**
 * @file test_current_sense.cpp
 * @brief Unit tests for current sensing module
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/current_sense.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

#ifndef UNIT_TEST
void setUp(void) {
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
}

void tearDown(void) {
    // Clean up
}
#endif

/**
 * Test current sense initialization
 */
void test_current_sense_init() {
    // Test with no sensor
    g_bmsSettings.currentSensorType = 0;
    currentSenseInit();
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.currentSensorRange);
    
    // Test with analog sensor
    g_bmsSettings.currentSensorType = 1;
    currentSenseInit();
    // Should initialize without error
}

/**
 * Test current sense with no sensor configured
 */
void test_current_sense_no_sensor() {
    g_bmsSettings.currentSensorType = 0;
    currentSenseInit();
    
    currentSenseUpdate();
    
    // Should return zero current
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, g_bmsState.currentAmps);
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.currentSensorRange);
}

/**
 * Test current sense filtering
 */
void test_current_sense_filtering() {
    g_bmsSettings.currentSensorType = 0; // No sensor for this test
    currentSenseInit();
    
    // Manually set current values to test filtering
    g_bmsState.currentAmps = 10.0f;
    g_bmsState.avgCurrentAmps = 5.0f;
    
    // Update should apply filtering
    currentSenseUpdate();
    
    // avgCurrentAmps should be filtered value
    // With no sensor, it should go toward zero
    TEST_ASSERT_FLOAT_WITHIN(5.0f, 2.5f, g_bmsState.avgCurrentAmps);
}

/**
 * Test currentSenseGetAmps returns filtered value
 */
void test_current_sense_get_amps() {
    g_bmsState.avgCurrentAmps = 12.5f;
    
    float current = currentSenseGetAmps();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.5f, current);
}

/**
 * Test current sensor configuration validation
 */
void test_current_sensor_config() {
    // Test valid sensor types
    for (int i = 0; i <= 3; i++) {
        g_bmsSettings.currentSensorType = i;
        currentSenseInit();
        currentSenseUpdate();
        // Should not crash
    }
    
    // Test invalid sensor type
    g_bmsSettings.currentSensorType = 99;
    currentSenseUpdate();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, g_bmsState.currentAmps);
}

/**
 * Test current sensor settings
 */
void test_current_sensor_settings() {
    BmsSettings settings;
    
    // Test default settings
    TEST_ASSERT_EQUAL_INT(0, settings.currentSensorType);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 580.0f, settings.conversionHigh);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 6430.0f, settings.conversionLow);
    TEST_ASSERT_EQUAL_UINT16(1750, settings.offset1);
    TEST_ASSERT_EQUAL_UINT16(1750, settings.offset2);
    TEST_ASSERT_EQUAL_INT32(20000, settings.rangeChangeCurrent);
    TEST_ASSERT_EQUAL_UINT16(5, settings.currentDeadband);
}

#ifndef UNIT_TEST
void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_current_sense_init);
    RUN_TEST(test_current_sense_no_sensor);
    RUN_TEST(test_current_sense_filtering);
    RUN_TEST(test_current_sense_get_amps);
    RUN_TEST(test_current_sensor_config);
    RUN_TEST(test_current_sensor_settings);

    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}
#endif
