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

static void updateHighestCell(long voltageMv, unsigned long elapsedMs = 1000) {
    g_mockMillis += elapsedMs;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = voltageMv;
    g_bmsState.updatePackStatistics();
    socUpdate();
}

void test_soc_filter_rejects_brief_sag_then_accepts_sustained_sag() {
    updateHighestCell(4020, 0);
    TEST_ASSERT_EQUAL_INT(99, g_bmsState.soc);
    for (int i = 0; i < 7; ++i) updateHighestCell(3490);
    TEST_ASSERT_EQUAL_INT(99, g_bmsState.soc);
    updateHighestCell(3490);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT32(3490, socFilteredCellMv());
}

void test_soc_filter_limits_upward_recovery() {
    updateHighestCell(4020, 0);
    for (int i = 0; i < 8; ++i) updateHighestCell(3490);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);

    for (int i = 0; i < 8; ++i) updateHighestCell(4020);
    TEST_ASSERT_EQUAL_INT(10, g_bmsState.soc);
    for (int i = 0; i < 29; ++i) updateHighestCell(4020);
    TEST_ASSERT_EQUAL_INT(11, g_bmsState.soc);
    for (int i = 0; i < 30; ++i) updateHighestCell(4020);
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
    updateHighestCell(4020, 0);
    for (int i = 0; i < 8; ++i) updateHighestCell(3490);
    for (int i = 0; i < 8; ++i) updateHighestCell(4020);
    for (int i = 0; i < 30; ++i) updateHighestCell(4020);
    TEST_ASSERT_EQUAL_INT(11, g_bmsState.soc);
}

void test_soc_uses_highest_selected_cell() {
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].voltages[0] = 3490;
    g_bmsState.modules[0].voltages[1] = 3625;
    g_bmsState.updatePackStatistics();
    socUpdate();
    TEST_ASSERT_EQUAL_INT(13, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT(13, socCalculateFromVoltage());
    TEST_ASSERT_EQUAL_INT32(3625, socFilteredCellMv());
}

void test_soc_full_requires_live_highest_cell_at_4200() {
    const SocCurvePoint curve[] = {{3200, 0}, {4010, 90}, {4200, 100}};
    TEST_ASSERT_TRUE(settingsSetSocCurvePoints(curve, 3));
    updateHighestCell(4199, 0);
    TEST_ASSERT_EQUAL_INT(99, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT(99, socCalculateFromVoltage());
    updateHighestCell(4200, 1);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT(100, socUnfilteredPercent());
    // Even before the next filter sample, a fall below full must clear 100%.
    updateHighestCell(4199, 1);
    TEST_ASSERT_EQUAL_INT(99, g_bmsState.soc);
    for (int i = 0; i < 15; ++i) updateHighestCell(4200);
    TEST_ASSERT_EQUAL_INT32(4200, socFilteredCellMv());
    updateHighestCell(4199);
    TEST_ASSERT_EQUAL_INT(99, g_bmsState.soc);
    TEST_ASSERT_EQUAL_INT32(4200, socFilteredCellMv());
    updateHighestCell(4201);
    TEST_ASSERT_EQUAL_INT(100, g_bmsState.soc);
}
