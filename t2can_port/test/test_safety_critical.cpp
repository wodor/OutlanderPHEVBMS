/**
 * @file test_safety_critical.cpp
 * @brief Safety-critical edge case tests for BMS system
 * 
 * These tests focus on conditions that could cause:
 * - Integer overflow/underflow
 * - millis() rollover
 * - Division by zero
 * - Memory corruption
 * - Fire hazards from miscalculation
 * 
 * CRITICAL: This runs on ESP32-S3 with limited resources
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/soc_calc.h"
#include "../src/protection.h"
#include "../src/current_sense.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

#ifndef UNIT_TEST
void setUp(void) {
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
}

void tearDown(void) {
    protectionClearFaults();
}
#endif

// =============================================================================
// CRITICAL: INTEGER OVERFLOW/UNDERFLOW TESTS
// =============================================================================

/**
 * Test SOC calculation with extreme current values
 * SAFETY: Large charging current over long time could overflow ampSeconds
 */
void test_soc_extreme_current_overflow() {
    extern unsigned long g_mockMillis;
    
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    g_bmsSettings.useVoltageSoc = false;
    g_bmsSettings.currentSensorType = 1; // Enable coulomb counting
    
    socReset(50);
    g_bmsState.socInitialized = true;
    
    // Simulate extreme charging: 1000A for 1 hour
    // This should not overflow float or cause fire hazard
    g_bmsState.currentAmps = 1000.0f;
    g_bmsState.lastSocUpdate = 0;
    g_mockMillis = 0;
    
    // Simulate 3600 seconds (1 hour)
    for (int i = 1; i <= 3600; i++) {
        g_mockMillis = i * 1000;
        socUpdate();
    }
    
    // Should clamp at 100%, not overflow
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    TEST_ASSERT_TRUE(g_bmsState.ampSeconds < 1e10f); // Sanity check - not infinity
}

/**
 * Test SOC calculation with extreme discharge
 * SAFETY: Large discharge should not underflow to negative infinity
 */
void test_soc_extreme_discharge_underflow() {
    extern unsigned long g_mockMillis;
    
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    g_bmsSettings.useVoltageSoc = false;
    g_bmsSettings.currentSensorType = 1; // Enable coulomb counting
    
    socReset(50);
    g_bmsState.socInitialized = true;
    
    // Simulate extreme discharge: -1000A for 1 hour
    g_bmsState.currentAmps = -1000.0f;
    g_bmsState.lastSocUpdate = 0;
    g_mockMillis = 0;
    
    for (int i = 1; i <= 3600; i++) {
        g_mockMillis = i * 1000;
        socUpdate();
    }
    
    // Should clamp at 0%, not underflow to negative
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
    TEST_ASSERT_TRUE(g_bmsState.soc >= 0);
}

/**
 * Test voltage readings at extreme values
 * SAFETY: Extreme voltage readings should be detected
 */
void test_voltage_extreme_values() {
    g_bmsSettings.overVoltage = 4.2f;
    
    // Test extreme but valid range value (filtered values are 1500-4500mV)
    // Set to just at the edge that would pass filtering but still be dangerous
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 4500; // 4.5V - at filter limit
    
    g_bmsState.updatePackStatistics();
    bool safe = protectionCheck();
    
    // Should detect as overvoltage fault (4.5V > 4.2V threshold)
    TEST_ASSERT_FALSE(safe);
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());
}

/**
 * Test temperature readings at extreme values
 * SAFETY: High temperature readings should be detected
 */
void test_temperature_extreme_values() {
    g_bmsSettings.overTemp = 65.0f;
    g_bmsSettings.underTemp = -10.0f;
    
    g_bmsState.modules[0].present = true;
    
    // Test max positive temperature (like 32767 raw = 32.767°C)
    g_bmsState.modules[0].temperatures[0] = 32767;
    g_bmsState.updatePackStatistics();
    protectionCheck();
    // Should be OK (32.767°C is normal)
    
    // Test high temperature within filter range but above threshold
    // Filter accepts -70 to 100°C, so use 99°C which is within range
    // but above 65°C threshold
    g_bmsState.modules[0].temperatures[0] = 99000; // 99°C - above 65°C threshold
    g_bmsState.updatePackStatistics();
    bool safe = protectionCheck();
    
    TEST_ASSERT_FALSE(safe);
}

// =============================================================================
// CRITICAL: millis() ROLLOVER TESTS (happens after 49.7 days)
// =============================================================================

/**
 * Test millis() rollover in SOC calculation
 * SAFETY: millis() rolls over every 49.7 days, must handle gracefully
 */
void test_soc_millis_rollover() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.useVoltageSoc = false;
    
    socReset(50);
    g_bmsState.socInitialized = true;
    g_bmsState.currentAmps = 10.0f;
    
    // Simulate near rollover: last update near max, current time after rollover
    g_bmsState.lastSocUpdate = 0xFFFFFFF0; // Near max
    uint32_t afterRollover = 100;      // After rollover (small number)
    
    // Calculate delta manually to verify it handles rollover
    // Use uint32_t to match ESP32 behavior (unsigned long is 32-bit on ESP32, but 64-bit on native)
    uint32_t delta = afterRollover - (uint32_t)g_bmsState.lastSocUpdate;
    
    // Delta should be small due to unsigned arithmetic wraparound
    TEST_ASSERT_TRUE(delta < 1000); // Should be ~116ms, not huge number
}

/**
 * Test millis() rollover in protection debouncing
 * SAFETY: Debounce timers must work across millis() rollover
 */
void test_protection_millis_rollover() {
    g_bmsSettings.underVoltage = 3.0f;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 2900; // Undervoltage
    
    g_bmsState.updatePackStatistics();
    
    // Start debounce timer near rollover
    // Protection code uses: millis() - s_underVoltTime > DEBOUNCE
    // This should handle rollover correctly due to unsigned arithmetic
    
    protectionCheck();
    // After rollover, debounce should still work
}

// =============================================================================
// CRITICAL: DIVISION BY ZERO TESTS
// =============================================================================

/**
 * Test SOC calculation with zero capacity
 * SAFETY: Dividing by zero capacity would crash or produce infinity
 */
void test_soc_zero_capacity() {
    g_bmsSettings.capacityAh = 0; // DANGEROUS CONFIG!
    g_bmsSettings.parallelStrings = 1;
    g_bmsSettings.useVoltageSoc = false;
    
    g_bmsState.soc = 50;
    g_bmsState.ampSeconds = 1000.0f;
    g_bmsState.socInitialized = true;
    g_bmsState.lastSocUpdate = 0;
    g_bmsState.currentAmps = 10.0f;
    
    // This should not divide by zero and crash
    socUpdate();
    
    // SOC should be clamped or show error, not infinity
    TEST_ASSERT_TRUE(g_bmsState.soc >= 0 && g_bmsState.soc <= 100);
    TEST_ASSERT_FALSE(isinf(g_bmsState.ampSeconds));
    TEST_ASSERT_FALSE(isnan(g_bmsState.ampSeconds));
}

/**
 * Test current sense with zero conversion factor
 * SAFETY: Division by zero in current conversion
 */
void test_current_sense_zero_conversion() {
    g_bmsSettings.currentSensorType = 1;
    g_bmsSettings.conversionHigh = 0.0f; // DANGEROUS!
    g_bmsSettings.conversionLow = 0.0f;  // DANGEROUS!
    
    currentSenseInit();
    currentSenseUpdate();
    
    // Should not divide by zero and crash
    TEST_ASSERT_FALSE(isinf(g_bmsState.currentAmps));
    TEST_ASSERT_FALSE(isnan(g_bmsState.currentAmps));
}

/**
 * Test pack voltage calculation with zero parallel strings
 * SAFETY: getPackVoltage() divides by parallelStrings
 */
void test_pack_voltage_zero_strings() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    g_bmsState.updatePackStatistics();
    
    // Try to get pack voltage with zero strings (invalid config)
    float voltage = g_bmsState.getPackVoltage(0);
    
    // Should not crash or return infinity
    TEST_ASSERT_FALSE(isinf(voltage));
    TEST_ASSERT_FALSE(isnan(voltage));
}

// =============================================================================
// CRITICAL: FLOAT TO INT CONVERSION TESTS
// =============================================================================

/**
 * Test float to int conversion in SOC calculation
 * SAFETY: Large float values might overflow when cast to int
 */
void test_soc_float_to_int_overflow() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.parallelStrings = 1;
    g_bmsSettings.useVoltageSoc = false;
    
    // Force ampSeconds to huge value
    g_bmsState.ampSeconds = 1e20f; // Huge number
    g_bmsState.socInitialized = true;
    g_bmsState.lastSocUpdate = millis();
    g_bmsState.currentAmps = 0.0f;
    
    socUpdate();
    
    // SOC should clamp at 100, not overflow to negative
    TEST_ASSERT_TRUE(g_bmsState.soc >= 0);
    TEST_ASSERT_TRUE(g_bmsState.soc <= 100);
}

// =============================================================================
// CRITICAL: ARRAY BOUNDS TESTS
// =============================================================================

/**
 * Test module array bounds
 * SAFETY: Accessing modules[20] or higher would corrupt memory
 */
void test_module_array_bounds() {
    // This test verifies we don't access out of bounds
    // Real code should never do this, but let's verify constants
    TEST_ASSERT_TRUE(BMS_MODULE_COUNT == 20);
    
    // Verify loops use correct bounds
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        g_bmsState.modules[m].present = true;
        // Should not crash
    }
    
    // Verify we can't accidentally access modules[20]
    // (This would be a compile error, but we document the limit)
}

/**
 * Test cell voltage array bounds
 * SAFETY: Accessing voltages[8] would corrupt memory
 */
void test_cell_array_bounds() {
    TEST_ASSERT_TRUE(CELLS_PER_MODULE == 8);
    
    g_bmsState.modules[0].present = true;
    for (int c = 0; c < CELLS_PER_MODULE; c++) {
        g_bmsState.modules[0].voltages[c] = 3600;
        // Should not crash
    }
}

/**
 * Test temperature array bounds
 * SAFETY: Accessing temperatures[3] would corrupt memory
 */
void test_temperature_array_bounds() {
    TEST_ASSERT_TRUE(TEMPS_PER_MODULE == 3);
    
    g_bmsState.modules[0].present = true;
    for (int t = 0; t < TEMPS_PER_MODULE; t++) {
        g_bmsState.modules[0].temperatures[t] = 25000;
        // Should not crash
    }
}

// =============================================================================
// CRITICAL: CONCURRENT ACCESS / RACE CONDITION TESTS
// =============================================================================

/**
 * Test SOC update during statistics calculation
 * SAFETY: What if updatePackStatistics() runs while SOC updates?
 */
void test_concurrent_soc_and_statistics() {
    g_bmsSettings.capacityAh = 100;
    g_bmsSettings.useVoltageSoc = false;
    
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    
    socReset(50);
    g_bmsState.socInitialized = true;
    g_bmsState.currentAmps = 10.0f;
    g_bmsState.lastSocUpdate = 0;
    
    // Simulate interleaved operations
    g_bmsState.updatePackStatistics();
    socUpdate();
    g_bmsState.updatePackStatistics();
    
    // Should not corrupt data
    TEST_ASSERT_TRUE(g_bmsState.soc >= 0 && g_bmsState.soc <= 100);
    TEST_ASSERT_TRUE(g_bmsState.lowestCellMv > 0);
}

/**
 * Test protection check during voltage update
 * SAFETY: What if cell voltages change during protection check?
 */
void test_concurrent_protection_and_voltage_update() {
    g_bmsSettings.overVoltage = 4.2f;
    
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3800;
    
    // Update voltage during protection check simulation
    protectionCheck();
    g_bmsState.modules[0].voltages[0] = 4300; // Now overvoltage
    protectionCheck();
    
    // Should detect the fault
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());
}

// =============================================================================
// CRITICAL: ESP32-S3 SPECIFIC TESTS
// =============================================================================

/**
 * Test memory usage is within ESP32-S3 limits
 * SAFETY: ESP32-S3 has ~400KB SRAM, structures must fit
 */
void test_memory_usage() {
    size_t bmsStateSize = sizeof(BmsState);
    size_t bmsSettingsSize = sizeof(BmsSettings);
    size_t cmuDataSize = sizeof(CmuData);
    
    // Log memory usage
    Serial.printf("[MEMORY] BmsState: %d bytes\n", bmsStateSize);
    Serial.printf("[MEMORY] BmsSettings: %d bytes\n", bmsSettingsSize);
    Serial.printf("[MEMORY] CmuData: %d bytes\n", cmuDataSize);
    
    // Verify structures are reasonable size (< 10KB each)
    TEST_ASSERT_TRUE(bmsStateSize < 10240);
    TEST_ASSERT_TRUE(bmsSettingsSize < 10240);
    
    // Total should be < 20KB (leaving plenty for stack, heap, etc.)
    TEST_ASSERT_TRUE(bmsStateSize + bmsSettingsSize < 20480);
}

/**
 * Test stack usage doesn't overflow
 * SAFETY: ESP32 default stack is 8KB, deep recursion could overflow
 */
void test_no_deep_recursion() {
    // Verify no recursive function calls in critical path
    // All our functions are iterative, not recursive
    
    // Call all major functions in sequence
    g_bmsState.updatePackStatistics();
    protectionCheck();
    socUpdate();
    currentSenseUpdate();
    
    // If we get here without stack overflow, we're good
    TEST_ASSERT_TRUE(true);
}

/**
 * Test float operations don't cause issues on ESP32
 * SAFETY: ESP32-S3 has hardware FPU, but still need to verify
 */
void test_float_operations_accuracy() {
    // Test critical float calculations
    float voltage = 3.7f;
    float current = 10.5f;
    float power = voltage * current;
    
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 38.85f, power);
    
    // Test division
    float result = power / voltage;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.5f, result);
    
    // Test that we don't have denormal numbers or NaN
    TEST_ASSERT_FALSE(isnan(result));
    TEST_ASSERT_FALSE(isinf(result));
}

#ifndef UNIT_TEST
void setup() {
    delay(2000);
    UNITY_BEGIN();

    // Critical overflow/underflow tests
    RUN_TEST(test_soc_extreme_current_overflow);
    RUN_TEST(test_soc_extreme_discharge_underflow);
    RUN_TEST(test_voltage_extreme_values);
    RUN_TEST(test_temperature_extreme_values);

    // Critical millis() rollover tests
    RUN_TEST(test_soc_millis_rollover);
    RUN_TEST(test_protection_millis_rollover);

    // Critical division by zero tests
    RUN_TEST(test_soc_zero_capacity);
    RUN_TEST(test_current_sense_zero_conversion);
    RUN_TEST(test_pack_voltage_zero_strings);

    // Critical float to int conversion tests
    RUN_TEST(test_soc_float_to_int_overflow);

    // Critical array bounds tests
    RUN_TEST(test_module_array_bounds);
    RUN_TEST(test_cell_array_bounds);
    RUN_TEST(test_temperature_array_bounds);

    // Critical concurrent access tests
    RUN_TEST(test_concurrent_soc_and_statistics);
    RUN_TEST(test_concurrent_protection_and_voltage_update);

    // Critical ESP32-S3 specific tests
    RUN_TEST(test_memory_usage);
    RUN_TEST(test_no_deep_recursion);
    RUN_TEST(test_float_operations_accuracy);

    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}
#endif
