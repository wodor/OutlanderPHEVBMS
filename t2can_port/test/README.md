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
10 Tests 0 Failures 0 Ignored
OK
```

## Notes

- Tests are designed to run on both native (development machine) and embedded (ESP32-S3) targets
- Some tests include `delay()` calls for debounce simulation - these may need adjustment based on actual timing
- Current sensing tests use placeholder ADC values since physical hardware isn't available in test environment
- NVS persistence is not tested (requires actual flash storage on hardware)

## Future Enhancements

- [ ] Add integration tests for full system behavior
- [ ] Add tests for CAN message decoding
- [ ] Add mock objects for hardware dependencies
- [ ] Add performance/timing tests
- [ ] Add tests for web API endpoints
