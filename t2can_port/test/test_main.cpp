/**
 * @file test_main.cpp
 * @brief Main entry point for native unit tests
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/protection.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;

// Test functions from test_bms_data.cpp
void test_pack_statistics_voltages();
void test_pack_statistics_temperatures();
void test_pack_statistics_invalid_temps();
void test_pack_statistics_no_modules();
void test_pack_statistics_zero_voltages();
void test_has_any_data();
void test_get_pack_voltage_parallel_strings();
void test_settings_defaults();
void test_cmu_data_init();

// Test functions from test_soc_calc.cpp
void test_soc_voltage_calculation();
void test_soc_reset();
void test_soc_coulomb_counting();
void test_soc_clamping();
void test_soc_parallel_strings();
void test_soc_no_data_defaults_to_zero();

// Test functions from test_protection.cpp
void test_overvoltage_detection();
void test_undervoltage_detection();
void test_overtemperature_detection();
void test_undertemperature_detection();
void test_cell_imbalance_detection();
void test_can_charge();
void test_can_discharge();
void test_protection_hysteresis();
void test_fault_clearing();
void test_comm_fault_no_data();

// Test functions from test_current_sense.cpp
void test_current_sense_init();
void test_current_sense_no_sensor();
void test_current_sense_filtering();
void test_current_sense_get_amps();
void test_current_sensor_config();
void test_current_sensor_settings();

// Test functions from test_safety_critical.cpp
void test_soc_extreme_current_overflow();
void test_soc_extreme_discharge_underflow();
void test_voltage_extreme_values();
void test_temperature_extreme_values();
void test_soc_millis_rollover();
void test_protection_millis_rollover();
void test_soc_zero_capacity();
void test_current_sense_zero_conversion();
void test_pack_voltage_zero_strings();
void test_soc_float_to_int_overflow();
void test_module_array_bounds();
void test_cell_array_bounds();
void test_temperature_array_bounds();
void test_concurrent_soc_and_statistics();
void test_concurrent_protection_and_voltage_update();
void test_memory_usage();
void test_no_deep_recursion();
void test_float_operations_accuracy();

// Unity setUp/tearDown - called before/after each test
extern unsigned long g_mockMillis;

void setUp(void) {
    g_bmsState = BmsState();
    g_bmsSettings = BmsSettings();
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
    RUN_TEST(test_pack_statistics_temperatures);
    RUN_TEST(test_pack_statistics_invalid_temps);
    RUN_TEST(test_pack_statistics_no_modules);
    RUN_TEST(test_pack_statistics_zero_voltages);
    RUN_TEST(test_has_any_data);
    RUN_TEST(test_get_pack_voltage_parallel_strings);
    RUN_TEST(test_settings_defaults);
    RUN_TEST(test_cmu_data_init);

    // SOC calculation tests
    RUN_TEST(test_soc_voltage_calculation);
    RUN_TEST(test_soc_reset);
    RUN_TEST(test_soc_coulomb_counting);
    RUN_TEST(test_soc_clamping);
    RUN_TEST(test_soc_parallel_strings);
    RUN_TEST(test_soc_no_data_defaults_to_zero);

    // Protection tests
    RUN_TEST(test_overvoltage_detection);
    RUN_TEST(test_undervoltage_detection);
    RUN_TEST(test_overtemperature_detection);
    RUN_TEST(test_undertemperature_detection);
    RUN_TEST(test_cell_imbalance_detection);
    RUN_TEST(test_can_charge);
    RUN_TEST(test_can_discharge);
    RUN_TEST(test_protection_hysteresis);
    RUN_TEST(test_fault_clearing);
    RUN_TEST(test_comm_fault_no_data);

    // Current sense tests
    RUN_TEST(test_current_sense_init);
    RUN_TEST(test_current_sense_no_sensor);
    RUN_TEST(test_current_sense_filtering);
    RUN_TEST(test_current_sense_get_amps);
    RUN_TEST(test_current_sensor_config);
    RUN_TEST(test_current_sensor_settings);

    // Safety critical tests
    RUN_TEST(test_voltage_extreme_values);
    RUN_TEST(test_soc_extreme_current_overflow);
    RUN_TEST(test_soc_extreme_discharge_underflow);
    RUN_TEST(test_temperature_extreme_values);
    RUN_TEST(test_soc_millis_rollover);
    RUN_TEST(test_protection_millis_rollover);
    RUN_TEST(test_soc_zero_capacity);
    RUN_TEST(test_current_sense_zero_conversion);
    RUN_TEST(test_pack_voltage_zero_strings);
    RUN_TEST(test_soc_float_to_int_overflow);
    RUN_TEST(test_module_array_bounds);
    RUN_TEST(test_cell_array_bounds);
    RUN_TEST(test_temperature_array_bounds);
    RUN_TEST(test_concurrent_soc_and_statistics);
    RUN_TEST(test_concurrent_protection_and_voltage_update);
    RUN_TEST(test_memory_usage);
    RUN_TEST(test_no_deep_recursion);
    RUN_TEST(test_float_operations_accuracy);

    return UNITY_END();
}
