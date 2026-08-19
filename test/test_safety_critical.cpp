/**
 * @file test_safety_critical.cpp
 * @brief Guardrail tests retained for pack-statistics boundaries.
 */

#include <unity.h>
#include "../src/bms_data.h"

extern BmsState g_bmsState;

void test_module_array_bounds() {
    TEST_ASSERT_EQUAL_INT(20, BMS_MODULE_COUNT);
    for (int module = 0; module < BMS_MODULE_COUNT; ++module) g_bmsState.modules[module].present = true;
}

void test_cell_array_bounds() {
    TEST_ASSERT_EQUAL_INT(8, CELLS_PER_MODULE);
    for (int cell = 0; cell < CELLS_PER_MODULE; ++cell) g_bmsState.modules[0].voltages[cell] = 3600;
    TEST_ASSERT_EQUAL_INT32(28800, g_bmsState.modules[0].getModuleVoltageMv());
}

void test_temperature_array_bounds() {
    TEST_ASSERT_EQUAL_INT(3, TEMPS_PER_MODULE);
    for (int sensor = 0; sensor < TEMPS_PER_MODULE; ++sensor) g_bmsState.modules[0].temperatures[sensor] = 25000;
    g_bmsState.modules[0].present = true;
    g_bmsState.updatePackStatistics();
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25.0f, g_bmsState.highestTemp);
}
