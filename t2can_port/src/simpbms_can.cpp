/**
 * @file simpbms_can.cpp
 * @brief SIMPBMS/Victron-style CAN output implementation
 */

#include "simpbms_can.h"
#include "bms_data.h"
#include "can_handler.h"
#include "config.h"
#include <mcp2515.h>
#include <math.h>
#include <limits.h>

static SimpBmsStats s_stats = {};

// Cache of valid cell voltages (mV)
static uint16_t s_cellVoltages[BMS_MODULE_COUNT * CELLS_PER_MODULE] = {};
static uint16_t s_cellCount = 0;
static uint16_t s_cellIndex = 0;

static const uint8_t s_bmsName[8] = { 'S', 'I', 'M', 'P', ' ', 'B', 'M', 'S' };
static const uint8_t s_bmsManu[8] = { 'S', 'I', 'M', 'P', ' ', 'E', 'C', 'O' };

static uint8_t selectBus() {
    // If Bus A is not used for CMUs, use it for SIMPBMS output
    return g_bmsSettings.useBusAForCmu ? 1 : 0;
}

static bool sendFrame(uint16_t id, uint8_t len, const uint8_t* data) {
    struct can_frame frame = {};
    frame.can_id = id;
    frame.can_dlc = len;
    if (data && len > 0) {
        memcpy(frame.data, data, len);
    }

    uint8_t bus = selectBus();
    if (bus == 1 && !canIsBusBEnabled()) {
        return false;
    }

    bool ok = canSendFrame(frame, bus);
    if (ok) {
        s_stats.framesSent++;
        s_stats.lastSendTime = millis();
        s_stats.lastBusUsed = bus;
    }
    return ok;
}

static void refreshCellCache() {
    s_cellCount = 0;
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        if (!g_bmsState.modules[m].present) continue;
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            long v = g_bmsState.modules[m].voltages[c];
            if (v >= 1500 && v <= 4500) {
                if (s_cellCount < (BMS_MODULE_COUNT * CELLS_PER_MODULE)) {
                    s_cellVoltages[s_cellCount++] = (uint16_t)v;
                }
            }
        }
    }
    if (s_cellIndex >= s_cellCount) {
        s_cellIndex = 0;
    }
}

// Count configured CMUs from expected masks (10 bits per bus).
// Falls back to seriesCells-derived module count if masks are empty.
static uint16_t getConfiguredModuleCountForLimits() {
    uint16_t configured = 0;
    for (uint8_t i = 0; i < 10; i++) {
        if (g_bmsSettings.expectedCmusA & (1U << i)) configured++;
        if (g_bmsSettings.expectedCmusB & (1U << i)) configured++;
    }

    if (configured > 0) {
        return configured;
    }

    const uint16_t fallback = (uint16_t)((g_bmsSettings.seriesCells + (CELLS_PER_MODULE - 1)) / CELLS_PER_MODULE);
    return fallback > 0 ? fallback : 1;
}

SimpBmsDesignVoltageLimits simpBmsGetDesignVoltageLimits() {
    const uint16_t moduleCount = getConfiguredModuleCountForLimits();
    const uint16_t seriesCells = moduleCount * CELLS_PER_MODULE;

    SimpBmsDesignVoltageLimits limits = {};
    limits.moduleCount = moduleCount;
    limits.seriesCells = seriesCells;
    // Battery Emulator design limits use the configured per-cell safety limits.
    limits.maxVoltageV = g_bmsSettings.overVoltage * seriesCells;
    limits.minVoltageV = g_bmsSettings.dischargeVoltage * seriesCells;
    return limits;
}

void simpBmsInit() {
    s_stats = {};
    s_cellCount = 0;
    s_cellIndex = 0;
}

void simpBmsTick() {
    if (!g_bmsSettings.simpBmsEnabled) {
        return;
    }

    const bool hasData = g_bmsState.hasAnyData();

    // 0x351: charge/discharge limits + voltage cutoffs (0.1V, 0.1A)
    {
        uint8_t data[8] = {0};

        const SimpBmsDesignVoltageLimits limits = simpBmsGetDesignVoltageLimits();

        uint16_t charge_dV = (uint16_t)round(limits.maxVoltageV * 10.0f);
        uint16_t discharge_dV = (uint16_t)round(limits.minVoltageV * 10.0f);

        int16_t maxCharge = g_bmsState.targetChargeCurrent != 0 ? g_bmsState.targetChargeCurrent
                                                               : g_bmsSettings.maxChargeCurrent;
        int16_t maxDischarge = g_bmsState.targetDischargeCurrent != 0 ? g_bmsState.targetDischargeCurrent
                                                                     : g_bmsSettings.maxDischargeCurrent;

        data[0] = lowByte(charge_dV);
        data[1] = highByte(charge_dV);
        data[2] = lowByte((uint16_t)maxCharge);
        data[3] = highByte((uint16_t)maxCharge);
        data[4] = lowByte((uint16_t)maxDischarge);
        data[5] = highByte((uint16_t)maxDischarge);
        data[6] = lowByte(discharge_dV);
        data[7] = highByte(discharge_dV);

        sendFrame(0x351, 8, data);
    }

    // 0x355: SOC / SOH
    {
        uint8_t data[8] = {0};
        uint16_t soc = (uint16_t)constrain(g_bmsState.soc, 0, 100);
        uint16_t soh = 100; // placeholder

        data[0] = lowByte(soc);
        data[1] = highByte(soc);
        data[2] = lowByte(soh);
        data[3] = highByte(soh);
        data[4] = 0x00;
        data[5] = 0x00;
        data[6] = 0x00;
        data[7] = 0x00;

        sendFrame(0x355, 8, data);
    }

    // 0x356: pack voltage and current
    {
        uint8_t data[8] = {0};
        float packV = hasData ? g_bmsState.packVoltage : 0.0f;
        int32_t current_mA = (int32_t)lround(g_bmsState.currentAmps * 1000.0f);
        if (current_mA > INT16_MAX) current_mA = INT16_MAX;
        if (current_mA < INT16_MIN) current_mA = INT16_MIN;

        uint16_t pack_cV = (uint16_t)round(packV * 100.0f); // 0.01V units
        int16_t current_i16 = (int16_t)current_mA;          // mA

        data[0] = lowByte(pack_cV);
        data[1] = highByte(pack_cV);
        data[2] = lowByte(current_i16);
        data[3] = highByte(current_i16);
        data[4] = 0x00;
        data[5] = 0x00;
        data[6] = 0x00;
        data[7] = 0x00;

        sendFrame(0x356, 8, data);
    }

    // 0x35A: alarms/warnings (not mapped yet)
    {
        uint8_t data[8] = {0};
        sendFrame(0x35A, 8, data);
    }

    // 0x35E: BMS name
    {
        sendFrame(0x35E, 8, s_bmsName);
    }

    // 0x370: BMS manufacturer
    {
        sendFrame(0x370, 8, s_bmsManu);
    }

    // 0x373: min/max cell voltage + min/max temperature (Kelvin)
    {
        uint8_t data[8] = {0};
        uint16_t lowCell = hasData ? (uint16_t)g_bmsState.lowestCellMv : 0;
        uint16_t highCell = hasData ? (uint16_t)g_bmsState.highestCellMv : 0;
        float lowTempC = hasData ? g_bmsState.lowestTemp : 0.0f;
        float highTempC = hasData ? g_bmsState.highestTemp : 0.0f;

        uint16_t lowTempK = (uint16_t)round(lowTempC + 273.15f);
        uint16_t highTempK = (uint16_t)round(highTempC + 273.15f);

        data[0] = lowByte(lowCell);
        data[1] = highByte(lowCell);
        data[2] = lowByte(highCell);
        data[3] = highByte(highCell);
        data[4] = lowByte(lowTempK);
        data[5] = highByte(lowTempK);
        data[6] = lowByte(highTempK);
        data[7] = highByte(highTempK);

        sendFrame(0x373, 8, data);
    }

    // 0x379: installed capacity (Ah)
    {
        uint8_t data[2] = {0};
        uint16_t cap = (uint16_t)(g_bmsSettings.capacityAh * g_bmsSettings.parallelStrings);
        data[0] = lowByte(cap);
        data[1] = highByte(cap);
        sendFrame(0x379, 2, data);
    }

    // 0x372: cell voltage (one cell per frame)
    {
        refreshCellCache();
        uint8_t data[8] = {0};
        data[0] = lowByte(s_cellCount);
        data[1] = highByte(s_cellCount);

        if (s_cellCount > 0) {
            uint16_t cellNum = (uint16_t)(s_cellIndex + 1);
            uint16_t cellMv = s_cellVoltages[s_cellIndex];

            data[3] = (uint8_t)cellNum;
            data[6] = lowByte(cellMv);
            data[7] = highByte(cellMv);

            s_cellIndex++;
            if (s_cellIndex >= s_cellCount) {
                s_cellIndex = 0;
            }
        }

        sendFrame(0x372, 8, data);
    }
}

SimpBmsStats simpBmsGetStats() {
    return s_stats;
}
