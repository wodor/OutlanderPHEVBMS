#include "soc_curve_validation.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool parseSocVoltageCurve(const char* input, int outCurve[4]) {
    if (input == nullptr || outCurve == nullptr || *input == '\0') return false;

    int parsed[4];
    const char* cursor = input;
    for (int i = 0; i < 4; ++i) {
        if (*cursor == '\0' || *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') {
            return false;
        }

        errno = 0;
        char* end = nullptr;
        const long value = std::strtol(cursor, &end, 10);
        if (errno == ERANGE || end == cursor || value < INT_MIN || value > INT_MAX) return false;

        const char expectedDelimiter = (i < 3) ? ',' : '\0';
        if (*end != expectedDelimiter) return false;
        parsed[i] = static_cast<int>(value);
        cursor = end + (i < 3 ? 1 : 0);
    }

    const bool sensibleRanges =
        parsed[0] >= 2000 && parsed[0] <= 5000 &&
        parsed[2] >= 2000 && parsed[2] <= 5000 &&
        parsed[1] >= 0 && parsed[1] <= 100 &&
        parsed[3] >= 0 && parsed[3] <= 100;
    const bool orderedEndpoints = parsed[0] < parsed[2] && parsed[1] < parsed[3];
    if (!sensibleRanges || !orderedEndpoints) return false;

    for (int i = 0; i < 4; ++i) outCurve[i] = parsed[i];
    return true;
}

bool validateSocCurvePoints(const SocCurvePoint* points, uint8_t count) {
    if (points == nullptr || count < SOC_CURVE_MIN_POINTS || count > SOC_CURVE_MAX_POINTS) {
        return false;
    }
    for (uint8_t i = 0; i < count; ++i) {
        if (points[i].voltageMv < 2000 || points[i].voltageMv > 5000 ||
            points[i].socPercent < 0 || points[i].socPercent > 100) {
            return false;
        }
        if (i > 0 && (points[i].voltageMv <= points[i - 1].voltageMv ||
                      points[i].socPercent < points[i - 1].socPercent)) {
            return false;
        }
    }
    return true;
}

bool parseSocCurvePoints(const char* input, SocCurvePoint outPoints[SOC_CURVE_MAX_POINTS],
                         uint8_t& outCount) {
    if (input == nullptr || outPoints == nullptr || *input == '\0') return false;

    SocCurvePoint parsed[SOC_CURVE_MAX_POINTS] = {};
    uint8_t count = 0;
    const char* cursor = input;
    while (*cursor != '\0') {
        if (count >= SOC_CURVE_MAX_POINTS || *cursor == ' ' || *cursor == '\t' ||
            *cursor == '\r' || *cursor == '\n') {
            return false;
        }

        errno = 0;
        char* voltageEnd = nullptr;
        const long voltage = std::strtol(cursor, &voltageEnd, 10);
        if (errno == ERANGE || voltageEnd == cursor || *voltageEnd != ':' ||
            voltage < INT_MIN || voltage > INT_MAX) {
            return false;
        }

        errno = 0;
        char* socEnd = nullptr;
        const long soc = std::strtol(voltageEnd + 1, &socEnd, 10);
        if (errno == ERANGE || socEnd == voltageEnd + 1 ||
            (*socEnd != ',' && *socEnd != '\0') || soc < INT_MIN || soc > INT_MAX) {
            return false;
        }

        parsed[count++] = {static_cast<int>(voltage), static_cast<int>(soc)};
        cursor = (*socEnd == ',') ? socEnd + 1 : socEnd;
        if (*socEnd == ',' && *cursor == '\0') return false;
    }

    if (!validateSocCurvePoints(parsed, count)) return false;
    std::memcpy(outPoints, parsed, sizeof(SocCurvePoint) * count);
    outCount = count;
    return true;
}

int interpolateSocCurve(long cellVoltageMv, const SocCurvePoint* points, uint8_t count) {
    if (!validateSocCurvePoints(points, count)) return 0;
    if (cellVoltageMv <= points[0].voltageMv) return points[0].socPercent;
    if (cellVoltageMv >= points[count - 1].voltageMv) return points[count - 1].socPercent;

    for (uint8_t i = 1; i < count; ++i) {
        if (cellVoltageMv > points[i].voltageMv) continue;
        const long voltageSpan = points[i].voltageMv - points[i - 1].voltageMv;
        const long socSpan = points[i].socPercent - points[i - 1].socPercent;
        return static_cast<int>(points[i - 1].socPercent +
                                ((cellVoltageMv - points[i - 1].voltageMv) * socSpan) /
                                    voltageSpan);
    }
    return points[count - 1].socPercent;
}

bool formatSocCurvePoints(const SocCurvePoint* points, uint8_t count,
                          char* output, size_t outputSize) {
    if (!validateSocCurvePoints(points, count) || output == nullptr || outputSize == 0) {
        return false;
    }
    size_t used = 0;
    output[0] = '\0';
    for (uint8_t i = 0; i < count; ++i) {
        const int written = std::snprintf(output + used, outputSize - used, "%s%d:%d",
                                          i == 0 ? "" : ",", points[i].voltageMv,
                                          points[i].socPercent);
        if (written < 0 || static_cast<size_t>(written) >= outputSize - used) {
            output[0] = '\0';
            return false;
        }
        used += static_cast<size_t>(written);
    }
    return true;
}
