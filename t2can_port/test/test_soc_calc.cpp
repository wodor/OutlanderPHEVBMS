/**
 * @file test_soc_calc.cpp
 * @brief Tests for voltage-only SOC.
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/soc_calc.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

void test_soc_update_uses_voltage_curve() {
    g_bmsSettings.socVoltageCurve[0] = 3200;
    g_bmsSettings.socVoltageCurve[1] = 0;
    g_bmsSettings.socVoltageCurve[2] = 4000;
    g_bmsSettings.socVoltageCurve[3] = 100;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3600;
    g_bmsState.updatePackStatistics();

    socUpdate();
    TEST_ASSERT_EQUAL_INT(50, g_bmsState.soc);
}

void test_soc_no_data_defaults_to_zero() {
    g_bmsState.soc = 50;
    socUpdate();
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
}
