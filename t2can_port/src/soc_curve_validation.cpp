#include "soc_curve_validation.h"

#include <cerrno>
#include <climits>
#include <cstdlib>

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
