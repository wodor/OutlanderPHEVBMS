#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint8_t SOC_CURVE_MIN_POINTS = 2;
constexpr uint8_t SOC_CURVE_MAX_POINTS = 12;

struct SocCurvePoint {
    int voltageMv;
    int socPercent;
};

// Parse lowMv,lowSoc,highMv,highSoc without changing outCurve on failure.
bool parseSocVoltageCurve(const char* input, int outCurve[4]);

// Parse voltage:soc pairs without changing the output on failure.
bool parseSocCurvePoints(const char* input, SocCurvePoint outPoints[SOC_CURVE_MAX_POINTS],
                         uint8_t& outCount);
bool validateSocCurvePoints(const SocCurvePoint* points, uint8_t count);
int interpolateSocCurve(long cellVoltageMv, const SocCurvePoint* points, uint8_t count);
bool formatSocCurvePoints(const SocCurvePoint* points, uint8_t count,
                          char* output, size_t outputSize);
