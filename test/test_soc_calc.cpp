/**
 * @file test_soc_calc.cpp
 * @brief Tests for voltage-only SOC.
 */

#include <unity.h>
#include <climits>
#include "../src/bms_data.h"
#include "../src/soc_calc.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

void test_soc_update_uses_piecewise_voltage_curve() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3625;
    g_bmsState.updatePackStatistics();

    socUpdate();
    TEST_ASSERT_EQUAL_INT(13, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT(13, socUnfilteredPercent());
    TEST_ASSERT_EQUAL_INT32(3625, socFilteredCellMv());
}

void test_soc_no_data_defaults_to_zero() {
    g_bmsState.soc = 50;
    socUpdate();
    TEST_ASSERT_EQUAL_INT(0, g_bmsState.soc);
}

static void updateLowestCell(long voltageMv, unsigned long elapsedMs = 1000) {
    g_mockMillis += elapsedMs;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = voltageMv;
    g_bmsState.updatePackStatistics();
    socUpdate();
}

void test_soc_filter_rejects_brief_sag_then_accepts_sustained_sag() {
    updateLowestCell(4020, 0);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    for (int i = 0; i < 7; ++i) updateLowestCell(3490);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    updateLowestCell(3490);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT32(3490, socFilteredCellMv());
}

void test_soc_filter_limits_upward_recovery() {
    updateLowestCell(4020, 0);
    for (int i = 0; i < 8; ++i) updateLowestCell(3490);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);

    for (int i = 0; i < 8; ++i) updateLowestCell(4020);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);
    for (int i = 0; i < 29; ++i) updateLowestCell(4020);
    TEST_ASSERT_EQUAL_INT(11, g_bmsState.soc);
    for (int i = 0; i < 30; ++i) updateLowestCell(4020);
    TEST_ASSERT_EQUAL_INT(12, g_bmsState.soc);
}

void test_soc_curve_matches_recorded_discharge_energy() {
    const float totalEnergyKwh = 7.78f;
    const int firstRunStart = interpolateSocCurve(
        4020, g_bmsSettings.socCurvePoints, g_bmsSettings.socCurvePointCount);
    const int firstRunEnd = interpolateSocCurve(
        3490, g_bmsSettings.socCurvePoints, g_bmsSettings.socCurvePointCount);
    const float firstRunKwh = (firstRunStart - firstRunEnd) * totalEnergyKwh / 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.4f, 7.0f, firstRunKwh);

    const int secondRunStart = interpolateSocCurve(
        3990, g_bmsSettings.socCurvePoints, g_bmsSettings.socCurvePointCount);
    const int secondRunEnd = interpolateSocCurve(
        3829, g_bmsSettings.socCurvePoints, g_bmsSettings.socCurvePointCount);
    const float secondRunKwh = (secondRunStart - secondRunEnd) * totalEnergyKwh / 100.0f;
    TEST_ASSERT_FLOAT_WITHIN(0.4f, 3.8f, secondRunKwh);
}

void test_soc_filter_upward_timer_handles_millis_rollover() {
    g_mockMillis = ULONG_MAX - 5000UL;
    updateLowestCell(4020, 0);
    for (int i = 0; i < 8; ++i) updateLowestCell(3490);
    for (int i = 0; i < 8; ++i) updateLowestCell(4020);
    for (int i = 0; i < 30; ++i) updateLowestCell(4020);
    TEST_ASSERT_EQUAL_INT(11, g_bmsState.soc);
}
