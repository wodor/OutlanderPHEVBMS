/**
 * @file test_bms_data.cpp
 * @brief Unit tests for BMS data structures and pack statistics
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/soc_curve_validation.h"
#include <Preferences.h>

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
    TEST_ASSERT_EQUAL_INT32(3662, g_bmsState.medianCellMv);
    TEST_ASSERT_EQUAL_INT32(250, g_bmsState.cellVoltageDeltaMv);
    
    // Verify pack voltage (sum of all valid cells)
    float expectedPackVoltage = (3600 + 3700 + 3650 + 3550 + 3800 + 3675) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedPackVoltage, g_bmsState.packVoltage);
    
    // Verify average cell voltage
    float expectedAvg = expectedPackVoltage / 6.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedAvg, g_bmsState.avgCellVoltage);
}

void test_pack_statistics_excludes_unselected_modules() {
    g_bmsSettings.expectedCmusA = 0x001;
    g_bmsSettings.expectedCmusB = 0x000;

    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    g_bmsState.modules[1].present = true;
    g_bmsState.modules[1].voltages[0] = 4200;

    g_bmsState.updatePackStatistics();

    TEST_ASSERT_TRUE(g_bmsState.modules[1].present);
    TEST_ASSERT_EQUAL_INT32(3600, g_bmsState.lowestCellMv);
    TEST_ASSERT_EQUAL_INT32(3600, g_bmsState.highestCellMv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.6f, g_bmsState.packVoltage);
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
    TEST_ASSERT_EQUAL_INT32(0, g_bmsState.medianCellMv);
    TEST_ASSERT_EQUAL_INT32(0, g_bmsState.cellVoltageDeltaMv);
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
    TEST_ASSERT_EQUAL_INT32(3625, g_bmsState.medianCellMv);
    TEST_ASSERT_EQUAL_INT32(50, g_bmsState.cellVoltageDeltaMv);
    
    // Pack voltage should be sum of valid cells only
    float expectedPackVoltage = (3600 + 3650) / 1000.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, expectedPackVoltage, g_bmsState.packVoltage);
}

/**
 * Test median calculation with an odd number of valid cells.
 */
void test_pack_statistics_odd_median() {
    g_bmsState.modules[0].present = true;
    const long voltages[] = {4100, 3500, 3900, 3700, 3600};
    for (int i = 0; i < 5; i++) {
        g_bmsState.modules[0].voltages[i] = voltages[i];
    }

    g_bmsState.updatePackStatistics();

    TEST_ASSERT_EQUAL_INT32(3700, g_bmsState.medianCellMv);
    TEST_ASSERT_EQUAL_INT32(600, g_bmsState.cellVoltageDeltaMv);
}

/**
 * A module voltage is valid only when all eight constituent cells are valid.
 */
void test_module_voltage_requires_all_cells() {
    CmuData cmu;
    long expectedTotalMv = 0;
    for (int c = 0; c < CELLS_PER_MODULE; c++) {
        cmu.voltages[c] = 3900 + c;
        expectedTotalMv += cmu.voltages[c];
    }

    TEST_ASSERT_EQUAL_INT32(expectedTotalMv, cmu.getModuleVoltageMv());

    cmu.voltages[4] = 0;
    TEST_ASSERT_EQUAL_INT32(0, cmu.getModuleVoltageMv());
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
    
    // Critical temperature limit
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 65.0f, settings.overTemp);
    
    // SOC curve defaults
    TEST_ASSERT_EQUAL_INT(3200, settings.socVoltageCurve[0]);
    TEST_ASSERT_EQUAL_INT(0, settings.socVoltageCurve[1]);
    TEST_ASSERT_EQUAL_INT(4050, settings.socVoltageCurve[2]);
    TEST_ASSERT_EQUAL_INT(100, settings.socVoltageCurve[3]);
    TEST_ASSERT_EQUAL_INT(12, settings.socCurvePointCount);
    TEST_ASSERT_EQUAL_INT(3490, settings.socCurvePoints[1].voltageMv);
    TEST_ASSERT_EQUAL_INT(10, settings.socCurvePoints[1].socPercent);
    TEST_ASSERT_TRUE(settings.useVoltageSoc);
}

void test_settings_soc_curve_save_reload() {
    Preferences::clearAll();
    g_bmsSettings.socVoltageCurve[0] = 3300;
    g_bmsSettings.socVoltageCurve[1] = 5;
    g_bmsSettings.socVoltageCurve[2] = 4200;
    g_bmsSettings.socVoltageCurve[3] = 95;
    g_bmsSettings.useVoltageSoc = false;
    settingsSave();

    g_bmsSettings = BmsSettings();
    settingsLoad();

    TEST_ASSERT_EQUAL_INT(3300, g_bmsSettings.socVoltageCurve[0]);
    TEST_ASSERT_EQUAL_INT(5, g_bmsSettings.socVoltageCurve[1]);
    TEST_ASSERT_EQUAL_INT(4200, g_bmsSettings.socVoltageCurve[2]);
    TEST_ASSERT_EQUAL_INT(95, g_bmsSettings.socVoltageCurve[3]);
    TEST_ASSERT_EQUAL_INT(2, g_bmsSettings.socCurvePointCount);
    TEST_ASSERT_FALSE(g_bmsSettings.useVoltageSoc);
}

void test_settings_load_migrates_historical_soc_high_voltage_endpoints() {
    const int legacyEndpoints[] = {4000};
    const int expectedEndpoints[] = {4050};

    for (size_t i = 0; i < sizeof(legacyEndpoints) / sizeof(legacyEndpoints[0]); ++i) {
        Preferences::clearAll();
        g_bmsSettings = BmsSettings();
        g_bmsSettings.socVoltageCurve[2] = legacyEndpoints[i];
        settingsSave();

        g_bmsSettings = BmsSettings();
        settingsLoad();
        TEST_ASSERT_EQUAL_INT(expectedEndpoints[i], g_bmsSettings.socVoltageCurve[2]);

        // The migration must survive the next boot rather than being applied
        // only in RAM.
        g_bmsSettings = BmsSettings();
        settingsLoad();
        TEST_ASSERT_EQUAL_INT(expectedEndpoints[i], g_bmsSettings.socVoltageCurve[2]);
    }
}

void test_settings_load_preserves_current_and_emergency_soc_high_voltage_endpoints() {
    const int endpoints[] = {4050, 4100, 4200};
    for (size_t i = 0; i < sizeof(endpoints) / sizeof(endpoints[0]); ++i) {
        Preferences::clearAll();
        g_bmsSettings = BmsSettings();
        g_bmsSettings.socVoltageCurve[2] = endpoints[i];
        settingsSave();

        g_bmsSettings = BmsSettings();
        settingsLoad();
        TEST_ASSERT_EQUAL_INT(endpoints[i], g_bmsSettings.socVoltageCurve[2]);
    }
}

void test_soc_curve_validation_accepts_exact_ordered_values() {
    int curve[4] = {1, 2, 3, 4};
    TEST_ASSERT_TRUE(parseSocVoltageCurve("3500,0,4100,100", curve));
    TEST_ASSERT_EQUAL_INT(3500, curve[0]);
    TEST_ASSERT_EQUAL_INT(0, curve[1]);
    TEST_ASSERT_EQUAL_INT(4100, curve[2]);
    TEST_ASSERT_EQUAL_INT(100, curve[3]);
}

void test_soc_curve_validation_rejects_invalid_without_mutation() {
    const char* invalid[] = {
        "3500,0,4100", "3500,0,4100,100,1", "3500,,4100,100",
        "3500,zero,4100,100", "1999,0,4100,100", "3500,-1,4100,100",
        "3500,0,5001,100", "3500,0,4100,101", "4100,0,3500,100",
        "3500,100,4100,0", "3500,0,3500,100", "3500,50,4100,50",
        " 3500,0,4100,100", "3500,0,4100,100 "
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        int curve[4] = {3200, 10, 4200, 90};
        TEST_ASSERT_FALSE(parseSocVoltageCurve(invalid[i], curve));
        TEST_ASSERT_EQUAL_INT(3200, curve[0]);
        TEST_ASSERT_EQUAL_INT(10, curve[1]);
        TEST_ASSERT_EQUAL_INT(4200, curve[2]);
        TEST_ASSERT_EQUAL_INT(90, curve[3]);
    }
}

void test_soc_curve_points_parse_interpolate_and_format() {
    SocCurvePoint points[SOC_CURVE_MAX_POINTS] = {};
    uint8_t count = 0;
    const char* fitted =
        "3200:0,3490:10,3580:11,3625:13,3710:18,3795:37,"
        "3840:46,3880:56,3925:68,3965:87,4020:100,4050:100";
    TEST_ASSERT_TRUE(parseSocCurvePoints(fitted, points, count));
    TEST_ASSERT_EQUAL_INT(12, count);
    TEST_ASSERT_EQUAL_INT(0, interpolateSocCurve(3100, points, count));
    TEST_ASSERT_EQUAL_INT(10, interpolateSocCurve(3490, points, count));
    TEST_ASSERT_EQUAL_INT(15, interpolateSocCurve(3675, points, count));
    TEST_ASSERT_EQUAL_INT(100, interpolateSocCurve(4030, points, count));

    char formatted[160] = {};
    TEST_ASSERT_TRUE(formatSocCurvePoints(points, count, formatted, sizeof(formatted)));
    TEST_ASSERT_EQUAL_STRING(fitted, formatted);
}

void test_soc_curve_points_reject_invalid_without_mutation() {
    const char* invalid[] = {
        "3200:0",
        "3200:0,3200:10,4050:100", "3200:0,3500:20,3600:19,4050:100",
        "1999:0,4050:100", "3200:0,5001:100", "3200:0,4050:100,",
        " 3200:0,4050:100", "3200:0,4050:100 "
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        SocCurvePoint points[SOC_CURVE_MAX_POINTS] = {{3333, 33}};
        uint8_t count = 7;
        TEST_ASSERT_FALSE(parseSocCurvePoints(invalid[i], points, count));
        TEST_ASSERT_EQUAL_INT(3333, points[0].voltageMv);
        TEST_ASSERT_EQUAL_INT(33, points[0].socPercent);
        TEST_ASSERT_EQUAL_INT(7, count);
    }
}

void test_settings_soc_curve_points_save_reload_and_corruption_fallback() {
    const SocCurvePoint custom[] = {{3200, 0}, {3500, 10}, {3900, 75}, {4050, 100}};
    TEST_ASSERT_TRUE(settingsSetSocCurvePoints(custom, 4));
    settingsSave();

    g_bmsSettings = BmsSettings();
    settingsLoad();
    TEST_ASSERT_EQUAL_INT(4, g_bmsSettings.socCurvePointCount);
    TEST_ASSERT_EQUAL_INT(75, g_bmsSettings.socCurvePoints[2].socPercent);

    Preferences prefs;
    prefs.begin("bms", false);
    const uint32_t corrupt = 0x12345678U;
    prefs.putBytes("socPts", &corrupt, sizeof(corrupt));
    prefs.end();

    g_bmsSettings = BmsSettings();
    settingsLoad();
    TEST_ASSERT_EQUAL_INT(12, g_bmsSettings.socCurvePointCount);
    TEST_ASSERT_EQUAL_INT(3490, g_bmsSettings.socCurvePoints[1].voltageMv);
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
