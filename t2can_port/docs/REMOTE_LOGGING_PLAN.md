# Remote Logging Implementation Plan for Outlander PHEV BMS

## Executive Summary

This document outlines a comprehensive plan for implementing a robust, standards-based remote logging system for the Outlander PHEV Battery Management System (BMS). The solution will enable near-realtime monitoring of battery cell data while maintaining long-term storage of critical events, with resilience to network interruptions.

**Target Architecture:**
- **BMS Device**: ESP32-S3 (T-2Can board) with WiFi
- **Log Aggregator**: Raspberry Pi 3 on local network
- **Protocol**: MQTT over WiFi with local buffering
- **Monitoring**: Heartbeat-based health monitoring

---

## Table of Contents

1. [Requirements Analysis](#requirements-analysis)
2. [Standards and Protocols](#standards-and-protocols)
3. [Architecture Overview](#architecture-overview)
4. [Data Classification and Retention](#data-classification-and-retention)
5. [Network Resilience Strategy](#network-resilience-strategy)
6. [Heartbeat and Monitoring](#heartbeat-and-monitoring)
7. [Implementation Phases](#implementation-phases)
8. [Technology Stack](#technology-stack)
9. [Performance Considerations](#performance-considerations)
10. [Security Considerations](#security-considerations)
11. [References and Standards](#references-and-standards)

---

## Requirements Analysis

### Functional Requirements

1. **Real-time Cell Monitoring**
   - Near real-time logging of individual cell voltages (64 cells)
   - Temperature data from all modules (24 temperature sensors)
   - Short retention period (hours to days)

2. **Critical Event Logging**
   - State of Charge (SOC) changes
   - Error conditions and protection events
   - System state changes
   - Long retention period (weeks to months)

3. **Network Resilience**
   - Graceful handling of WiFi disconnections
   - Local buffering of logs during outages
   - Automatic backlog transmission on reconnection
   - Configurable buffer size with overflow management

4. **Health Monitoring**
   - Heartbeat transmission every 10 seconds
   - BMS availability monitoring
   - Automated alerting on communication loss

### Non-Functional Requirements

- **Reliability**: No data loss for critical events
- **Efficiency**: Minimal CPU and memory overhead on ESP32-S3
- **Scalability**: Support for future expansion
- **Maintainability**: Standards-based, well-documented

---

## Standards and Protocols

### Recommended Primary Protocol: MQTT

**Why MQTT?**

MQTT (Message Queuing Telemetry Transport) is the industry standard for IoT logging and telemetry. It is specifically designed for constrained devices and unreliable networks.

**Key Benefits:**
- **Lightweight**: Minimal packet overhead (2-byte header minimum)
- **QoS Levels**: Three Quality of Service levels (0, 1, 2) for different reliability needs
- **Last Will and Testament (LWT)**: Automatic notification of client disconnect
- **Retained Messages**: New subscribers receive last known state
- **Topic Hierarchy**: Organized data structure
- **Persistent Sessions**: Automatic message queuing during disconnection
- **Wide Support**: Mature client libraries for ESP32 and Linux

**MQTT Specifications:**
- Standard: OASIS MQTT v3.1.1 (ISO/IEC 20922:2016) or MQTT v5.0
- Port: 1883 (unencrypted) or 8883 (TLS)
- Protocol: TCP-based with keep-alive mechanism

### Alternative Protocols (for consideration)

#### 1. Syslog (RFC 5424/5425)
**Pros:**
- Universal logging standard
- Built-in severity levels
- Wide tooling support (rsyslog, syslog-ng)

**Cons:**
- No built-in QoS or acknowledgment
- UDP variant can lose messages
- Less efficient for structured IoT data
- No automatic session persistence

**Use Case:** Could be used as secondary logging channel for traditional log aggregation

#### 2. InfluxDB Line Protocol over HTTP
**Pros:**
- Optimized for time-series data
- Native support in monitoring tools
- Built-in tagging and field structure

**Cons:**
- Requires HTTP overhead (larger packets)
- No built-in offline buffering
- More complex implementation

**Use Case:** Could be used for direct time-series database writes (Raspberry Pi runs InfluxDB)

#### 3. CoAP (Constrained Application Protocol - RFC 7252)
**Pros:**
- Designed for constrained devices
- UDP-based (lower overhead)
- REST-like semantics

**Cons:**
- Less mature ecosystem
- More complex reliability implementation
- Limited library support on ESP32

**Use Case:** Only if extreme resource constraints exist

### Standards for Log Format

#### Recommended: Structured Logging with JSON

**Format:** JSON Lines (one JSON object per line)

**Benefits:**
- Human-readable
- Machine-parseable
- Self-describing schema
- Wide tooling support

**Example Log Entry:**
```json
{"ts":1706000000,"level":"INFO","module":"cell","cmu":1,"cell":3,"voltage_mv":4050,"tag":"cell_voltage"}
{"ts":1706000000,"level":"WARN","module":"protection","event":"overvoltage","cell_id":"1-3","voltage_mv":4210,"tag":"alert"}
{"ts":1706000000,"level":"INFO","module":"soc","soc_pct":87.5,"current_a":-15.2,"tag":"state"}
```

**Schema Fields:**
- `ts`: Unix timestamp (seconds or milliseconds)
- `level`: Log level (DEBUG, INFO, WARN, ERROR, CRITICAL)
- `module`: Subsystem identifier
- `tag`: Classification tag for retention policies
- Additional fields vary by message type

#### Alternative: MessagePack

**Benefits:**
- Binary format (smaller size)
- 2-5x smaller than JSON for same data
- Schema-less

**Drawback:**
- Not human-readable without tools

**Use Case:** If bandwidth is severely constrained

---

## Architecture Overview

### System Components

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3 BMS Device                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │  CAN Handler │  │  Protection  │  │   SOC Tracking   │  │
│  │  (Cell Data) │  │   System     │  │                  │  │
│  └──────┬───────┘  └──────┬───────┘  └────────┬─────────┘  │
│         │                  │                    │            │
│         └──────────────────┼────────────────────┘            │
│                            │                                 │
│                  ┌─────────▼──────────┐                      │
│                  │   Log Aggregator   │                      │
│                  │   (collects logs)  │                      │
│                  └─────────┬──────────┘                      │
│                            │                                 │
│                  ┌─────────▼──────────┐                      │
│                  │   MQTT Publisher   │                      │
│                  │  (with buffering)  │                      │
│                  └─────────┬──────────┘                      │
│                            │                                 │
│           ┌────────────────┼────────────────┐                │
│           │                │                │                │
│      ┌────▼────┐    ┌──────▼─────┐   ┌─────▼─────┐          │
│      │ Memory  │    │ SPIFFS/    │   │  MQTT     │          │
│      │ Buffer  │◄──►│ LittleFS   │   │  Client   │          │
│      │ (Ring)  │    │ (Overflow) │   │           │          │
│      └─────────┘    └────────────┘   └─────┬─────┘          │
│                                             │                │
└─────────────────────────────────────────────┼────────────────┘
                                              │ WiFi
                                              │
                        ┌─────────────────────▼──────────────┐
                        │    Raspberry Pi 3 Log Server       │
                        │                                    │
                        │  ┌──────────────────────────────┐  │
                        │  │    Mosquitto MQTT Broker     │  │
                        │  │    (message persistence)     │  │
                        │  └──────────┬───────────────────┘  │
                        │             │                      │
                        │  ┌──────────▼───────────────────┐  │
                        │  │  Log Processor & Storage     │  │
                        │  │  - InfluxDB (time-series)    │  │
                        │  │  - Loki (logs)               │  │
                        │  │  - PostgreSQL (events)       │  │
                        │  └──────────┬───────────────────┘  │
                        │             │                      │
                        │  ┌──────────▼───────────────────┐  │
                        │  │  Monitoring & Dashboards     │  │
                        │  │  - Grafana                   │  │
                        │  │  - Alertmanager              │  │
                        │  └──────────────────────────────┘  │
                        └────────────────────────────────────┘
```

### Data Flow

1. **BMS collects data** from CAN bus (cell voltages, temperatures)
2. **Log Aggregator** formats logs with appropriate tags
3. **Logs are categorized** by priority:
   - **HIGH**: Errors, protection events, SOC changes → persistent buffer
   - **MEDIUM**: State changes, warnings → memory buffer
   - **LOW**: Cell data snapshots → memory buffer only
4. **MQTT Publisher** transmits with QoS levels:
   - **QoS 2**: Critical events (exactly once)
   - **QoS 1**: Important state (at least once)
   - **QoS 0**: High-frequency cell data (fire and forget)
5. **Buffering strategy**:
   - Recent data in RAM (circular buffer, 10-50KB)
   - Overflow to flash storage (SPIFFS/LittleFS, up to 1-2MB)
   - Oldest non-critical data deleted on overflow
6. **On reconnection**: Backlog transmitted with rate limiting

---

## Data Classification and Retention

### Log Levels and Tags

| Level | Tag | Examples | Retention (BMS) | Retention (Server) | MQTT QoS |
|-------|-----|----------|-----------------|--------------------| ---------|
| CRITICAL | `alert`, `fault` | Overvoltage, overtemp, system failure | Until sent | 6-12 months | 2 |
| ERROR | `error` | CAN timeout, sensor failure | Until sent | 3-6 months | 2 |
| WARNING | `warning` | Cell imbalance, temperature warning | Until sent | 1-3 months | 1 |
| INFO | `state`, `soc` | SOC changes, balance state changes | Until sent | 1-3 months | 1 |
| INFO | `cell_voltage`, `temp` | Individual cell readings | 1 hour max | 7-30 days | 0 |
| DEBUG | `debug` | CAN frames, internal state | Not buffered | 24 hours | 0 |

### Data Rates and Volume Estimates

**Assumptions:**
- 64 cells × 8 bytes/sample = 512 bytes per cell voltage snapshot
- 24 temps × 8 bytes/sample = 192 bytes per temperature snapshot
- JSON overhead ~30%
- Update rates:
  - Cell data: 1-10 Hz
  - SOC/current: 1 Hz
  - Heartbeat: 0.1 Hz (every 10s)

**Bandwidth Calculations:**

| Data Type | Rate | Size/msg | Bandwidth |
|-----------|------|----------|-----------|
| Cell voltages (all) | 1 Hz | 1.5 KB | 1.5 KB/s = 130 MB/day |
| Cell voltages (all) | 0.1 Hz | 1.5 KB | 150 B/s = 13 MB/day |
| SOC/Current/Pack | 1 Hz | 200 B | 200 B/s = 17 MB/day |
| Temperatures | 1 Hz | 800 B | 800 B/s = 69 MB/day |
| Heartbeat | 0.1 Hz | 100 B | 10 B/s = 0.8 MB/day |

**Recommendation:** Sample cell data at 0.1 Hz (every 10 seconds) for continuous logging
- Reduces bandwidth to ~30 MB/day
- Sample at 1-10 Hz only during specific events (charging, alerts)
- Raspberry Pi 3 can easily handle this over WiFi

**Buffering Requirements:**
- Critical buffer: 100 KB (persistent flash) - ~500 critical events
- General buffer: 50 KB (RAM) - ~10 minutes of low-rate cell data
- High-rate buffer: 10 KB (RAM) - ~6 seconds at 1 Hz

---

## Network Resilience Strategy

### Problem Scenarios

1. **Brief WiFi dropout** (< 30 seconds)
   - Common in home networks
   - Solution: Memory buffer + MQTT persistent session

2. **Extended WiFi outage** (minutes to hours)
   - Router restart, maintenance
   - Solution: Flash storage overflow + backlog transmission

3. **Server/broker unavailable**
   - Raspberry Pi reboot, disk full
   - Solution: MQTT persistent session on broker

4. **Permanent network loss**
   - Critical system failure
   - Solution: Log to local SD card, manual retrieval

### Implementation Strategy

#### Phase 1: Memory Ring Buffer (Essential)

**Library:** Use a circular buffer implementation

**Configuration:**
```cpp
// In config.h
#define LOG_BUFFER_SIZE 51200  // 50 KB
#define LOG_ENTRY_MAX_SIZE 512  // Max bytes per log entry
```

**Behavior:**
- FIFO (First In, First Out) queue in RAM
- When full: Drop oldest LOW priority logs, keep CRITICAL/ERROR
- Buffer capacity: ~100 log entries at 512 bytes each

**Implementation Notes:**
- Use `std::deque` or fixed circular buffer
- Separate buffer for critical logs (never overwrite)
- Track buffer usage percentage

#### Phase 2: Flash Overflow Storage (Important)

**Storage:** SPIFFS or LittleFS on ESP32-S3

**Configuration:**
```cpp
#define LOG_OVERFLOW_DIR "/logs"
#define LOG_OVERFLOW_MAX_SIZE (2 * 1024 * 1024)  // 2 MB max
#define LOG_FILE_MAX_SIZE (512 * 1024)  // 512 KB per file
```

**Behavior:**
- When memory buffer fills, write to flash
- Rotate files (e.g., `log_001.jsonl`, `log_002.jsonl`)
- On reconnection: Stream files to broker, then delete

**File Format:** JSON Lines (newline-delimited JSON)
```
{"ts":1706000000,"level":"ERROR",...}\n
{"ts":1706000001,"level":"WARN",...}\n
```

**Flash Wear Considerations:**
- ESP32-S3 flash has ~10,000 write cycles
- With 2MB allocation and 30 MB/day write rate = 15 writes/day per sector
- Expected lifetime: ~1-2 years before wear concerns
- Mitigation: Use wear-leveling filesystem (LittleFS), rotate files

#### Phase 3: MQTT Session Persistence (Recommended)

**MQTT Feature:** Clean Session = false

When MQTT client connects with `cleanSession=false`, the broker stores:
- Subscriptions
- Unacknowledged QoS 1 and QoS 2 messages
- New messages published while client offline (for subscriptions)

**Configuration:**
```cpp
// PubSubClient configuration
client.setServer(mqtt_server, 1883);
client.setCallback(callback);
client.setBufferSize(2048);  // Increase for larger messages

// On connect
client.connect(clientID, mqtt_user, mqtt_pass, 
               "bms/status", 0, true, "offline",  // LWT
               false);  // cleanSession = false
```

**Benefits:**
- Broker queues QoS 1/2 messages automatically
- No client-side code needed for broker buffering

**Limitations:**
- Broker must have sufficient disk space
- Messages queued only for subscribed topics

#### Phase 4: Backlog Transmission Strategy

**Goals:**
- Don't flood broker on reconnection
- Prioritize recent data over old data for high-frequency logs
- Ensure critical logs are sent first

**Algorithm:**
```
On WiFi reconnection:
1. Connect to MQTT broker with QoS 2 for connection
2. Send buffered CRITICAL/ERROR logs first (QoS 2)
3. Send WARNING/INFO logs next (QoS 1)
4. Stream flash overflow files:
   - Read oldest file first
   - Send in chunks with rate limiting (e.g., 10 messages/second)
   - Delete file after successful transmission
5. Resume normal operation
```

**Rate Limiting:**
```cpp
#define BACKLOG_TX_RATE_MS 100  // Send one buffered message every 100ms
unsigned long lastBacklogTx = 0;

void processBacklog() {
    if (bufferHasData() && millis() - lastBacklogTx >= BACKLOG_TX_RATE_MS) {
        sendOldestBufferedMessage();
        lastBacklogTx = millis();
    }
}
```

**Priority Queue:**
```cpp
// Pseudocode for multi-priority buffer
class LogBuffer {
    std::vector<LogEntry> criticalBuffer;  // Always send first
    std::vector<LogEntry> normalBuffer;    // Send after critical
    std::vector<LogEntry> lowPriorityBuffer;  // Send last
    
    void addLog(LogEntry entry) {
        switch (entry.priority) {
            case CRITICAL: criticalBuffer.push_back(entry); break;
            case NORMAL: normalBuffer.push_back(entry); break;
            case LOW: lowPriorityBuffer.push_back(entry); break;
        }
    }
    
    LogEntry getNextToSend() {
        if (!criticalBuffer.empty()) return criticalBuffer.front();
        if (!normalBuffer.empty()) return normalBuffer.front();
        return lowPriorityBuffer.front();
    }
};
```

### Disk Full Handling on ESP32

**Detection:**
```cpp
size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
if (freeBytes < LOG_FILE_MAX_SIZE) {
    // Delete oldest log files
    deleteOldestLogFiles();
}
```

**Strategy:**
- Keep critical logs in separate directory with reserved space
- Delete oldest non-critical logs first
- Log deletion event itself (metadata preserved)

---

## Heartbeat and Monitoring

### Heartbeat Mechanism

**Purpose:** Prove BMS is alive and responding

**Implementation:**

```cpp
// main.cpp
unsigned long lastHeartbeat = 0;
#define HEARTBEAT_INTERVAL_MS 10000  // 10 seconds

void loop() {
    if (millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
        sendHeartbeat();
        lastHeartbeat = millis();
    }
}

void sendHeartbeat() {
    StaticJsonDocument<256> doc;
    doc["ts"] = millis() / 1000;
    doc["type"] = "heartbeat";
    doc["soc"] = g_bmsState.soc;
    doc["pack_v"] = g_bmsState.packVoltage;
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["wifi_rssi"] = WiFi.RSSI();
    
    String output;
    serializeJson(doc, output);
    mqttClient.publish("bms/heartbeat", output.c_str(), false);  // QoS 0
}
```

**Heartbeat Message Contents:**
- Timestamp
- SOC percentage
- Pack voltage
- System uptime
- Free heap memory
- WiFi signal strength (RSSI)
- Last CAN message time (detect CAN bus issues)

### MQTT Last Will and Testament (LWT)

**Setup:**
```cpp
// On MQTT connect, register LWT
client.connect(
    "bms-outlander-01",          // Client ID
    mqtt_user, mqtt_pass,         // Credentials
    "bms/status",                 // LWT topic
    1,                            // LWT QoS
    true,                         // LWT retain
    "{\"status\":\"offline\"}",   // LWT message
    false                         // Clean session
);

// After successful connection, send online status
client.publish("bms/status", "{\"status\":\"online\"}", true);  // Retained
```

**Benefits:**
- Broker automatically publishes offline status on unexpected disconnect
- Monitoring system instantly knows BMS is down
- Retained message means subscribers get last known state

### Monitoring on Raspberry Pi

#### Option 1: Simple Script with Alerts

**Technology:** Bash + systemd timer or cron

```bash
#!/bin/bash
# /opt/bms-monitor/check_heartbeat.sh

LAST_HEARTBEAT=$(redis-cli GET bms:last_heartbeat)
NOW=$(date +%s)
AGE=$((NOW - LAST_HEARTBEAT))

if [ $AGE -gt 30 ]; then
    echo "BMS heartbeat missing for $AGE seconds!" | mail -s "BMS ALERT" user@example.com
fi
```

**Mosquitto Bridge to Update Timestamp:**
```bash
# Subscribe to heartbeat and update Redis
mosquitto_sub -h localhost -t "bms/heartbeat" | while read msg; do
    redis-cli SET bms:last_heartbeat $(date +%s)
done
```

#### Option 2: Monitoring Stack (Recommended)

**Components:**
1. **Telegraf** - Collects MQTT messages, converts to metrics
2. **InfluxDB** - Stores time-series data
3. **Grafana** - Visualizes and alerts
4. **Prometheus Alertmanager** - Alert routing and deduplication

**Data Flow:**
```
MQTT Broker → Telegraf (mqtt_consumer) → InfluxDB → Grafana
                                           ↓
                                      Alertmanager → Email/SMS/Webhook
```

**Telegraf Configuration:**
```toml
# /etc/telegraf/telegraf.d/bms.conf
[[inputs.mqtt_consumer]]
  servers = ["tcp://localhost:1883"]
  topics = ["bms/heartbeat", "bms/+/alert"]
  data_format = "json"
  tag_keys = ["type"]
  
[[outputs.influxdb_v2]]
  urls = ["http://localhost:8086"]
  token = "$INFLUX_TOKEN"
  organization = "home"
  bucket = "bms"
```

**Grafana Alert Rule:**
```yaml
# Alert if no heartbeat for 30 seconds
name: BMS Heartbeat Missing
condition: last() of query(A, 30s ago, now) is below 1
  query(A): SELECT count("uptime_s") FROM "mqtt_consumer" 
            WHERE time > now() - 30s
```

**Alert Channels:**
- Email (sendmail/SMTP)
- Pushover (mobile notifications)
- Webhook to home automation (Home Assistant, etc.)
- SMS via Twilio

#### Option 3: Home Assistant Integration

Many users already run Home Assistant for home automation.

**MQTT Sensor Configuration:**
```yaml
# configuration.yaml
mqtt:
  sensor:
    - name: "BMS State of Charge"
      state_topic: "bms/heartbeat"
      value_template: "{{ value_json.soc }}"
      unit_of_measurement: "%"
      device_class: battery
      
    - name: "BMS Pack Voltage"
      state_topic: "bms/heartbeat"
      value_template: "{{ value_json.pack_v }}"
      unit_of_measurement: "V"
      device_class: voltage
      
  binary_sensor:
    - name: "BMS Online"
      state_topic: "bms/status"
      value_template: "{{ value_json.status }}"
      payload_on: "online"
      payload_off: "offline"
      device_class: connectivity

automation:
  - alias: "BMS Offline Alert"
    trigger:
      platform: state
      entity_id: binary_sensor.bms_online
      to: 'off'
      for: "00:00:30"  # 30 seconds
    action:
      service: notify.mobile_app
      data:
        message: "BMS has gone offline!"
        title: "BMS Alert"
```

---

## Implementation Phases

### Phase 1: Basic Logging Infrastructure (Week 1)

**Goal:** Get logs flowing from ESP32 to Raspberry Pi

**Tasks:**
1. Install Mosquitto MQTT broker on Raspberry Pi
   ```bash
   sudo apt-get install mosquitto mosquitto-clients
   ```
2. Create log aggregator module in ESP32 code
   - New file: `src/log_manager.h/cpp`
   - Implements basic log formatting
   - Categories: DEBUG, INFO, WARN, ERROR, CRITICAL
3. Implement memory ring buffer (50 KB)
   - Store logs temporarily
   - Simple FIFO queue
4. Create MQTT publisher module
   - Use PubSubClient library
   - Connect to Raspberry Pi broker
   - Publish logs to topics by level: `bms/log/{level}`
5. Test basic connectivity and message delivery

**Success Criteria:**
- BMS sends logs to Raspberry Pi
- Can view logs with `mosquitto_sub -h localhost -t "bms/log/#"`
- Memory buffer prevents loss during brief disconnections

### Phase 2: Network Resilience (Week 2)

**Goal:** Handle WiFi outages gracefully

**Tasks:**
1. Implement WiFi reconnection logic
   - Current code has basic WiFi, enhance with retry logic
   - Exponential backoff: 1s, 2s, 4s, 8s, 15s, 30s
2. Add flash storage for overflow logs
   - Initialize LittleFS on startup
   - Create `/logs` directory
   - Implement file rotation (max 5 files × 512 KB)
3. Implement priority-based buffering
   - Critical logs never dropped
   - Lower priority logs dropped when buffer full
4. Add backlog transmission on reconnection
   - Rate-limited sending (10 messages/sec)
   - Send critical logs first
5. Test scenarios:
   - Unplug WiFi for 1 minute → verify logs buffered
   - Unplug for 30 minutes → verify flash storage used
   - Reconnect → verify backlog sent

**Success Criteria:**
- Survive 1-hour WiFi outage with no critical data loss
- All buffered logs transmitted within 5 minutes of reconnection
- System remains responsive during backlog transmission

### Phase 3: Structured Logging and Tagging (Week 3)

**Goal:** Implement retention-based log tagging

**Tasks:**
1. Define log schema with tags
   - Create `LogEntry` struct with tag field
   - Document tag taxonomy
2. Update all logging calls throughout codebase
   - `logCellVoltage()` with tag `cell_voltage`
   - `logProtectionEvent()` with tag `alert`
   - `logSocChange()` with tag `state`
3. Implement filtering based on tags
   - High-frequency cell data uses QoS 0
   - Critical events use QoS 2
4. Add JSON formatting for all logs
   - Use ArduinoJson library
   - Consistent timestamp format

**Success Criteria:**
- All logs are properly tagged
- Different tags use appropriate QoS levels
- Logs are valid JSON and parseable

### Phase 4: Heartbeat and Monitoring (Week 4)

**Goal:** Implement liveness monitoring

**Tasks:**
1. Implement 10-second heartbeat on ESP32
   - Send to `bms/heartbeat` topic
   - Include SOC, voltage, uptime, memory stats
2. Implement MQTT Last Will and Testament
   - Set LWT on connection
   - Send online status after connection
3. Set up monitoring on Raspberry Pi
   - Option A (simple): Bash script + cron
   - Option B (recommended): Telegraf + InfluxDB + Grafana
4. Configure alerting
   - Email or mobile notification
   - Alert on 30-second heartbeat miss
5. Create Grafana dashboard
   - Heartbeat status panel
   - SOC gauge
   - Pack voltage graph
   - Cell voltage heatmap

**Success Criteria:**
- Heartbeat visible in Grafana
- Alert fires within 30 seconds of BMS shutdown
- Dashboard shows realtime data

### Phase 5: Raspberry Pi Log Storage (Week 5)

**Goal:** Implement retention policies on server side

**Tasks:**
1. Set up InfluxDB for time-series data
   ```bash
   sudo apt-get install influxdb
   ```
2. Configure Telegraf to consume MQTT and write to InfluxDB
3. Implement retention policies
   - `cell_voltage` tag: 7 days
   - `state` tag: 90 days
   - `alert` tag: 365 days
4. Set up Loki for long-term log storage (optional)
   - Lightweight log aggregation
   - Cheap storage for text logs
5. Create backup strategy
   - Daily InfluxDB snapshots
   - Weekly offsite backup

**Success Criteria:**
- All data categories stored with appropriate retention
- Can query historical data from InfluxDB
- Data automatically expires per retention policy

### Phase 6: Optimization and Tuning (Week 6)

**Goal:** Optimize for long-term stability

**Tasks:**
1. Performance profiling
   - Measure CPU usage during logging
   - Measure memory fragmentation
   - Measure WiFi bandwidth usage
2. Tune sampling rates
   - Cell data: Test 0.1 Hz, 0.5 Hz, 1 Hz
   - Adjust based on bandwidth and storage needs
3. Implement adaptive logging
   - Higher rate during charging (1 Hz)
   - Lower rate when idle (0.1 Hz)
   - Burst mode on alerts (10 Hz for 10 seconds)
4. Add statistics logging
   - Log buffer usage %
   - MQTT message success rate
   - WiFi connection stability
5. Documentation
   - Update README with logging setup instructions
   - Document MQTT topic structure
   - Create troubleshooting guide

**Success Criteria:**
- Logging overhead < 5% CPU
- Memory usage stable (no leaks)
- System runs for 7+ days without restart

---

## Technology Stack

### ESP32-S3 (BMS Device)

| Component | Technology | Library/Tool |
|-----------|-----------|--------------|
| MQTT Client | PubSubClient | [Arduino PubSubClient](https://github.com/knolleary/pubsubclient) |
| JSON | ArduinoJson | [ArduinoJson](https://arduinojson.org/) |
| Filesystem | LittleFS | Built-in ESP32 |
| WiFi | ESP32 WiFi | Built-in |
| Ring Buffer | Custom or std::deque | C++ STL |

**PlatformIO Dependencies:**
```ini
lib_deps =
    me-no-dev/ESPAsyncWebServer@^3.6.0
    me-no-dev/AsyncTCP@^1.1.1
    knolleary/PubSubClient@^2.8
    bblanchon/ArduinoJson@^6.21.3
```

### Raspberry Pi 3 (Log Server)

| Component | Technology | Purpose |
|-----------|-----------|---------|
| MQTT Broker | Mosquitto | Message routing and persistence |
| Time-series DB | InfluxDB 2.x | Store metrics and sensor data |
| Log storage | Loki (optional) | Long-term log storage |
| Collector | Telegraf | MQTT to InfluxDB bridge |
| Visualization | Grafana | Dashboards and alerts |
| Alerting | Alertmanager | Alert routing and deduplication |

**Installation:**
```bash
# Mosquitto
sudo apt-get install mosquitto mosquitto-clients

# InfluxDB 2.x
wget https://dl.influxdata.com/influxdb/releases/influxdb2_2.7.1_arm64.deb
sudo dpkg -i influxdb2_2.7.1_arm64.deb

# Telegraf
wget https://dl.influxdata.com/telegraf/releases/telegraf_1.28.3_arm64.deb
sudo dpkg -i telegraf_1.28.3_arm64.deb

# Grafana
sudo apt-get install -y software-properties-common
wget -q -O - https://packages.grafana.com/gpg.key | sudo apt-key add -
echo "deb https://packages.grafana.com/oss/deb stable main" | sudo tee /etc/apt/sources.list.d/grafana.list
sudo apt-get update
sudo apt-get install grafana
```

---

## Performance Considerations

### ESP32-S3 Resource Constraints

**Available Resources:**
- Flash: 16 MB (after firmware: ~14 MB available)
- RAM: 320 KB SRAM + 16 MB PSRAM
- CPU: Dual-core Xtensa @ 240 MHz

**Resource Allocation:**
- Firmware + libraries: ~1-2 MB flash
- Log buffer (RAM): 50 KB
- Log overflow (Flash): 2 MB
- MQTT buffer: 2-4 KB
- JSON serialization buffer: 2-4 KB

**CPU Budget:**
- Main loop: ~100 Hz (10ms per iteration)
- CAN processing: ~5% CPU
- WiFi/MQTT: ~10% CPU
- Logging: Target < 5% CPU

**Memory Budget for Logging:**
```cpp
// Typical log entry in RAM
struct LogEntry {
    uint32_t timestamp;        // 4 bytes
    uint8_t level;             // 1 byte
    uint8_t tag;               // 1 byte (enum)
    char message[128];         // 128 bytes
};  // Total: ~134 bytes

// Ring buffer
LogEntry buffer[384];  // 384 entries × 134 bytes = ~51 KB
```

### WiFi Bandwidth Management

**Calculations:**
- WiFi 802.11n: ~40 Mbps realistic throughput
- MQTT overhead: ~10 bytes per message (fixed header + topic)
- Target: < 1% WiFi utilization for logging (400 Kbps)

**Strategies:**
1. **Compression** (optional)
   - Use MessagePack instead of JSON (50% smaller)
   - Only compress cell data payloads (bulk messages)

2. **Batching** (recommended)
   - Group multiple cell readings into single MQTT message
   - Example: Send all 64 cells in one message instead of 64 messages
   ```json
   {
     "ts": 1706000000,
     "tag": "cell_snapshot",
     "cells": [4050, 4051, 4052, ..., 4049]  // All 64 cells
   }
   ```

3. **Adaptive Sampling**
   - 0.1 Hz during idle
   - 1 Hz during charge/discharge
   - 10 Hz during alert conditions (limited duration)

### Raspberry Pi 3 Considerations

**Resources:**
- CPU: Quad-core ARM Cortex-A53 @ 1.2 GHz
- RAM: 1 GB
- Network: WiFi 802.11n (40 Mbps typical)

**Capacity Estimates:**
- InfluxDB can handle 10,000+ points/sec (we need ~10-100)
- Disk I/O: ~20 MB/s (SD card) - adequate for 30 MB/day
- MQTT broker: Can handle 10,000+ msgs/sec (we need ~1-10)

**Bottleneck:** SD card wear and failure
- **Mitigation 1:** Use high-endurance SD card (designed for surveillance cameras)
- **Mitigation 2:** Mount /var/log and InfluxDB data on USB3 drive
- **Mitigation 3:** Use log2ram to reduce SD writes

**Recommended SD Card:**
- SanDisk High Endurance 64GB (rated for 10,000 hours video recording)

---

## Security Considerations

### Authentication and Encryption

#### MQTT Security

**Level 1: Username/Password (Minimum)**
```
# mosquitto.conf
allow_anonymous false
password_file /etc/mosquitto/passwd

# Create user
sudo mosquitto_passwd -c /etc/mosquitto/passwd bms_device
```

**Level 2: TLS Encryption (Recommended)**
```
# mosquitto.conf
listener 8883
cafile /etc/mosquitto/ca_certificates/ca.crt
certfile /etc/mosquitto/certs/server.crt
keyfile /etc/mosquitto/certs/server.key
require_certificate false
```

**ESP32 Configuration:**
```cpp
#include <WiFiClientSecure.h>
WiFiClientSecure espClient;
PubSubClient client(espClient);

// Set CA certificate
espClient.setCACert(ca_cert);
client.setServer(mqtt_server, 8883);  // TLS port
```

**Considerations:**
- TLS adds ~30 KB RAM overhead on ESP32
- Slightly higher CPU usage (minimal on ESP32-S3)
- Prevents WiFi sniffing of battery data

#### Network Isolation

**Best Practice:** Isolate IoT devices on separate VLAN
- BMS on IoT VLAN (e.g., 192.168.2.x)
- Raspberry Pi has two interfaces: IoT VLAN + main network
- Firewall rules: IoT devices cannot initiate connections to main network

### Data Privacy

**Sensitive Data:**
- Battery state of charge (SoC) reveals usage patterns
- Charge/discharge patterns reveal daily routines

**Mitigations:**
- Keep data on local network (don't send to cloud)
- If cloud logging needed: Use VPN or SSH tunnel
- Encrypt at rest: InfluxDB supports encryption

### Firmware Security

**Considerations:**
1. **WiFi credentials in firmware** - stored in `.config.h` (not in git)
2. **OTA updates** - Enable encrypted OTA for remote firmware updates
3. **Debug port** - Disable Serial debug in production builds

---

## References and Standards

### MQTT Specifications
- **MQTT v3.1.1** - [OASIS Standard](http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/mqtt-v3.1.1.html)
- **MQTT v5.0** - [OASIS Standard](https://docs.oasis-open.org/mqtt/mqtt/v5.0/mqtt-v5.0.html)
- **ISO/IEC 20922:2016** - Official ISO standard for MQTT 3.1.1

### Logging Standards
- **RFC 5424** - The Syslog Protocol
- **RFC 5425** - TLS Transport Mapping for Syslog
- **RFC 3164** - The BSD Syslog Protocol (legacy)
- **JSON Lines** - [jsonlines.org](https://jsonlines.org/) - Streaming JSON format

### IoT Best Practices
- **IETF RFC 7228** - Terminology for Constrained-Node Networks
- **IETF RFC 7252** - Constrained Application Protocol (CoAP)
- **IETF RFC 8428** - Sensor Measurement Lists (SenML)
- **Eclipse IoT Working Group** - [IoT Architecture](https://iot.eclipse.org/)

### Time-Series Databases
- **InfluxDB Documentation** - [docs.influxdata.com](https://docs.influxdata.com/)
- **Prometheus Best Practices** - [prometheus.io](https://prometheus.io/docs/practices/)

### ESP32 Resources
- **ESP-IDF Documentation** - [docs.espressif.com](https://docs.espressif.com/)
- **LittleFS** - [Lightweight filesystem for embedded](https://github.com/littlefs-project/littlefs)
- **PubSubClient** - [MQTT library for Arduino](https://pubsubclient.knolleary.net/)

### Monitoring and Observability
- **The Twelve-Factor App** - [Logs as Event Streams](https://12factor.net/logs)
- **Google SRE Book** - Monitoring Distributed Systems
- **Grafana Labs Best Practices** - [grafana.com](https://grafana.com/docs/)

---

## Appendix A: MQTT Topic Structure

### Recommended Topic Hierarchy

```
bms/
├── status                    # online/offline (LWT)
├── heartbeat                 # 10-second heartbeat
├── log/
│   ├── critical             # Critical events (QoS 2)
│   ├── error                # Error events (QoS 2)
│   ├── warning              # Warnings (QoS 1)
│   ├── info                 # Info messages (QoS 1)
│   └── debug                # Debug messages (QoS 0)
├── cell/
│   ├── voltage/snapshot     # All 64 cells (QoS 0)
│   ├── voltage/1            # Individual cell (rarely used)
│   └── temperature/snapshot # All 24 temps (QoS 0)
├── pack/
│   ├── soc                  # State of charge (QoS 1)
│   ├── voltage              # Pack voltage (QoS 1)
│   ├── current              # Pack current (QoS 1)
│   └── power                # Pack power (QoS 1)
├── protection/
│   ├── alert                # Protection alerts (QoS 2)
│   └── status               # Protection system status (QoS 1)
└── config/
    ├── get                  # Request config
    └── set                  # Update config
```

### Message Retention Policy

| Topic | Retain Flag | Reason |
|-------|-------------|---------|
| bms/status | Yes | Subscribers need last known state |
| bms/heartbeat | No | Only current heartbeat matters |
| bms/log/* | No | Logs are events, not state |
| bms/cell/voltage/snapshot | Yes | Useful for late subscribers |
| bms/pack/* | Yes | Current values are state |
| bms/protection/status | Yes | Current status is state |

---

## Appendix B: Example Log Messages

### Heartbeat Message
```json
{
  "ts": 1706000000,
  "type": "heartbeat",
  "device_id": "bms-outlander-01",
  "soc": 87.5,
  "pack_v": 49.8,
  "current_a": -15.2,
  "uptime_s": 345678,
  "free_heap": 245678,
  "wifi_rssi": -65,
  "can_ok": true
}
```

### Cell Voltage Snapshot
```json
{
  "ts": 1706000000,
  "tag": "cell_voltage",
  "voltages_mv": [
    4050, 4051, 4052, 4048, 4049, 4050, 4051, 4053,  // CMU 0
    4045, 4046, 4047, 4048, 4049, 4050, 4051, 4052,  // CMU 1
    4048, 4049, 4050, 4051, 4052, 4053, 4054, 4055,  // CMU 2
    4047, 4048, 4049, 4050, 4051, 4052, 4053, 4054,  // CMU 3
    4050, 4051, 4052, 4053, 4054, 4055, 4056, 4057,  // CMU 4
    4049, 4050, 4051, 4052, 4053, 4054, 4055, 4056,  // CMU 5
    4048, 4049, 4050, 4051, 4052, 4053, 4054, 4055,  // CMU 6
    4047, 4048, 4049, 4050, 4051, 4052, 4053, 4054   // CMU 7
  ]
}
```

### Protection Alert
```json
{
  "ts": 1706000000,
  "level": "CRITICAL",
  "tag": "alert",
  "module": "protection",
  "event": "overvoltage",
  "cell_id": "3-2",
  "voltage_mv": 4210,
  "threshold_mv": 4200,
  "action": "charge_disabled"
}
```

### SOC Update
```json
{
  "ts": 1706000000,
  "level": "INFO",
  "tag": "state",
  "module": "soc",
  "soc_pct": 87.5,
  "soc_change": -0.5,
  "current_a": -15.2,
  "method": "coulomb_counting"
}
```

---

## Appendix C: Sample Code Snippets

### Log Manager Interface

```cpp
// log_manager.h
#pragma once
#include <Arduino.h>

enum LogLevel {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

enum LogTag {
    TAG_CELL_VOLTAGE,
    TAG_TEMPERATURE,
    TAG_SOC,
    TAG_STATE,
    TAG_ALERT,
    TAG_ERROR,
    TAG_DEBUG
};

class LogManager {
public:
    void init();
    void loop();  // Call from main loop
    
    // Logging functions
    void log(LogLevel level, LogTag tag, const char* message);
    void logf(LogLevel level, LogTag tag, const char* format, ...);
    
    // Specialized logging
    void logCellVoltages();
    void logTemperatures();
    void logSocChange(float oldSoc, float newSoc);
    void logProtectionEvent(const char* event, const char* details);
    
    // Buffer management
    size_t getBufferUsage();
    bool isBufferFull();
    void clearBuffer();

private:
    void sendLog(const char* json);
    void bufferLog(const char* json, LogLevel level);
    void processBacklog();
};

extern LogManager g_logManager;
```

### Usage Example

```cpp
// In protection.cpp
void checkOvervoltage() {
    for (int m = 0; m < BMS_MODULE_COUNT; m++) {
        for (int c = 0; c < CELLS_PER_MODULE; c++) {
            if (g_bmsState.modules[m].voltages[c] > 4200) {
                char msg[128];
                snprintf(msg, sizeof(msg), 
                    "Overvoltage detected: CMU %d Cell %d = %ld mV",
                    m, c, g_bmsState.modules[m].voltages[c]);
                
                g_logManager.log(LOG_CRITICAL, TAG_ALERT, msg);
                
                // Disable charging
                disableCharging();
            }
        }
    }
}

// In main.cpp loop()
void loop() {
    // ... existing code ...
    
    // Process logging
    g_logManager.loop();
    
    // ... existing code ...
}
```

---

## Summary and Recommendations

### Core Recommendations

1. **Use MQTT as primary protocol** - Industry standard, perfect for this use case
2. **Implement 3-tier buffering**:
   - Memory ring buffer (50 KB) for immediate resilience
   - Flash overflow (2 MB) for extended outages
   - MQTT persistent session for broker-side queuing
3. **Sample cell data at 0.1 Hz normally, 1 Hz during events** - Balances insight and bandwidth
4. **Implement 10-second heartbeat with LWT** - Enables reliable monitoring
5. **Use JSON Lines format** - Human-readable, widely supported
6. **Deploy monitoring stack on Raspberry Pi** - Telegraf + InfluxDB + Grafana
7. **Implement tag-based retention** - Keep critical logs long, cell data short

### Quick Start Path

For fastest deployment:
1. Week 1: Get basic MQTT logging working
2. Week 2: Add memory buffer and reconnection
3. Week 3: Add heartbeat and simple monitoring
4. Defer flash overflow to Phase 2 (if needed)

### Alternative Minimal Approach

If time is very limited:
- Use ESP32 serial output to Raspberry Pi over USB
- Run Python script on RPi to parse serial and write to InfluxDB
- Simple but loses wireless benefit

### Next Steps

1. Review and approve this plan
2. Provision Raspberry Pi (install OS, setup networking)
3. Begin Phase 1 implementation
4. Iterate based on real-world testing

---

## Revision History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-01-22 | Planning Agent | Initial comprehensive plan |

---

## Questions for Stakeholder

Before implementation, please consider:

1. **Raspberry Pi Setup**: Do you already have the RPi3 running? What OS?
2. **Monitoring Preference**: Simple bash script or full Grafana stack?
3. **Alert Method**: Email, mobile app (Pushover), Home Assistant, or other?
4. **Retention Priorities**: Confirm retention periods for each data type
5. **Security**: Do you need TLS encryption for MQTT?
6. **Cell Data Rate**: 0.1 Hz (every 10s) acceptable or need faster?

---

*End of Planning Document*
