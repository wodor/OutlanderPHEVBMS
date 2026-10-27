#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>

namespace charge {
enum class Mode : uint32_t { NORMAL, TOP_HOLD, TOP_TAIL };
struct Record {
    uint32_t magic = 0x43485932, version = 1, mode = 0, fingerprint = 0, checksum = 0;
};
inline uint32_t checksum(const Record& r) {
    uint32_t hash = 2166136261U;
    const auto* p = reinterpret_cast<const uint8_t*>(&r);
    for (size_t i = 0; i < offsetof(Record, checksum); ++i) { hash ^= p[i]; hash *= 16777619U; }
    return hash;
}
inline bool valid(const Record& r) {
    return r.magic == 0x43485932 && r.version == 1 && r.mode <= 2 && r.checksum == checksum(r);
}
using Save = bool (*)(const Record&, void*);

// Single-owner pure state machine; persistence callback must commit/read back.
class Hysteresis {
public:
    static constexpr uint32_t OFF_MS = 300000, DWELL_MS = 60000;
    void restore(const Record* r, bool corrupt, uint32_t now) {
        *this = Hysteresis();
        storage_error_ = corrupt || (r && !valid(*r));
        if (r && valid(*r)) { mode_ = static_cast<Mode>(r->mode); fingerprint_ = r->fingerprint; configured_ = true; }
        last_ms_ = now;
    }
    void update(uint32_t now, int max_mv, int stop_mv, bool healthy,
                uint32_t fingerprint, Save save, void* context, bool voltage_fresh = false) {
        const uint32_t elapsed = now - last_ms_;
        last_ms_ = now;
        if (mode_ == Mode::TOP_HOLD) age_ = saturate(age_, elapsed);
        // A missed update is not evidence of continuous voltage/health dwell.
        if (elapsed > 5000) dwell_ = false;
        if (!configured_) { fingerprint_ = fingerprint; configured_ = true; }
        else if (fingerprint_ != fingerprint) {
            fingerprint_ = fingerprint; event_ = peak_ = age_ = 0; dwell_ = false;
            persist(save, context);
        }
        if (storage_error_) { dwell_ = false; return; }
        if ((healthy || voltage_fresh) && max_mv >= stop_mv) {
            dwell_ = false;
            if (mode_ != Mode::TOP_HOLD) {
                mode_ = Mode::TOP_HOLD; age_ = 0;
                persist(save, context);
            }
            if (healthy && !event_) { event_ = ++next_event_; if (!event_) event_ = ++next_event_; peak_ = max_mv; }
            return;
        }
        if (!healthy) { dwell_ = false; return; }
        const int threshold = stop_mv - (mode_ == Mode::TOP_HOLD ? 20 : 40);
        if (mode_ == Mode::NORMAL || max_mv > threshold) { dwell_ = false; return; }
        if (!dwell_) { dwell_ = true; dwell_start_ = now; }
        if (uint32_t(now - dwell_start_) < DWELL_MS ||
            (mode_ == Mode::TOP_HOLD && age_ < OFF_MS)) return;
        const Mode previous = mode_;
        mode_ = previous == Mode::TOP_HOLD ? Mode::TOP_TAIL : Mode::NORMAL;
        if (!persist(save, context)) { mode_ = previous; return; }
        event_ = peak_ = age_ = 0; dwell_ = false;
    }
    int limit(int curve_da, int reduced_da) const {
        if (storage_error_ || mode_ == Mode::TOP_HOLD) return 0;
        return mode_ == Mode::TOP_TAIL && curve_da > reduced_da ? reduced_da : curve_da;
    }
    Mode mode() const { return mode_; }
    bool storage_error() const { return storage_error_; }
    uint32_t event() const { return event_; }
    uint32_t peak() const { return peak_; }
    uint32_t age() const { return age_; }
    uint32_t off_remaining() const { return mode_ == Mode::TOP_HOLD && age_ < OFF_MS ? OFF_MS - age_ : 0; }
    uint32_t dwell_ms(uint32_t now) const {
        const uint32_t value = dwell_ ? uint32_t(now - dwell_start_) : 0;
        return value < DWELL_MS ? value : DWELL_MS;
    }
    static const char* name(Mode m) {
        return m == Mode::TOP_HOLD ? "TOP_HOLD" : m == Mode::TOP_TAIL ? "TOP_TAIL" : "NORMAL";
    }
private:
    static uint32_t saturate(uint32_t a, uint32_t b) { return b > UINT32_MAX - a ? UINT32_MAX : a + b; }
    bool persist(Save save, void* context) {
        Record r; r.mode = static_cast<uint32_t>(mode_); r.fingerprint = fingerprint_; r.checksum = checksum(r);
        if (!save || !save(r, context)) { storage_error_ = true; return false; }
        return true;
    }
    Mode mode_ = Mode::NORMAL;
    uint32_t fingerprint_ = 0, next_event_ = 0, event_ = 0, peak_ = 0;
    uint32_t age_ = 0, last_ms_ = 0, dwell_start_ = 0;
    bool configured_ = false, storage_error_ = false, dwell_ = false;
};
}  // namespace charge
