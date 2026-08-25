/**
 * @file test_protection.cpp
 * @brief Tests for the deliberately narrow battery-safe-to-use contract.
 */

#include <unity.h>
#include "../src/bms_data.h"
#include "../src/config.h"
#include "../src/protection.h"

extern BmsState g_bmsState;
extern BmsSettings g_bmsSettings;
extern unsigned long g_mockMillis;
extern int g_digitalWriteState[256];

static void makeOneFreshSelectedCmu() {
    g_bmsSettings.expectedCmusA = 0x001;
    g_bmsSettings.expectedCmusB = 0x000;
    g_bmsState.modules[0].present = true;
    g_bmsState.modules[0].lastSeenTime = g_mockMillis;
    g_bmsState.modules[0].temperatures[0] = 25000;
    g_bmsState.lastCanMessageTime = g_mockMillis;
}

void test_safety_output_defaults_high() {
    protectionInit();
    TEST_ASSERT_EQUAL_INT(HIGH, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_safety_output_drops_for_overtemperature() {
    protectionInit();
    g_mockMillis = 1000;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].temperatures[1] = 70000;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERTEMP", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_safety_output_drops_after_ten_seconds_without_can() {
    protectionInit();
    g_mockMillis = CAN_DATA_TIMEOUT_MS;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_safety_output_drops_for_stale_selected_cmu() {
    protectionInit();
    g_mockMillis = CAN_DATA_TIMEOUT_MS + 1;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].lastSeenTime = 0;
    g_bmsState.lastCanMessageTime = g_mockMillis;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_voltage_and_cold_temperature_do_not_drop_safety_output() {
    protectionInit();
    g_mockMillis = 1000;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].voltages[0] = 4199;
    g_bmsState.modules[0].temperatures[1] = -30000;

    TEST_ASSERT_TRUE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OK", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(HIGH, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}

void test_safety_output_drops_at_emergency_cell_voltage_limits() {
    protectionInit();
    g_mockMillis = 1000;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].voltages[0] = 4200;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());

    g_bmsState.modules[0].voltages[0] = 2800;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("UNDERVOLTAGE", protectionGetStatus());
}

void test_supervised_override_lifts_voltage_trip_but_not_can_stop() {
    protectionInit();
    g_mockMillis = 1000;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].voltages[0] = 4200;
    TEST_ASSERT_TRUE(protectionEnableSupervisedOverride());
    TEST_ASSERT_TRUE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("SUPERVISED OVERRIDE", protectionGetStatus());

    g_bmsState.modules[0].temperatures[1] = 70000;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("OVERTEMP", protectionGetStatus());

    protectionCancelSupervisedOverride();
    TEST_ASSERT_FALSE(protectionEnableSupervisedOverride());

    g_bmsState.modules[0].temperatures[1] = 25000;
    g_mockMillis += CAN_DATA_TIMEOUT_MS;
    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_EQUAL_STRING("COMMUNICATION FAULT", protectionGetStatus());
}

void test_supervised_override_expires_after_ten_minutes() {
    protectionInit();
    g_mockMillis = 1000;
    makeOneFreshSelectedCmu();
    g_bmsState.modules[0].voltages[0] = 4200;
    TEST_ASSERT_TRUE(protectionEnableSupervisedOverride());
    TEST_ASSERT_TRUE(protectionCheck());

    g_mockMillis += 10UL * 60UL * 1000UL;
    g_bmsState.lastCanMessageTime = g_mockMillis;
    g_bmsState.modules[0].lastSeenTime = g_mockMillis;

    TEST_ASSERT_FALSE(protectionCheck());
    TEST_ASSERT_FALSE(protectionSupervisedOverrideActive());
    TEST_ASSERT_EQUAL_STRING("OVERVOLTAGE", protectionGetStatus());
    TEST_ASSERT_EQUAL_INT(LOW, g_digitalWriteState[PIN_BATTERY_SAFE_TO_USE]);
}
