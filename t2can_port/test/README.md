# Unit Tests for OutlanderPHEVBMS V2 Features

This directory contains unit tests for the V2 features added to the t2can_port implementation.

## Test Files

### test_soc_calc.cpp
Tests for SOC (State of Charge) calculation module:
- **test_soc_voltage_calculation**: Validates voltage-based SOC calculation with linear interpolation
- **test_soc_reset**: Tests SOC reset functionality (0%, 50%, 100%, boundary clamping)
- **test_soc_coulomb_counting**: Verifies coulomb-counting calculation logic
- **test_soc_clamping**: Tests SOC boundary limits (0-100%)
- **test_soc_parallel_strings**: Validates SOC calculation with parallel battery strings

### test_protection.cpp
Tests for protection system:
- **test_overvoltage_detection**: Validates overvoltage fault detection
- **test_undervoltage_detection**: Tests undervoltage detection with debouncing
- **test_overtemperature_detection**: Validates overtemperature fault
- **test_undertemperature_detection**: Tests undertemperature fault
- **test_cell_imbalance_detection**: Validates cell voltage delta warnings
- **test_can_charge**: Tests charge permission logic
- **test_can_discharge**: Tests discharge permission logic
- **test_protection_hysteresis**: Validates hysteresis prevents oscillation
- **test_fault_clearing**: Tests fault clearing functionality

### test_bms_data.cpp
Tests for BMS data structures and statistics:
- **test_pack_statistics_voltages**: Validates min/max/avg voltage calculations
- **test_pack_statistics_temperatures**: Tests temperature statistics
- **test_pack_statistics_invalid_temps**: Validates invalid temperature filtering
- **test_pack_statistics_no_modules**: Tests behavior with no modules present
- **test_pack_statistics_zero_voltages**: Validates handling of zero/invalid voltages
- **test_has_any_data**: Tests module presence detection
- **test_get_pack_voltage_parallel_strings**: Validates pack voltage with parallel strings
- **test_settings_defaults**: Verifies BmsSettings default values
- **test_cmu_data_init**: Tests CMU data structure initialization

### test_current_sense.cpp
Tests for current sensing module:
- **test_current_sense_init**: Validates initialization for different sensor types
- **test_current_sense_no_sensor**: Tests behavior with no sensor configured
- **test_current_sense_filtering**: Validates exponential moving average filter
- **test_current_sense_get_amps**: Tests filtered current retrieval
- **test_current_sensor_config**: Validates configuration for all sensor types
- **test_current_sensor_settings**: Verifies default sensor settings

### test_safety_critical.cpp ⚠️ **SAFETY-CRITICAL**
Edge case tests focused on preventing fire hazards and system failures:

**Integer Overflow/Underflow:**
- **test_soc_extreme_current_overflow**: Tests SOC with 1000A charging for 1 hour
- **test_soc_extreme_discharge_underflow**: Tests SOC with -1000A discharge
- **test_voltage_extreme_values**: Tests 65V cell voltage detection
- **test_temperature_extreme_values**: Tests extreme temperature readings

**millis() Rollover (49.7 day boundary):**
- **test_soc_millis_rollover**: Validates SOC calculation across millis() rollover
- **test_protection_millis_rollover**: Validates debounce timers across rollover

**Division by Zero:**
- **test_soc_zero_capacity**: Tests SOC with capacityAh = 0
- **test_current_sense_zero_conversion**: Tests current with zero conversion factor
- **test_pack_voltage_zero_strings**: Tests voltage calculation with zero parallel strings

**Float to Int Conversion:**
- **test_soc_float_to_int_overflow**: Tests large float values converted to int

**Array Bounds:**
- **test_module_array_bounds**: Validates module array limits (8 modules)
- **test_cell_array_bounds**: Validates cell voltage array limits (8 cells)
- **test_temperature_array_bounds**: Validates temperature array limits (3 temps)

**Concurrent Access:**
- **test_concurrent_soc_and_statistics**: Tests simultaneous SOC and statistics updates
- **test_concurrent_protection_and_voltage_update**: Tests protection during voltage changes

**ESP32-S3 Specific:**
- **test_memory_usage**: Validates structures fit in ESP32-S3 RAM (~400KB)
- **test_no_deep_recursion**: Ensures no stack overflow (8KB default stack)
- **test_float_operations_accuracy**: Validates FPU operations produce correct results

## Running Tests

### Native Testing (on development machine)
```bash
cd t2can_port
pio test -e native
```

### Embedded Testing (on actual hardware)
```bash
cd t2can_port
pio test -e outlander_bms --upload-port /dev/cu.usbmodem2101
```

## Test Framework

Tests use the [Unity](http://www.throwtheswitch.org/unity) testing framework, which is lightweight and suitable for embedded systems.

### Test Structure

Each test file follows this pattern:

```cpp
#include <unity.h>
#include "../src/module_to_test.h"

void setUp(void) {
    // Reset state before each test
}

void tearDown(void) {
    // Clean up after each test
}

void test_feature_name() {
    // Arrange
    // Act
    // Assert
    TEST_ASSERT_EQUAL_INT(expected, actual);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_feature_name);
    UNITY_END();
}

void loop() {}
```

## Coverage

The test suite covers:
- ✅ SOC calculation (coulomb-counting and voltage-based)
- ✅ Protection system (voltage, temperature, imbalance)
- ✅ Pack statistics (min/max/avg calculations)
- ✅ Current sensing framework
- ✅ Data structure initialization and defaults
- ✅ Edge cases and boundary conditions
- ✅ **Safety-critical scenarios (overflow, rollover, division by zero, etc.)**

## Safety Features Added

Based on safety-critical testing, the following guards were added to production code:

1. **SOC Calculation (`soc_calc.cpp`)**:
   - Current clamping to ±1000A to prevent overflow
   - Time delta clamping to 1 hour to handle missed updates
   - ampSeconds clamping to ±1e9 to prevent infinity
   - Division by zero checks for capacity
   - NaN/Inf detection with voltage fallback

2. **Current Sensing (`current_sense.cpp`)**:
   - Division by zero protection in conversion factors
   - NaN/Inf detection in measurements
   - Current clamping to ±1000A
   - Filtered current validation

3. **Protection System (`protection.cpp`)**:
   - Extreme voltage detection (>10V per cell)
   - NaN/Inf detection in voltage and temperature
   - millis() rollover handling in debounce timers

4. **Pack Voltage Calculation (`bms_data.h`)**:
   - Division by zero protection for parallel strings

## Adding New Tests

1. Create a new file `test_<module>.cpp` in this directory
2. Include the module header and Unity framework
3. Write test functions following the naming convention `test_<feature>`
4. Add `RUN_TEST()` calls in `setup()`
5. Run tests to verify

## Test Results

Tests output results in Unity format:
```
test_soc_voltage_calculation:PASS
test_soc_reset:PASS
test_overvoltage_detection:PASS
...
-----------------------
48 Tests 0 Failures 0 Ignored
OK
```

## Notes

- Tests are designed to run on both native (development machine) and embedded (ESP32-S3) targets
- Some tests include `delay()` calls for debounce simulation - these may need adjustment based on actual timing
- Current sensing tests use placeholder ADC values since physical hardware isn't available in test environment
- NVS persistence is not tested (requires actual flash storage on hardware)
- **Safety-critical tests prevent conditions that could cause fire, memory corruption, or system crashes**

## Future Enhancements

- [ ] Add integration tests for full system behavior
- [ ] Add tests for CAN message decoding
- [ ] Add mock objects for hardware dependencies
- [ ] Add performance/timing tests
- [ ] Add tests for web API endpoints
- [ ] Add stress tests for long-term stability (>49.7 days)
