/**
 * @file Preferences.h
 * @brief Mock ESP32 Preferences (NVS) for native unit testing
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) {
        (void)name; (void)readOnly;
        return true;
    }

    void end() {}

    bool clear() { return true; }
    bool remove(const char* key) { (void)key; return true; }

    size_t putInt(const char* key, int32_t value) { (void)key; (void)value; return 4; }
    size_t putUInt(const char* key, uint32_t value) { (void)key; (void)value; return 4; }
    size_t putFloat(const char* key, float value) { (void)key; (void)value; return 4; }
    size_t putBool(const char* key, bool value) { (void)key; (void)value; return 1; }

    int32_t getInt(const char* key, int32_t defaultValue = 0) { (void)key; return defaultValue; }
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) { (void)key; return defaultValue; }
    float getFloat(const char* key, float defaultValue = 0.0f) { (void)key; return defaultValue; }
    bool getBool(const char* key, bool defaultValue = false) { (void)key; return defaultValue; }

    bool isKey(const char* key) { (void)key; return false; }
};
