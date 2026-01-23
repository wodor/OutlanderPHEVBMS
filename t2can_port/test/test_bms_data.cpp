/**
 * @file test_bms_data.cpp
 * @brief Unit tests for BMS data structures and pack statistics
 */

#include <unity.h>
#include "../src/bms_data.h"

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

// Test-specific setup helper
static void bms_data_test_setup() {
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
}

/**
 * Test pack statistics calculation - voltages
 */
void test_pack_statistics_voltages() {
    // Setup multiple modules with different voltages
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    g_bmsState.modules[0].voltages[1] = 3700;
    g_bmsState.modules[0].voltages[2] = 3650;
    
    g_bmsState.modules[1].present = true;
    g_bmsState.modules[1].voltages[0] = 3550; // Lowest
    g_bmsState.modules[1].voltages[1] = 3800; // Highest
    g_bmsState.modules[1].voltages[2] = 3675;
    
    // Update statistics
    g_bmsState.updatePackStatistics();
    
    // Verify lowest and highest
    TEST_ASSERT_EQUAL_INT32(3550, g_bmsState.lowestCellMv);
    TEST_ASSERT_EQUAL_INT32(3800, g_bmsState.highestCellMv);
    
    // Verify pack voltage (sum of all valid cells)
    float expectedPackVoltage = (3600 + 3700 + 3650 + 3550 + 3800 + 3675) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedPackVoltage, g_bmsState.packVoltage);
    
    // Verify average cell voltage
    float expectedAvg = expectedPackVoltage / 6.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedAvg, g_bmsState.avgCellVoltage);
}

/**
 * Test pack statistics calculation - temperatures
 */
void test_pack_statistics_temperatures() {
    // Setup modules with different temperatures
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].temperatures[0] = 25000; // 25°C
    g_bmsState.modules[0].temperatures[1] = 30000; // 30°C
    g_bmsState.modules[0].temperatures[2] = 28000; // 28°C
    
    g_bmsState.modules[1].present = true;
    g_bmsState.modules[1].temperatures[0] = 20000; // 20°C - lowest
    g_bmsState.modules[1].temperatures[1] = 35000; // 35°C - highest
    g_bmsState.modules[1].temperatures[2] = 27000; // 27°C
    
    // Update statistics
    g_bmsState.updatePackStatistics();
    
    // Verify lowest and highest
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, g_bmsState.lowestTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 35.0f, g_bmsState.highestTemp);
    
    // Verify average
    float expectedAvg = (25.0f + 30.0f + 28.0f + 20.0f + 35.0f + 27.0f) / 6.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.5f, expectedAvg, g_bmsState.avgTemp);
}

/**
 * Test pack statistics ignores invalid temperatures
 */
void test_pack_statistics_invalid_temps() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].temperatures[0] = 25000;  // 25°C - valid
    g_bmsState.modules[0].temperatures[1] = -80000; // -80°C - invalid (below -70)
    g_bmsState.modules[0].temperatures[2] = 150000; // 150°C - invalid (above 100)
    
    g_bmsState.updatePackStatistics();
    
    // Should only use valid temperature
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f, g_bmsState.avgTemp);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f, g_bmsState.lowestTemp);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 25.0f, g_bmsState.highestTemp);
}

/**
 * Test pack statistics with no modules present
 */
void test_pack_statistics_no_modules() {
    // No modules marked as present
    g_bmsState.updatePackStatistics();
    
    // Should have default/initial values
    TEST_ASSERT_EQUAL_INT32(DEFAULT_LOW_CELL_MV, g_bmsState.lowestCellMv);
    TEST_ASSERT_EQUAL_INT32(0, g_bmsState.highestCellMv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, g_bmsState.packVoltage);
}

/**
 * Test pack statistics ignores zero/invalid voltages
 */
void test_pack_statistics_zero_voltages() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600; // Valid
    g_bmsState.modules[0].voltages[1] = 0;    // Invalid (zero)
    g_bmsState.modules[0].voltages[2] = 3650; // Valid
    
    g_bmsState.updatePackStatistics();
    
    // Should only count valid voltages
    TEST_ASSERT_EQUAL_INT32(3600, g_bmsState.lowestCellMv);
    TEST_ASSERT_EQUAL_INT32(3650, g_bmsState.highestCellMv);
    
    // Pack voltage should be sum of valid cells only
    float expectedPackVoltage = (3600 + 3650) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedPackVoltage, g_bmsState.packVoltage);
}

/**
 * Test hasAnyData method
 */
void test_has_any_data() {
    // Initially no data
    TEST_ASSERT_FALSE(g_bmsState.hasAnyData());
    
    // Mark one module as present
    g_bmsState.modules[0].present = true;
    TEST_ASSERT_TRUE(g_bmsState.hasAnyData());
    
    // Mark all as not present
    for (int i = 0; i < BMS_MODULE_COUNT; i++) {
        g_bmsState.modules[i].present = false;
    }
    TEST_ASSERT_FALSE(g_bmsState.hasAnyData());
}

/**
 * Test getPackVoltage with parallel strings
 */
void test_get_pack_voltage_parallel_strings() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    g_bmsState.modules[0].voltages[1] = 3600;
    
    g_bmsState.updatePackStatistics();
    
    // Pack voltage with 1 parallel string
    float voltage1 = g_bmsState.getPackVoltage(1);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 7.2f, voltage1);
    
    // Pack voltage with 2 parallel strings (divided by 2)
    float voltage2 = g_bmsState.getPackVoltage(2);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.6f, voltage2);
}

/**
 * Test BmsSettings default values
 */
void test_settings_defaults() {
    BmsSettings settings;
    
    // Voltage limits
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, settings.overVoltage);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.0f, settings.underVoltage);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.1f, settings.chargeVoltage);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.2f, settings.dischargeVoltage);
    
    // Temperature limits
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 65.0f, settings.overTemp);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -10.0f, settings.underTemp);
    
    // Battery config
    TEST_ASSERT_EQUAL_INT(12, settings.seriesCells);
    TEST_ASSERT_EQUAL_INT(1, settings.parallelStrings);
    TEST_ASSERT_EQUAL_INT(100, settings.capacityAh);
    
    // SOC curve defaults
    TEST_ASSERT_EQUAL_INT(3100, settings.socVoltageCurve[0]);
    TEST_ASSERT_EQUAL_INT(10, settings.socVoltageCurve[1]);
    TEST_ASSERT_EQUAL_INT(4100, settings.socVoltageCurve[2]);
    TEST_ASSERT_EQUAL_INT(90, settings.socVoltageCurve[3]);
}

/**
 * Test CMU data initialization
 */
void test_cmu_data_init() {
    CmuData cmu;
    
    TEST_ASSERT_FALSE(cmu.present);
    TEST_ASSERT_EQUAL_INT(0, cmu.balanceStatus);
    
    // All voltages should be zero
    for (int i = 0; i < CELLS_PER_MODULE; i++) {
        TEST_ASSERT_EQUAL_INT32(0, cmu.voltages[i]);
    }
    
    // All temperatures should be zero
    for (int i = 0; i < TEMPS_PER_MODULE; i++) {
        TEST_ASSERT_EQUAL_INT32(0, cmu.temperatures[i]);
    }
}

#ifndef UNIT_TEST
void setup() {
    delay(2000);
    UNITY_BEGIN();

    RUN_TEST(test_pack_statistics_voltages);
    RUN_TEST(test_pack_statistics_temperatures);
    RUN_TEST(test_pack_statistics_invalid_temps);
    RUN_TEST(test_pack_statistics_no_modules);
    RUN_TEST(test_pack_statistics_zero_voltages);
    RUN_TEST(test_has_any_data);
    RUN_TEST(test_get_pack_voltage_parallel_strings);
    RUN_TEST(test_settings_defaults);
    RUN_TEST(test_cmu_data_init);

    UNITY_END();
}

void loop() {
    // Tests run once in setup()
}
#endif
