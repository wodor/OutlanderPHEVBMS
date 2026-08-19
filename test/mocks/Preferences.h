/**
 * @file Preferences.h
 * @brief Mock ESP32 Preferences (NVS) for native unit testing
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <map>
#include <string>
#include <stdlib.h>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false) {
        namespace_ = name ? name : "";
        readOnly_ = readOnly;
        return true;
    }

    void end() { namespace_.clear(); }

    bool clear() {
        if (readOnly_) return false;
        std::map<std::string, std::string>& values = storage();
        const std::string prefix = namespace_ + "\n";
        for (std::map<std::string, std::string>::iterator it = values.begin(); it != values.end();) {
            if (it->first.compare(0, prefix.size(), prefix) == 0) {
                values.erase(it++);
            } else {
                ++it;
            }
        }
        return true;
    }

    bool remove(const char* key) {
        if (readOnly_) return false;
        return storage().erase(fullKey(key)) > 0;
    }

    size_t putInt(const char* key, int32_t value) { return put(key, std::to_string(value), 4); }
    size_t putUInt(const char* key, uint32_t value) { return put(key, std::to_string(value), 4); }
    size_t putFloat(const char* key, float value) { return put(key, std::to_string(value), 4); }
    size_t putBool(const char* key, bool value) { return put(key, value ? "1" : "0", 1); }

    int32_t getInt(const char* key, int32_t defaultValue = 0) {
        const std::string* value = get(key);
        return value ? static_cast<int32_t>(strtol(value->c_str(), NULL, 10)) : defaultValue;
    }
    uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
        const std::string* value = get(key);
        return value ? static_cast<uint32_t>(strtoul(value->c_str(), NULL, 10)) : defaultValue;
    }
    float getFloat(const char* key, float defaultValue = 0.0f) {
        const std::string* value = get(key);
        return value ? strtof(value->c_str(), NULL) : defaultValue;
    }
    bool getBool(const char* key, bool defaultValue = false) {
        const std::string* value = get(key);
        return value ? *value == "1" : defaultValue;
    }

    bool isKey(const char* key) { return storage().count(fullKey(key)) != 0; }

    static void clearAll() { storage().clear(); }

private:
    std::string namespace_;
    bool readOnly_ = false;

    static std::map<std::string, std::string>& storage() {
        static std::map<std::string, std::string> values;
        return values;
    }

    std::string fullKey(const char* key) const {
        return namespace_ + "\n" + (key ? key : "");
    }

    size_t put(const char* key, const std::string& value, size_t bytes) {
        if (readOnly_) return 0;
        storage()[fullKey(key)] = value;
        return bytes;
    }

    const std::string* get(const char* key) const {
        std::map<std::string, std::string>& values = storage();
        std::map<std::string, std::string>::const_iterator it = values.find(fullKey(key));
        return it == values.end() ? NULL : &it->second;
    }
};
