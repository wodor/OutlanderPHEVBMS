/**
 * @file test_main.cpp
 * @brief Main entry point for native unit tests
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/protection.h"
#include "../src/soc_calc.h"
#include <Preferences.h>

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

// Test functions from test_bms_data.cpp
void test_pack_statistics_voltages();
void test_pack_statistics_excludes_unselected_modules();
void test_pack_statistics_temperatures();
void test_pack_statistics_invalid_temps();
void test_pack_statistics_no_modules();
void test_pack_statistics_zero_voltages();
void test_pack_statistics_odd_median();
void test_module_voltage_requires_all_cells();
void test_has_any_data();
void test_get_pack_voltage_parallel_strings();
void test_settings_defaults();
void test_settings_soc_curve_save_reload();
void test_settings_load_migrates_historical_soc_high_voltage_endpoints();
void test_settings_load_preserves_current_and_emergency_soc_high_voltage_endpoints();
void test_soc_curve_validation_accepts_exact_ordered_values();
void test_soc_curve_validation_rejects_invalid_without_mutation();
void test_soc_curve_points_parse_interpolate_and_format();
void test_soc_curve_points_reject_invalid_without_mutation();
void test_settings_soc_curve_points_save_reload_and_corruption_fallback();
void test_cmu_data_init();

// Test functions from test_soc_calc.cpp
void test_soc_update_uses_piecewise_voltage_curve();
void test_soc_no_data_defaults_to_zero();
void test_soc_filter_rejects_brief_sag_then_accepts_sustained_sag();
void test_soc_filter_limits_upward_recovery();
void test_soc_curve_matches_recorded_discharge_energy();
void test_soc_filter_upward_timer_handles_millis_rollover();

// Test functions from test_protection.cpp
void test_safety_output_defaults_high();
void test_safety_output_drops_for_overtemperature();
void test_safety_output_drops_after_ten_seconds_without_can();
void test_safety_output_drops_for_stale_selected_cmu();
void test_voltage_and_cold_temperature_do_not_drop_safety_output();
void test_safety_output_drops_at_emergency_cell_voltage_limits();
void test_supervised_override_lifts_voltage_trip_but_not_can_stop();
void test_supervised_override_expires_after_ten_minutes();

// Test functions from test_safety_critical.cpp
void test_module_array_bounds();
void test_cell_array_bounds();
void test_temperature_array_bounds();

// Unity setUp/tearDown - called before/after each test
extern unsigned long g_mockMillis;

void setUp(void) {
    Preferences::clearAll();
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
    socResetFilter();
    protectionInit();
    g_mockMillis = 0;
}

void tearDown(void) {
    protectionClearFaults();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    // BMS Data tests
    RUN_TEST(test_pack_statistics_voltages);
    RUN_TEST(test_pack_statistics_excludes_unselected_modules);
    RUN_TEST(test_pack_statistics_temperatures);
    RUN_TEST(test_pack_statistics_invalid_temps);
    RUN_TEST(test_pack_statistics_no_modules);
    RUN_TEST(test_pack_statistics_zero_voltages);
    RUN_TEST(test_pack_statistics_odd_median);
    RUN_TEST(test_module_voltage_requires_all_cells);
    RUN_TEST(test_has_any_data);
    RUN_TEST(test_get_pack_voltage_parallel_strings);
    RUN_TEST(test_settings_defaults);
    RUN_TEST(test_settings_soc_curve_save_reload);
    RUN_TEST(test_settings_load_migrates_historical_soc_high_voltage_endpoints);
    RUN_TEST(test_settings_load_preserves_current_and_emergency_soc_high_voltage_endpoints);
    RUN_TEST(test_soc_curve_validation_accepts_exact_ordered_values);
    RUN_TEST(test_soc_curve_validation_rejects_invalid_without_mutation);
    RUN_TEST(test_soc_curve_points_parse_interpolate_and_format);
    RUN_TEST(test_soc_curve_points_reject_invalid_without_mutation);
    RUN_TEST(test_settings_soc_curve_points_save_reload_and_corruption_fallback);
    RUN_TEST(test_cmu_data_init);

    // SOC calculation tests
    RUN_TEST(test_soc_update_uses_piecewise_voltage_curve);
    RUN_TEST(test_soc_no_data_defaults_to_zero);
    RUN_TEST(test_soc_filter_rejects_brief_sag_then_accepts_sustained_sag);
    RUN_TEST(test_soc_filter_limits_upward_recovery);
    RUN_TEST(test_soc_curve_matches_recorded_discharge_energy);
    RUN_TEST(test_soc_filter_upward_timer_handles_millis_rollover);

    // Protection tests
    RUN_TEST(test_safety_output_defaults_high);
    RUN_TEST(test_safety_output_drops_for_overtemperature);
    RUN_TEST(test_safety_output_drops_after_ten_seconds_without_can);
    RUN_TEST(test_safety_output_drops_for_stale_selected_cmu);
    RUN_TEST(test_voltage_and_cold_temperature_do_not_drop_safety_output);
    RUN_TEST(test_safety_output_drops_at_emergency_cell_voltage_limits);
    RUN_TEST(test_supervised_override_lifts_voltage_trip_but_not_can_stop);
    RUN_TEST(test_supervised_override_expires_after_ten_minutes);

    // Safety critical tests
    RUN_TEST(test_module_array_bounds);
    RUN_TEST(test_cell_array_bounds);
    RUN_TEST(test_temperature_array_bounds);

    return UNITY_END();
}
