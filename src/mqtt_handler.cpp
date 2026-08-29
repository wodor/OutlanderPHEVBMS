/**
 * @file mqtt_handler.cpp
 * @brief Home Assistant MQTT Discovery integration.
 *
 * Cell readings are sampled once per second, published as ten-second means,
 * rounded to one millivolt, and skipped when unchanged. This keeps Recorder
 * writes useful without hiding fast-changing pack-level data.
 */

#include "mqtt_handler.h"

#include "bms_data.h"
#include "config.h"
#include "current_taper.h"
#include "protection.h"
#include "soc_calc.h"
#include <WiFi.h>
#include <PubSubClient.h>

namespace {
constexpr unsigned long CELL_SAMPLE_INTERVAL_MS = 1000;
constexpr unsigned long CELL_PUBLISH_INTERVAL_MS = 10000;
constexpr unsigned long BALANCING_COUNT_PUBLISH_INTERVAL_MS = 10000;
constexpr unsigned long FAST_PUBLISH_INTERVAL_MS = 3000;
constexpr unsigned long MQTT_RECONNECT_INTERVAL_MS = 5000;
constexpr int INVALID_CELL_MV = -1;

WiFiClient s_wifiClient;
PubSubClient s_mqtt(s_wifiClient);
unsigned long s_lastReconnectAttempt = 0;
unsigned long s_lastCellSample = 0;
unsigned long s_lastCellPublish = 0;
unsigned long s_lastFastPublish = 0;
uint64_t s_cellSums[BMS_MODULE_COUNT][CELLS_PER_MODULE] = {};
uint16_t s_cellSamples[BMS_MODULE_COUNT][CELLS_PER_MODULE] = {};
int s_lastCellPublishedMv[BMS_MODULE_COUNT][CELLS_PER_MODULE];
bool s_cellDiscoverySent[BMS_MODULE_COUNT][CELLS_PER_MODULE] = {};
long s_lastModulePublishedMv[BMS_MODULE_COUNT];
bool s_moduleDiscoverySent[BMS_MODULE_COUNT] = {};
bool s_packDiscoverySent = false;
bool s_forcePublish = true;
String s_lastProtection;
int s_lastSoc = -1;
int s_lastUnfilteredSoc = -1;
long s_lastFilteredSocCellMv = -1;
String s_lastSocCurvePoints;
long s_lastPackVoltageMv = -1;
int s_lastAvgTempTenths = -10000;
int s_lastMaximumTempTenths = -10000;
long s_lastCellDeltaMv = -1;
long s_lastMinimumCellMv = -1;
long s_lastMaximumCellMv = -1;
String s_lastDesignVoltage;
String s_lastCurrentLimits;
int s_lastBalancingCells = -1;
uint32_t s_heartbeatSequence = 0;
unsigned long s_lastBalancingCountPublish = 0;
int s_lastModuleMaximumTempTenths[BMS_MODULE_COUNT];

String baseTopic() { return "outlander_bms"; }
String availabilityTopic() { return baseTopic() + "/status"; }

String moduleToken(int moduleIndex) {
    const char bus = moduleIndex < 10 ? 'a' : 'b';
    const int cmuId = (moduleIndex % 10) + 1;
    return String(bus) + "/" + String(cmuId);
}

String moduleUniqueId(int moduleIndex, int cellIndex) {
    const char bus = moduleIndex < 10 ? 'a' : 'b';
    const int cmuId = (moduleIndex % 10) + 1;
    return "outlander_bms_" + String(bus) + "_" + String(cmuId) +
        "_cell_" + String(cellIndex + 1) + "_voltage";
}

String moduleVoltageUniqueId(int moduleIndex) {
    const char bus = moduleIndex < 10 ? 'a' : 'b';
    const int cmuId = (moduleIndex % 10) + 1;
    return "outlander_bms_bus_" + String(bus) + "_cmu_" + String(cmuId) + "_module_voltage";
}

String moduleMaximumTemperatureUniqueId(int moduleIndex) {
    const char bus = moduleIndex < 10 ? 'a' : 'b';
    const int cmuId = (moduleIndex % 10) + 1;
    return "outlander_bms_bus_" + String(bus) + "_cmu_" + String(cmuId) + "_maximum_temperature";
}

bool publish(const String& topic, const String& payload, bool retained = true) {
    return s_mqtt.publish(topic.c_str(), payload.c_str(), retained);
}

void publishDiscovery(const String& uniqueId, const String& name, const String& stateTopic,
                      const char* unit, const char* deviceClass, const char* stateClass,
                      const String& deviceJson, int precision = -1) {
    const String topic = "homeassistant/sensor/" + uniqueId + "/config";
    String payload = "{\"name\":\"" + name + "\",\"uniq_id\":\"" + uniqueId +
        "\",\"stat_t\":\"" + stateTopic + "\",\"avty_t\":\"" + availabilityTopic() +
        "\",\"pl_avail\":\"online\",\"pl_not_avail\":\"offline\"";
    if (unit && unit[0]) payload += ",\"unit_of_meas\":\"" + String(unit) + "\"";
    if (deviceClass && deviceClass[0]) payload += ",\"dev_cla\":\"" + String(deviceClass) + "\"";
    if (stateClass && stateClass[0]) payload += ",\"stat_cla\":\"" + String(stateClass) + "\"";
    if (precision >= 0) payload += ",\"sugg_dsp_prc\":" + String(precision);
    payload += ",\"dev\":" + deviceJson + "}";
    publish(topic, payload);
}

String packDeviceJson() {
    return "{\"ids\":[\"outlander_bms\"],\"name\":\"Outlander BMS\",\"mf\":\"Mitsubishi\",\"mdl\":\"Outlander PHEV BMS\"}";
}

String moduleDeviceJson(int moduleIndex) {
    const char busUpper = moduleIndex < 10 ? 'A' : 'B';
    const char busLower = moduleIndex < 10 ? 'a' : 'b';
    const int cmuId = (moduleIndex % 10) + 1;
    return "{\"ids\":[\"outlander_bms_cmu_" + String(busLower) + "_" + String(cmuId) +
        "\"],\"name\":\"Outlander BMS Bus " + String(busUpper) + " CMU " + String(cmuId) +
        "\",\"mf\":\"Mitsubishi\",\"mdl\":\"Outlander PHEV CMU\",\"via_device\":\"outlander_bms\"}";
}

void publishPackDiscovery() {
    if (s_packDiscoverySent) return;
    const String device = packDeviceJson();
    const String root = baseTopic() + "/pack/";
    publishDiscovery("outlander_bms_soc", "State of Charge", root + "soc", "%", "battery", "measurement", device, 0);
    publishDiscovery("outlander_bms_soc_unfiltered", "Unfiltered State of Charge",
                     root + "soc_unfiltered", "%", "", "measurement", device, 0);
    publishDiscovery("outlander_bms_soc_filtered_cell_voltage", "SOC Filtered Minimum Cell Voltage",
                     root + "soc_filtered_cell_voltage", "V", "voltage", "measurement", device, 3);
    publishDiscovery("outlander_bms_soc_curve_points", "SOC Curve Points",
                     root + "soc_curve_points", "", "", "", device);
    publishDiscovery("outlander_bms_pack_voltage", "Pack Voltage", root + "voltage", "V", "voltage", "measurement", device, 2);
    publishDiscovery("outlander_bms_average_temperature", "Average Temperature", root + "average_temperature", "°C", "temperature", "measurement", device, 1);
    publishDiscovery("outlander_bms_maximum_temperature", "Maximum Temperature", root + "maximum_temperature", "°C", "temperature", "measurement", device, 1);
    publishDiscovery("outlander_bms_all_cell_voltage_delta", "All-Cell Voltage Delta", root + "cell_voltage_delta", "mV", "voltage", "measurement", device, 0);
    publishDiscovery("outlander_bms_minimum_cell_voltage", "Minimum Cell Voltage", root + "minimum_cell_voltage", "V", "voltage", "measurement", device, 3);
    publishDiscovery("outlander_bms_maximum_cell_voltage", "Maximum Cell Voltage", root + "maximum_cell_voltage", "V", "voltage", "measurement", device, 3);
    publishDiscovery("outlander_bms_charge_current_limit", "Charge Current Limit", root + "charge_current_limit", "A", "current", "measurement", device, 1);
    publishDiscovery("outlander_bms_discharge_current_limit", "Discharge Current Limit", root + "discharge_current_limit", "A", "current", "measurement", device, 1);
    publishDiscovery("outlander_bms_balancing_cell_count", "Balancing Cell Count", root + "balancing_cell_count", "", "", "measurement", device, 0);
    publishDiscovery("outlander_bms_protection_state", "Protection State", root + "protection_state", "", "", "", device);
    s_packDiscoverySent = true;
}

void publishCellDiscovery(int moduleIndex, int cellIndex) {
    if (s_cellDiscoverySent[moduleIndex][cellIndex]) return;
    const String token = moduleToken(moduleIndex);
    const String uniqueId = moduleUniqueId(moduleIndex, cellIndex);
    publishDiscovery(uniqueId, "Cell " + String(cellIndex + 1) + " Voltage",
                     baseTopic() + "/cmu/" + token + "/cell/" + String(cellIndex + 1) + "/voltage",
                     "V", "voltage", "measurement", moduleDeviceJson(moduleIndex), 3);
    s_cellDiscoverySent[moduleIndex][cellIndex] = true;
}

void publishModuleDiscovery(int moduleIndex) {
    if (s_moduleDiscoverySent[moduleIndex]) return;
    publishDiscovery(moduleVoltageUniqueId(moduleIndex), "Module Voltage",
                     baseTopic() + "/cmu/" + moduleToken(moduleIndex) + "/module_voltage",
                     "V", "voltage", "measurement", moduleDeviceJson(moduleIndex), 3);
    publishDiscovery(moduleMaximumTemperatureUniqueId(moduleIndex), "Maximum Temperature",
                     baseTopic() + "/cmu/" + moduleToken(moduleIndex) + "/maximum_temperature",
                     "°C", "temperature", "measurement", moduleDeviceJson(moduleIndex), 1);
    s_moduleDiscoverySent[moduleIndex] = true;
}

bool moduleMaximumTemperatureTenths(int moduleIndex, int& maximumTenths) {
    const CmuData& cmu = g_bmsState.modules[moduleIndex];
    bool found = false;
    maximumTenths = 0;
    for (int temp = 0; temp < TEMPS_PER_MODULE; ++temp) {
        const long raw = cmu.temperatures[temp];
        if (raw <= -70000 || raw >= 100000) continue;
        const int tenths = static_cast<int>(lroundf(raw / 100.0f));
        if (!found || tenths > maximumTenths) maximumTenths = tenths;
        found = true;
    }
    return found;
}

void publishModuleMaximumTemperatures() {
    for (int module = 0; module < BMS_MODULE_COUNT; ++module) {
        if (!g_bmsState.modules[module].present) continue;
        int maximumTenths = 0;
        if (!moduleMaximumTemperatureTenths(module, maximumTenths)) continue;
        publishModuleDiscovery(module);
        if (s_forcePublish || maximumTenths != s_lastModuleMaximumTempTenths[module]) {
            publish(baseTopic() + "/cmu/" + moduleToken(module) + "/maximum_temperature",
                    String(maximumTenths / 10.0f, 1));
            s_lastModuleMaximumTempTenths[module] = maximumTenths;
        }
    }
}

int countBalancingCells() {
    int count = 0;
    for (int module = 0; module < BMS_MODULE_COUNT; module++) {
        if (!g_bmsState.modules[module].present) continue;
        for (int cell = 0; cell < CELLS_PER_MODULE; cell++) {
            if ((g_bmsState.modules[module].balanceStatus >> cell) & 1) count++;
        }
    }
    return count;
}

void resetCellSamples() {
    memset(s_cellSums, 0, sizeof(s_cellSums));
    memset(s_cellSamples, 0, sizeof(s_cellSamples));
}

void sampleCells() {
    for (int module = 0; module < BMS_MODULE_COUNT; module++) {
        if (!g_bmsState.modules[module].present) continue;
        for (int cell = 0; cell < CELLS_PER_MODULE; cell++) {
            const long mv = g_bmsState.modules[module].voltages[cell];
            if (mv >= 1500 && mv <= 4500) {
                s_cellSums[module][cell] += static_cast<uint64_t>(mv);
                s_cellSamples[module][cell]++;
            }
        }
    }
}

void publishCellMeans() {
    for (int module = 0; module < BMS_MODULE_COUNT; module++) {
        long moduleMeanMv = 0;
        bool moduleComplete = true;
        for (int cell = 0; cell < CELLS_PER_MODULE; cell++) {
            if (s_cellSamples[module][cell] == 0) {
                moduleComplete = false;
                continue;
            }
            const int averageMv = static_cast<int>((s_cellSums[module][cell] + (s_cellSamples[module][cell] / 2)) /
                                                   s_cellSamples[module][cell]);
            moduleMeanMv += averageMv;
            publishCellDiscovery(module, cell);
            if (s_forcePublish || averageMv != s_lastCellPublishedMv[module][cell]) {
                const String topic = baseTopic() + "/cmu/" + moduleToken(module) + "/cell/" +
                    String(cell + 1) + "/voltage";
                publish(topic, String(averageMv / 1000.0f, 3));
                s_lastCellPublishedMv[module][cell] = averageMv;
            }
        }
        if (moduleComplete) {
            publishModuleDiscovery(module);
            if (s_forcePublish || moduleMeanMv != s_lastModulePublishedMv[module]) {
                publish(baseTopic() + "/cmu/" + moduleToken(module) + "/module_voltage",
                        String(moduleMeanMv / 1000.0f, 3));
                s_lastModulePublishedMv[module] = moduleMeanMv;
            }
        }
    }
    resetCellSamples();
}

void publishFastSummary() {
  const String root = baseTopic() + "/pack/";
  // Unlike state topics, this is intentionally not retained: subscribers use
  // its regular arrival as proof that BMS telemetry is still being produced.
  publish(root + "heartbeat", String(++s_heartbeatSequence), false);
    const long packVoltageMv = lroundf(g_bmsState.packVoltage * 1000.0f);
    const int avgTempTenths = lroundf(g_bmsState.avgTemp * 10.0f);
    if (s_forcePublish || g_bmsState.soc != s_lastSoc) {
        publish(root + "soc", String(g_bmsState.soc)); s_lastSoc = g_bmsState.soc;
    }
    const int unfilteredSoc = socUnfilteredPercent();
    if (s_forcePublish || unfilteredSoc != s_lastUnfilteredSoc) {
        publish(root + "soc_unfiltered", String(unfilteredSoc));
        s_lastUnfilteredSoc = unfilteredSoc;
    }
    const long filteredSocCellMv = socFilteredCellMv();
    if (filteredSocCellMv > 0 &&
        (s_forcePublish || filteredSocCellMv != s_lastFilteredSocCellMv)) {
        publish(root + "soc_filtered_cell_voltage", String(filteredSocCellMv / 1000.0f, 3));
        s_lastFilteredSocCellMv = filteredSocCellMv;
    }
    char curveBuffer[160] = {};
    if (formatSocCurvePoints(g_bmsSettings.socCurvePoints,
                             g_bmsSettings.socCurvePointCount,
                             curveBuffer, sizeof(curveBuffer))) {
        const String curveText(curveBuffer);
        if (s_forcePublish || curveText != s_lastSocCurvePoints) {
            publish(root + "soc_curve_points", curveText);
            s_lastSocCurvePoints = curveText;
        }
    }
    if (s_forcePublish || packVoltageMv != s_lastPackVoltageMv) {
        publish(root + "voltage", String(packVoltageMv / 1000.0f, 2)); s_lastPackVoltageMv = packVoltageMv;
    }
    // The gateway must use the BMS's configured voltage-SOC endpoints, rather
    // than a copy compiled into its own firmware. Publish both endpoints in
    // one retained message so it cannot combine a new maximum with an old
    // minimum after a configuration change.
    uint16_t selectedModuleCount = 0;
    for (int module = 0; module < BMS_MODULE_COUNT; ++module) {
        if (isModuleSelectedForPack(module)) ++selectedModuleCount;
    }
    const uint16_t seriesCellCount = selectedModuleCount * CELLS_PER_MODULE;
    const String designVoltage = String(
        (g_bmsSettings.socVoltageCurve[2] * seriesCellCount) / 1000.0f, 1) +
        "," +
        String((g_bmsSettings.socVoltageCurve[0] * seriesCellCount) / 1000.0f, 1);
    if (s_forcePublish || designVoltage != s_lastDesignVoltage) {
        // Keep the existing Home Assistant design-voltage entities current.
        // The gateway consumes the combined topic above to update both values
        // atomically, while these individual retained topics preserve the UI
        // contract used by the previous BMS firmware.
        publish(root + "design_maximum_voltage",
                String((g_bmsSettings.socVoltageCurve[2] * seriesCellCount) / 1000.0f, 1));
        publish(root + "design_minimum_voltage",
                String((g_bmsSettings.socVoltageCurve[0] * seriesCellCount) / 1000.0f, 1));
        // Home Assistant's existing discovery records use these shorter
        // state-topic names. Retain them during the migration so existing
        // dashboards update without an entity-registry rewrite.
        publish(root + "design_max_voltage",
                String((g_bmsSettings.socVoltageCurve[2] * seriesCellCount) / 1000.0f, 1));
        publish(root + "design_min_voltage",
                String((g_bmsSettings.socVoltageCurve[0] * seriesCellCount) / 1000.0f, 1));
        publish(root + "design_voltage", designVoltage);
        s_lastDesignVoltage = designVoltage;
    }
    if (s_forcePublish || avgTempTenths != s_lastAvgTempTenths) {
        publish(root + "average_temperature", String(avgTempTenths / 10.0f, 1)); s_lastAvgTempTenths = avgTempTenths;
    }
    if (g_bmsState.highestTemp > -70.0f && g_bmsState.highestTemp < 100.0f) {
        const int maximumTempTenths = lroundf(g_bmsState.highestTemp * 10.0f);
        if (s_forcePublish || maximumTempTenths != s_lastMaximumTempTenths) {
            publish(root + "maximum_temperature", String(maximumTempTenths / 10.0f, 1));
            s_lastMaximumTempTenths = maximumTempTenths;
        }
    }
    publishModuleMaximumTemperatures();
    if (s_forcePublish || g_bmsState.cellVoltageDeltaMv != s_lastCellDeltaMv) {
        publish(root + "cell_voltage_delta", String(g_bmsState.cellVoltageDeltaMv)); s_lastCellDeltaMv = g_bmsState.cellVoltageDeltaMv;
    }
    if (s_forcePublish || g_bmsState.lowestCellMv != s_lastMinimumCellMv) {
        publish(root + "minimum_cell_voltage", String(g_bmsState.lowestCellMv / 1000.0f, 3));
        s_lastMinimumCellMv = g_bmsState.lowestCellMv;
    }
    if (s_forcePublish || g_bmsState.highestCellMv != s_lastMaximumCellMv) {
        publish(root + "maximum_cell_voltage", String(g_bmsState.highestCellMv / 1000.0f, 3));
        s_lastMaximumCellMv = g_bmsState.highestCellMv;
    }
    // One retained payload lets the gateway update both directions atomically.
    // The individual topics are for Home Assistant observability only.
    const CurrentLimits limits = currentTaperCalculate();
    const String currentLimits = String(limits.chargeDa / 10.0f, 1) + "," +
                                 String(limits.dischargeDa / 10.0f, 1);
    if (s_forcePublish || currentLimits != s_lastCurrentLimits) {
        publish(root + "charge_current_limit", String(limits.chargeDa / 10.0f, 1));
        publish(root + "discharge_current_limit", String(limits.dischargeDa / 10.0f, 1));
        publish(root + "current_limits", currentLimits);
        s_lastCurrentLimits = currentLimits;
    }
}

void publishBalancingCellCount() {
    const int balancingCells = countBalancingCells();
    if (balancingCells != s_lastBalancingCells) {
        publish(baseTopic() + "/pack/balancing_cell_count", String(balancingCells));
        s_lastBalancingCells = balancingCells;
    }
}

void publishProtectionImmediately() {
    const String protection = protectionGetStatus();
    if (s_forcePublish || protection != s_lastProtection) {
        publish(baseTopic() + "/pack/protection_state", protection);
        s_lastProtection = protection;
    }
}

bool connect() {
    if (MQTT_HOST[0] == '\0' || MQTT_USERNAME[0] == '\0') return false;
    const bool connected = s_mqtt.connect(MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD,
                                          availabilityTopic().c_str(), 0, true, "offline");
    if (connected) {
        publish(availabilityTopic(), "online");
        s_packDiscoverySent = false;
        memset(s_cellDiscoverySent, 0, sizeof(s_cellDiscoverySent));
        memset(s_moduleDiscoverySent, 0, sizeof(s_moduleDiscoverySent));
        s_forcePublish = true;
        Serial.println("[MQTT] Connected to Home Assistant broker");
    }
    return connected;
}
}  // namespace

void mqttInit() {
    for (int module = 0; module < BMS_MODULE_COUNT; module++) {
        s_lastModulePublishedMv[module] = INVALID_CELL_MV;
        s_lastModuleMaximumTempTenths[module] = -10000;
        for (int cell = 0; cell < CELLS_PER_MODULE; cell++) {
            s_lastCellPublishedMv[module][cell] = INVALID_CELL_MV;
        }
    }
    s_mqtt.setServer(MQTT_HOST, MQTT_PORT);
    s_mqtt.setBufferSize(1024);
}

void mqttTick() {
    const unsigned long now = millis();
    if (WiFi.status() != WL_CONNECTED) return;
    if (!s_mqtt.connected()) {
        if (now - s_lastReconnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
            s_lastReconnectAttempt = now;
            connect();
        }
        return;
    }

    s_mqtt.loop();
    publishPackDiscovery();
    publishProtectionImmediately();

    if (now - s_lastCellSample >= CELL_SAMPLE_INTERVAL_MS) {
        s_lastCellSample = now;
        sampleCells();
    }
    if (now - s_lastFastPublish >= FAST_PUBLISH_INTERVAL_MS) {
        s_lastFastPublish = now;
        publishFastSummary();
    }
    if (now - s_lastCellPublish >= CELL_PUBLISH_INTERVAL_MS) {
        s_lastCellPublish = now;
        publishCellMeans();
    }
    if (now - s_lastBalancingCountPublish >= BALANCING_COUNT_PUBLISH_INTERVAL_MS) {
        s_lastBalancingCountPublish = now;
        publishBalancingCellCount();
    }
    s_forcePublish = false;
}
