#pragma once

// Parse lowMv,lowSoc,highMv,highSoc without changing outCurve on failure.
bool parseSocVoltageCurve(const char* input, int outCurve[4]);
