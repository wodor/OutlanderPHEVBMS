/**
 * @file test_protection.cpp
 * @brief Tests for the fail-low battery-safe-to-use startup contract.
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/config.h"
#include "../src/protection.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;
extern unsigned long g_mockMillis;
extern int g_digitalWriteState[256];

static void configureOneSelectedCmu() {
    g_bmsSettings.expectedCmusA = 0x001;
    g_bmsSettings.expectedCmusB = 0x000;
}

static void populateHealthyValues(CmuData& cmu) {
    for (int cell = 0; cell < CELLS_PER_MODULE; ++cell) cmu.voltages[cell] = 3700;
    for (int sensor = 0; sensor < TEMPS_PER_MODULE; ++sensor) cmu.temperatures[sensor] = 25000;
}

static void recordCompleteHealthySample(CmuData& cmu) {
    populateHealthyValues(cmu);
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_STATUS, g_mockMillis));
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_1, g_mockMillis));
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_2, g_mockMillis));
    g_bmsState.lastCanMessageTime = g_mockMillis;
}

void test_safety_output_defaults_low() {
    protectionInit();
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_first_protection_check_requires_complete_selected_data() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_partial_startup_data_does_not_raise_safety_output() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    CmuData& cmu = g_bmsState.modules[0];
    populateHealthyValues(cmu);
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_STATUS, g_mockMillis));
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_1, g_mockMillis));

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);

    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_2, g_mockMillis));
    g_bmsState.lastCanMessageTime = g_mockMillis;
    TEST_ASSERT_TRUE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OK", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(HIGH, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_missing_selected_cmu_keeps_safety_output_low() {
    protectionInit();
    g_mockMillis = 100;
    g_bmsSettings.expectedCmusA = 0x003;
    g_bmsSettings.expectedCmusB = 0x000;
    recordCompleteHealthySample(g_bmsState.modules[0]);

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());

    recordCompleteHealthySample(g_bmsState.modules[1]);
    TEST_ASSERT_TRUE(protectionCheck());
}

void test_each_required_frame_group_must_remain_fresh() {
    for (uint8_t missingType = MSG_TYPE_STATUS; missingType <= MSG_TYPE_VOLTS_2; ++missingType) {
        g_bmsState = BmsState();
        protectionInit();
        g_mockMillis = 100;
        configureOneSelectedCmu();
        CmuData& cmu = g_bmsState.modules[0];
        recordCompleteHealthySample(cmu);
        TEST_ASSERT_TRUE(protectionCheck());

        // Continued traffic from the other required groups must not hide one frozen group.
        g_mockMillis += CAN_DATA_TIMEOUT_MS;
        for (uint8_t messageType = MSG_TYPE_STATUS; messageType <= MSG_TYPE_VOLTS_2; ++messageType) {
            if (messageType != missingType) {
                TEST_ASSERT_TRUE(cmu.recordRequiredFrame(messageType, g_mockMillis));
            }
        }
        g_bmsState.lastCanMessageTime = g_mockMillis;
        TEST_ASSERT_FALSE(protectionCheck());
        TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
        TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
    }
}

void test_type_four_cannot_extend_cmu_health() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    CmuData& cmu = g_bmsState.modules[0];
    recordCompleteHealthySample(cmu);
    const unsigned long lastRequiredFrame = cmu.lastSeenTime;

    g_mockMillis += CAN_DATA_TIMEOUT_MS;
    TEST_ASSERT_FALSE(cmu.recordRequiredFrame(4, g_mockMillis));
    TEST_ASSERT_EQUAL_UINT32(lastRequiredFrame, cmu.lastSeenTime);
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
}

void test_safety_output_drops_for_overtemperature() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    recordCompleteHealthySample(g_bmsState.modules[0]);
    g_bmsState.modules[0].temperatures[1] = 70000;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERTEMP", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_voltage_and_cold_temperature_do_not_drop_safety_output() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    recordCompleteHealthySample(g_bmsState.modules[0]);
    g_bmsState.modules[0].voltages[0] = 4199;
    g_bmsState.modules[0].temperatures[1] = -30000;

    TEST_ASSERT_TRUE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OK", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(HIGH, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_safety_output_drops_at_emergency_cell_voltage_limits() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    recordCompleteHealthySample(g_bmsState.modules[0]);
    g_bmsState.modules[0].voltages[0] = 4200;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());

    g_bmsState.modules[0].voltages[0] = 2800;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("UNDERVOLTAGE", protectionGetStatus());
}

void test_complete_recovery_and_reset_require_new_complete_data() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    recordCompleteHealthySample(g_bmsState.modules[0]);
    TEST_ASSERT_TRUE(protectionCheck());

    // A real controller reset clears RAM; no cached pre-reset health may permit.
    g_bmsState = BmsState();
    protectionInit();
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
    TEST_ASSERT_FALSE(protectionCheck());

    configureOneSelectedCmu();
    recordCompleteHealthySample(g_bmsState.modules[0]);
    TEST_ASSERT_TRUE(protectionCheck());
}

void test_supervised_override_lifts_voltage_trip_but_not_can_stop() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    CmuData& cmu = g_bmsState.modules[0];
    recordCompleteHealthySample(cmu);
    cmu.voltages[0] = 4200;
    TEST_ASSERT_TRUE(protectionEnableSupervisedOverride());
    TEST_ASSERT_TRUE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("SUPERVISED OVERRIDE", protectionGetStatus());

    cmu.temperatures[1] = 70000;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERTEMP", protectionGetStatus());

    protectionCancelSupervisedOverride();
    TEST_ASSERT_FALSE(protectionEnableSupervisedOverride());

    cmu.temperatures[1] = 25000;
    g_mockMillis += CAN_DATA_TIMEOUT_MS;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
}

void test_supervised_override_expires_after_ten_minutes() {
    protectionInit();
    g_mockMillis = 100;
    configureOneSelectedCmu();
    CmuData& cmu = g_bmsState.modules[0];
    recordCompleteHealthySample(cmu);
    cmu.voltages[0] = 4200;
    TEST_ASSERT_TRUE(protectionEnableSupervisedOverride());
    TEST_ASSERT_TRUE(protectionCheck());

    g_mockMillis += 10UL * 60UL * 1000UL;
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_STATUS, g_mockMillis));
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_1, g_mockMillis));
    TEST_ASSERT_TRUE(cmu.recordRequiredFrame(MSG_TYPE_VOLTS_2, g_mockMillis));
    g_bmsState.lastCanMessageTime = g_mockMillis;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_FALSE(protectionSupervisedOverrideActive());
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}
